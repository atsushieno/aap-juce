
#include <ctime>
#include <juce_audio_processors/juce_audio_processors.h>
#include "aap/android-audio-plugin.h"
#include "aap/unstable/logging.h"
#include "aap/ext/presets.h"
#include "aap/ext/state.h"
#include "aap/ext/aap-midi2.h"
#include "aap/ext/parameters.h"
#include "cmidi2.h"

#if ANDROID
#include <dlfcn.h>
#include <jni.h>
#include <android/looper.h>
#endif

using namespace juce;

extern juce::AudioProcessor *
createPluginFilter(); // it is defined in each Audio plugin project (by Projucer).

extern "C" { int juce_aap_wrapper_last_error_code{0}; }

#define JUCEAAP_SUCCESS 0
#define JUCEAAP_ERROR_INVALID_BUFFER -1
#define JUCEAAP_ERROR_PROCESS_BUFFER_ALTERED -2
#define JUCEAAP_ERROR_CHANNEL_IN_OUT_NUM_MISMATCH -3
#define JUCEAAP_LOG_PERF 0

// JUCE-AAP port mappings:
//
// 	JUCE AudioBuffer 0..nOut-1 -> AAP output ports 0..nOut-1
//  JUCE AudioBuffer nOut..nIn+nOut-1 -> AAP input ports nOut..nIn+nOut-1
//  IF exists JUCE MIDI input buffer -> AAP MIDI input port nIn+nOut
//  IF exists JUCE MIDI output buffer -> AAP MIDI output port last

class JuceAAPWrapper : juce::AudioPlayHead {
    AndroidAudioPlugin *aap;
    const char *plugin_unique_id;
    int sample_rate;
    AndroidAudioPluginHost *host;
    AndroidAudioPluginBuffer *buffer;
    aap_state_t state{nullptr, 0};
    juce::AudioProcessor *juce_processor;
    HeapBlock<float*> juce_channels;
    juce::MidiBuffer juce_midi_messages;

#if JUCEAAP_HAVE_AUDIO_PLAYHEAD_NEW_POSITION_INFO
    juce::AudioPlayHead::PositionInfo play_head_position;
#else
    juce::AudioPlayHead::CurrentPositionInfo play_head_position;
#endif

public:
    JuceAAPWrapper(AndroidAudioPlugin *plugin, const char *pluginUniqueId, int sampleRate,
                   AndroidAudioPluginHost *host)
            : aap(plugin), sample_rate(sampleRate), host(host) {
#if ANDROID
        typedef JavaVM*(*getJVMFunc)();
        auto libaap = dlopen("libandroidaudioplugin.so", RTLD_NOW);
        auto getJVM = (getJVMFunc) dlsym(libaap, "_ZN3aap15get_android_jvmEv"); // aap::get_android_jvm()
        auto jvm = getJVM();
        JNIEnv *env;
        jvm->AttachCurrentThread(&env, nullptr);
        auto looperClass = env->FindClass("android/os/Looper");
        auto myLooperMethod = env->GetStaticMethodID(looperClass, "myLooper",
                                                     "()Landroid/os/Looper;");
        auto prepareMethod = env->GetStaticMethodID(looperClass, "prepare", "()V");
        auto existingLooper = env->CallStaticObjectMethod(looperClass, myLooperMethod);
        if (!existingLooper)
            env->CallStaticVoidMethod(looperClass, prepareMethod);
#endif
        plugin_unique_id = pluginUniqueId == nullptr ? nullptr : strdup(pluginUniqueId);

        juce::MessageManager::getInstance(); // ensure that we have a message loop.
        juce_processor = createPluginFilter();
    }

    virtual ~JuceAAPWrapper() {
        juce_processor->releaseResources();

        if (state.data != nullptr)
            free((void *) state.data);
        if (plugin_unique_id != nullptr)
            free((void *) plugin_unique_id);
    }

#if JUCEAAP_HAVE_AUDIO_PLAYHEAD_NEW_POSITION_INFO
    Optional<PositionInfo> getPosition() const override {
        return play_head_position;
    }
#else
    bool getCurrentPosition(juce::AudioPlayHead::CurrentPositionInfo &result) override {
        result = play_head_position;
        return true;
    }
#endif

    void allocateBuffer(AndroidAudioPluginBuffer *buffer) {
        if (!buffer) {
            errno = juce_aap_wrapper_last_error_code = JUCEAAP_ERROR_INVALID_BUFFER;
            return;
        }
        // allocates juce_buffer. No need to interpret content.
        this->buffer = buffer;
        if (juce_processor->getBusCount(true) > 0) {
            if (juce_processor->getMainBusNumInputChannels() != juce_processor->getMainBusNumOutputChannels()) {
                errno = juce_aap_wrapper_last_error_code = JUCEAAP_ERROR_CHANNEL_IN_OUT_NUM_MISMATCH;
                return;
            }
            juce_processor->getBus(true, 0)->enable();
        }
        if (juce_processor->getBusCount(false) > 0)
            juce_processor->getBus(false, 0)->enable();
        //juce_buffer.setSize(juce_processor->getMainBusNumOutputChannels(), buffer->num_frames);
        juce_midi_messages.clear();
        cached_parameter_values.resize(juce_processor->getParameters().size());
        for (int i = 0; i < cached_parameter_values.size(); i++)
            cached_parameter_values[i] = 0.0f;
    }

    inline int getNumberOfChannelsOfBus(juce::AudioPluginInstance::Bus *bus) {
        return bus ? bus->getNumberOfChannels() : 0;
    }

    void prepare(AndroidAudioPluginBuffer *buffer) {
        allocateBuffer(buffer);
        if (juce_aap_wrapper_last_error_code != JUCEAAP_SUCCESS)
            return;

#if JUCEAAP_HAVE_AUDIO_PLAYHEAD_NEW_POSITION_INFO
        play_head_position.setBpm(120);
#else
        play_head_position.resetToDefault();
        play_head_position.bpm = 120;
#endif

        juce_processor->setPlayConfigDetails(
                getNumberOfChannelsOfBus(juce_processor->getBus(true, 0)),
                getNumberOfChannelsOfBus(juce_processor->getBus(false, 0)),
                sample_rate, buffer->num_frames);
        juce_processor->setPlayHead(this);

        juce_processor->prepareToPlay(sample_rate, buffer->num_frames);
    }

    void activate() {
        juce_channels.calloc(juce_processor->getMainBusNumInputChannels() + juce_processor->getMainBusNumOutputChannels());
#if JUCEAAP_HAVE_AUDIO_PLAYHEAD_NEW_POSITION_INFO
        play_head_position.setIsPlaying(true);
#else
        play_head_position.isPlaying = true;
#endif
    }

    void deactivate() {
#if JUCEAAP_HAVE_AUDIO_PLAYHEAD_NEW_POSITION_INFO
        play_head_position.setIsPlaying(false);
#else
        play_head_position.isPlaying = false;
#endif
        juce_channels.free();
    }

    int32_t current_bpm = 120; // FIXME: provide way to adjust it
    int32_t default_time_division = 192;

    std::vector<float> cached_parameter_values;
    uint8_t sysex_buffer[4096];
    int32_t sysex_offset{0};


    bool readMidi2Parameter(uint8_t *group, uint8_t* channel, uint8_t* key, uint8_t* extra,
                            uint16_t *index, float *value, cmidi2_ump* ump) {
        auto raw = (uint32_t*) ump;
        return aapReadMidi2ParameterSysex8(group, channel, key, extra, index, value,
                                           *raw, *(raw + 1), *(raw + 2), *(raw + 3));
    }

    void process(AndroidAudioPluginBuffer *audioBuffer, long timeoutInNanoseconds) {
#if JUCEAAP_LOG_PERF
        struct timespec timeSpecBegin, timeSpecEnd;
        struct timespec procTimeSpecBegin, procTimeSpecEnd;
        clock_gettime(CLOCK_REALTIME, &timeSpecBegin);
#endif

        int nOut = juce_processor->getMainBusNumOutputChannels();
        int nBuf = juce_processor->getMainBusNumInputChannels() +
                   juce_processor->getMainBusNumOutputChannels();

        for (int i = 0; i < nOut; i++)
            juce_channels[i] = (float*) audioBuffer->buffers[i];
        for (int i = nOut; i < nBuf; i++)
            juce_channels[i] = (float*) audioBuffer->buffers[i];

        int rawTimeDivision = default_time_division;

        if (juce_processor->acceptsMidi()) {
            // FIXME: for complete support for AudioPlayHead::CurrentPositionInfo, we would also
            //   have to store bpm and timeSignature, based on MIDI messages.

            juce_midi_messages.clear();
            sysex_offset = 0;
            int32_t midiBufferPortId = nBuf;
            auto midiInBuf = (AAPMidiBufferHeader*) audioBuffer->buffers[midiBufferPortId];
            auto umpStart = ((uint8_t*) midiInBuf) + sizeof(AAPMidiBufferHeader);
            int32_t positionInJRTimestamp = 0;

            CMIDI2_UMP_SEQUENCE_FOREACH(umpStart, midiInBuf->length, iter) {
                auto ump = (cmidi2_ump*) iter;
                auto messageType = cmidi2_ump_get_message_type(ump);
                auto statusCode = cmidi2_ump_get_status_code(ump);

                uint8_t paramGroup, paramChannel, paramKey{0}, paramExtra{0};
                uint16_t paramId;
                float paramValue;
                if (readMidi2Parameter(&paramGroup, &paramChannel, &paramKey, &paramExtra, &paramId, &paramValue, ump)) {
                    if (cached_parameter_values[paramId] != paramValue) {
                        auto param = juce_processor->getParameterTree().getParameters(true)[paramId];
                        param->setValue(paramValue);
                        cached_parameter_values[paramId] = paramValue;
                        param->sendValueChangedMessageToListeners (paramValue);
                    }
                    continue;
                }
            }

            // We could use cmidi2_convert_ump_to_midi1, but afterward we have to iterate midi1 bytes again...
            CMIDI2_UMP_SEQUENCE_FOREACH(umpStart, midiInBuf->length, iter) {
                auto ump = (cmidi2_ump*) iter;
                auto messageType = cmidi2_ump_get_message_type(ump);
                auto statusCode = cmidi2_ump_get_status_code(ump);
                if (messageType == CMIDI2_MESSAGE_TYPE_UTILITY) {
                    // Should we also cover JR Clock? how?
                    if (statusCode == CMIDI2_JR_TIMESTAMP)
                        positionInJRTimestamp += cmidi2_ump_get_jr_timestamp_timestamp(ump);
                    continue;
                }

                auto channel = cmidi2_ump_get_channel(ump);
                auto statusByte = statusCode | channel;
                MidiMessage m;
                int32_t sampleNumber = (positionInJRTimestamp * 1.0 / 31250.0) / sample_rate;

                switch (messageType) {
                    case CMIDI2_MESSAGE_TYPE_MIDI_1_CHANNEL:
                        juce_midi_messages.addEvent(
                                MidiMessage{statusByte,
                                            cmidi2_ump_get_midi1_byte2(ump),
                                            cmidi2_ump_get_midi1_byte3(ump)},
                                            sampleNumber);
                        break;
                    case CMIDI2_MESSAGE_TYPE_MIDI_2_CHANNEL: {
                        switch (statusCode) {
                            /* We don't treat them here, as they are used for the parameter changes.
                               FIXME: does this actually make sense? I'm afraid not...
                            case CMIDI2_STATUS_RPN: {
                                auto data = cmidi2_ump_get_midi2_rpn_data(ump);
                                juce_midi_messages.addEvent(
                                        MidiMessage{CMIDI2_STATUS_CC + channel,
                                                    CMIDI2_CC_RPN_MSB,
                                                    cmidi2_ump_get_midi2_rpn_msb(ump)},
                                        sampleNumber);
                                juce_midi_messages.addEvent(
                                        MidiMessage{CMIDI2_STATUS_CC + channel,
                                                    CMIDI2_CC_RPN_LSB,
                                                    cmidi2_ump_get_midi2_rpn_lsb(ump)},
                                        sampleNumber);
                                juce_midi_messages.addEvent(
                                        MidiMessage{CMIDI2_STATUS_CC + channel,
                                                    CMIDI2_CC_DTE_MSB,
                                                    (uint8_t) (data >> 25) & 0x7F},
                                        sampleNumber);
                                juce_midi_messages.addEvent(
                                        MidiMessage{CMIDI2_STATUS_CC + channel,
                                                    CMIDI2_CC_DTE_MSB,
                                                    (uint8_t) (data >> 18) & 0x7F},
                                        sampleNumber);
                            } break;
                            case CMIDI2_STATUS_NRPN: {
                                auto data = cmidi2_ump_get_midi2_nrpn_data(ump);
                                juce_midi_messages.addEvent(
                                        MidiMessage{CMIDI2_STATUS_CC + channel,
                                                    CMIDI2_CC_NRPN_MSB,
                                                    cmidi2_ump_get_midi2_nrpn_msb(ump)},
                                        sampleNumber);
                                juce_midi_messages.addEvent(
                                        MidiMessage{CMIDI2_STATUS_CC + channel,
                                                    CMIDI2_CC_NRPN_LSB,
                                                    cmidi2_ump_get_midi2_nrpn_lsb(ump)},
                                        sampleNumber);
                                juce_midi_messages.addEvent(
                                        MidiMessage{CMIDI2_STATUS_CC + channel,
                                                    CMIDI2_CC_DTE_MSB,
                                                    (uint8_t) (data >> 25) & 0x7F},
                                        sampleNumber);
                                juce_midi_messages.addEvent(
                                        MidiMessage{CMIDI2_STATUS_CC + channel,
                                                    CMIDI2_CC_DTE_MSB,
                                                    (uint8_t) (data >> 18) & 0x7F},
                                        sampleNumber);
                            } break;
                            */
                            case CMIDI2_STATUS_NOTE_OFF:
                            case CMIDI2_STATUS_NOTE_ON:
                                juce_midi_messages.addEvent(
                                        MidiMessage{statusCode,
                                                    cmidi2_ump_get_midi2_note_note(ump),
                                                    cmidi2_ump_get_midi2_note_velocity(ump) /
                                                    0x200},
                                        sampleNumber);
                                break;
                            case CMIDI2_STATUS_PAF:
                                juce_midi_messages.addEvent(
                                        MidiMessage{statusCode,
                                                    cmidi2_ump_get_midi2_note_note(ump),
                                                    (uint8_t) (cmidi2_ump_get_midi2_paf_data(ump) /
                                                               0x2000000)},
                                        sampleNumber);
                                break;
                            case CMIDI2_STATUS_CC:
                                juce_midi_messages.addEvent(
                                        MidiMessage{statusCode,
                                                    cmidi2_ump_get_midi2_cc_index(ump),
                                                    (uint8_t) (cmidi2_ump_get_midi2_cc_data(ump) /
                                                               0x2000000)},
                                        sampleNumber);
                                break;
                            case CMIDI2_STATUS_PROGRAM:
                                if (cmidi2_ump_get_midi2_program_options(ump) &
                                    CMIDI2_PROGRAM_CHANGE_OPTION_BANK_VALID) {
                                    juce_midi_messages.addEvent(
                                            MidiMessage{CMIDI2_STATUS_CC + channel,
                                                        CMIDI2_CC_BANK_SELECT,
                                                        cmidi2_ump_get_midi2_program_bank_msb(ump)},
                                            sampleNumber);
                                    juce_midi_messages.addEvent(
                                            MidiMessage{CMIDI2_STATUS_CC + channel,
                                                        CMIDI2_CC_BANK_SELECT_LSB,
                                                        cmidi2_ump_get_midi2_program_bank_lsb(ump)},
                                            sampleNumber);
                                }
                                juce_midi_messages.addEvent(
                                        MidiMessage{statusByte,
                                                    cmidi2_ump_get_midi2_program_program(ump),
                                                    0},
                                        sampleNumber);
                                break;
                            case CMIDI2_STATUS_CAF:
                                juce_midi_messages.addEvent(
                                        MidiMessage{statusByte,
                                                    (uint8_t) (cmidi2_ump_get_midi2_caf_data(ump) /
                                                               0x2000000),
                                                    0},
                                        sampleNumber);
                                break;
                            case CMIDI2_STATUS_PITCH_BEND: {
                                auto data = cmidi2_ump_get_midi2_pitch_bend_data(ump);
                                juce_midi_messages.addEvent(
                                        MidiMessage{statusByte,
                                                    (uint8_t) (data >> 25) & 0x7F,
                                                    (uint8_t) (data >> 18) & 0x7F},
                                        sampleNumber);
                            } break;
                        }
                    } break;
                    case CMIDI2_MESSAGE_TYPE_SYSEX7: {
                        switch (statusCode) {
                            case CMIDI2_SYSEX_IN_ONE_UMP:
                            case CMIDI2_SYSEX_START:
                                sysex_buffer[0] = 0xF0;
                                sysex_offset = 1;
                                break;
                        }

                        auto sysex7U64 = cmidi2_ump_read_uint64_bytes(ump);
                        auto sysex7NumBytesInUmp = cmidi2_ump_get_sysex7_num_bytes(ump);
                        for (size_t i = 0; i < sysex7NumBytesInUmp; i++)
                            if (sysex_offset < sizeof(sysex_buffer))
                                sysex_buffer[sysex_offset++] = cmidi2_ump_get_byte_from_uint64(sysex7U64, 2 + i);

                        switch (statusCode) {
                            case CMIDI2_SYSEX_IN_ONE_UMP:
                            case CMIDI2_SYSEX_END:
                                if (sysex_offset < sizeof(sysex_buffer)) {
                                    sysex_buffer[sysex_offset++] = 0xF7;
                                    juce_midi_messages.addEvent(
                                            MidiMessage(sysex_buffer, sysex_offset),
                                            sampleNumber);
                                }
                                sysex_offset = 0;
                                break;
                        }
                        break;
                    }
                }
            }
        }

        // process data by the JUCE plugin
#if JUCEAAP_LOG_PERF
        clock_gettime(CLOCK_REALTIME, &procTimeSpecBegin);
#endif
        juce::AudioSampleBuffer juceAudioBuffer(juce_channels, jmax(nBuf - nOut, nOut), audioBuffer->num_frames);

        juce_processor->processBlock(juceAudioBuffer, juce_midi_messages);

#if JUCEAAP_LOG_PERF
        clock_gettime(CLOCK_REALTIME, &procTimeSpecEnd);
        long procTimeDiff = (procTimeSpecEnd.tv_sec - procTimeSpecBegin.tv_sec) * 1000000000 + procTimeSpecEnd.tv_nsec - procTimeSpecBegin.tv_nsec;
#endif

#if JUCEAAP_HAVE_AUDIO_PLAYHEAD_NEW_POSITION_INFO
        play_head_position.setTimeInSamples(*play_head_position.getTimeInSamples() + audioBuffer->num_frames);
        auto thisTimeInSeconds = 1.0 * audioBuffer->num_frames / sample_rate;
        play_head_position.setPpqPosition(*play_head_position.getPpqPosition() + *play_head_position.getBpm() / 60 * 4 * thisTimeInSeconds);
#else
        play_head_position.timeInSamples += audioBuffer->num_frames;
        auto thisTimeInSeconds = 1.0 * audioBuffer->num_frames / sample_rate;
        play_head_position.timeInSeconds += thisTimeInSeconds;
        play_head_position.ppqPosition += play_head_position.bpm / 60 * 4 * thisTimeInSeconds;
#endif

        if (juce_processor->producesMidi()) {
            // This part is not really verified... we need some JUCE plugin that generates some outputs.
            int32_t midiOutBufIndex = nBuf + (juce_processor->acceptsMidi() ? 1 : 0);
            void *dst = buffer->buffers[midiOutBufIndex];
            auto outMidiBuf = (AAPMidiBufferHeader*) dst;
            auto umpDst = (cmidi2_ump*) ((uint8_t*) dst + sizeof(AAPMidiBufferHeader));

            MidiBuffer::Iterator iterator{juce_midi_messages};
            const uint8_t *data;
            int32_t eventSize, eventPos, dstBufSize = 0;
            while (iterator.getNextEvent(data, eventSize, eventPos)) {
                if (data[eventPos] == 0xF0) {
                    // sysex
                    memcpy(sysex_buffer, data + eventPos, eventSize);
                    auto numPackets = cmidi2_ump_sysex7_get_num_packets(eventSize);
                    for (int i = 0; i < numPackets; i++)
                        cmidi2_ump_write64(umpDst, cmidi2_ump_sysex7_get_packet_of(
                                0, i + 1 < numPackets ? 6 : eventSize % 6,
                                (void *) (data + eventPos + i * 6), i));
                    umpDst += 2;
                } else if (data[eventPos] > 0xF0) {
                    cmidi2_ump_write32(umpDst, cmidi2_ump_system_message(
                            0, data[eventPos],
                            eventSize > 1 ? data[eventPos + 1] : 0,
                            eventSize > 2 ? data[eventPos + 2] : 0));
                    umpDst++;
                } else {
                    cmidi2_ump_write32(umpDst, cmidi2_ump_midi1_message(
                            0, data[eventPos] & 0xF0, data[eventPos] & 0xF,
                            eventSize > 1 ? data[eventPos + 1] : 0,
                            eventSize > 2 ? data[eventPos + 2] : 0));
                    umpDst++;
                }
                dstBufSize += eventSize;
            }
            outMidiBuf->length = dstBufSize;
        }

#if JUCEAAP_LOG_PERF
        clock_gettime(CLOCK_REALTIME, &timeSpecEnd);
        long timeDiff = (timeSpecEnd.tv_sec - timeSpecBegin.tv_sec) * 1000000000 + timeSpecEnd.tv_nsec - timeSpecBegin.tv_nsec;
        aap::aprintf("AAP JUCE Bridge %s - Synth TAT: %ld", plugin_unique_id, procTimeDiff);
        aap::aprintf("AAP JUCE Bridge %s - Process TAT: time diff %ld", plugin_unique_id, timeDiff);
#endif
    }

    size_t getStateSize() {
        MemoryBlock mb;
        mb.reset();
        juce_processor->getStateInformation(mb);
        return mb.getSize();
    }

    void getState(aap_state_t *result) {
        MemoryBlock mb;
        mb.reset();
        juce_processor->getStateInformation(mb);
        if (state.data == nullptr || state.data_size < mb.getSize()) {
            if (state.data != nullptr)
                free((void *) state.data);
            state.data = calloc(mb.getSize(), 1);
        }
        memcpy((void *) state.data, mb.begin(), mb.getSize());
        result->data_size = state.data_size;
        result->data = state.data;
    }

    void setState(aap_state_t *input) {
        juce_processor->setStateInformation(input->data, input->data_size);
    }

    int32_t getPresetCount() {
        return juce_processor->getNumPrograms();
    }

    int32_t getPresetDataSize(int32_t index) {
        // There is no way in JUCE to retrieve data size of a preset program without once setting it as current...
        auto bak = juce_processor->getCurrentProgram();
        juce_processor->setCurrentProgram(index);
        MemoryBlock mb{};
        juce_processor->getCurrentProgramStateInformation(mb);
        auto ret = mb.getSize();
        juce_processor->setCurrentProgram(bak);
        return ret;
    }

    void getPreset(int32_t index, bool skipBinary, aap_preset_t* preset) {
        // There is no way in JUCE to retrieve data size of a preset program without once setting it as current...
        // And we need getcurrentProgramStateInformation() anyways, so skipBinary does not really optimize a lot...
        auto bak = juce_processor->getCurrentProgram();
        juce_processor->getProgramName(index).copyToUTF8(preset->name, AAP_PRESETS_EXTENSION_MAX_NAME_LENGTH);
        juce_processor->setCurrentProgram(index);
        MemoryBlock mb{};
        juce_processor->getCurrentProgramStateInformation(mb);
        if (!skipBinary)
            mb.copyTo(preset->data, 0, preset->data_size);
        if (preset->data_size > (int32_t) mb.getSize())
            preset->data_size = (int32_t) mb.getSize();
        juce_processor->setCurrentProgram(bak);
    }

    int32_t getPresetIndex() {
        return juce_processor->getCurrentProgram();
    }

    void setPresetIndex(int32_t index) {
        juce_processor->setCurrentProgram(index);
    }
};

// AAP plugin API ----------------------------------------------------------------------

JuceAAPWrapper *getWrapper(AndroidAudioPlugin *plugin) {
    return (JuceAAPWrapper *) plugin->plugin_specific;
}

void juceaap_prepare(
        AndroidAudioPlugin *plugin,
        AndroidAudioPluginBuffer *audioBuffer) {
    getWrapper(plugin)->prepare(audioBuffer);
}

void juceaap_activate(AndroidAudioPlugin *plugin) {
    getWrapper(plugin)->activate();
}

void juceaap_deactivate(AndroidAudioPlugin *plugin) {
    getWrapper(plugin)->deactivate();
}

void juceaap_process(
        AndroidAudioPlugin *plugin,
        AndroidAudioPluginBuffer *audioBuffer,
        long timeoutInNanoseconds) {
    getWrapper(plugin)->process(audioBuffer, timeoutInNanoseconds);
}

size_t juceaap_get_state_size(AndroidAudioPluginExtensionTarget target) {
    return getWrapper(target.plugin)->getStateSize();
}

void juceaap_get_state(AndroidAudioPluginExtensionTarget target, aap_state_t *result) {
    getWrapper(target.plugin)->getState(result);
}

void juceaap_set_state(AndroidAudioPluginExtensionTarget target, aap_state_t *input) {
    getWrapper(target.plugin)->setState(input);
}

// Extensions ----------------------------------------------------------------------

// presets extension

int32_t juce_aap_wrapper_get_preset_count(AndroidAudioPluginExtensionTarget target) {
    auto wrapper = (JuceAAPWrapper*) target.plugin->plugin_specific;
    return wrapper->getPresetCount();
}

int32_t juce_aap_wrapper_get_preset_data_size(AndroidAudioPluginExtensionTarget target, int32_t index) {
    auto wrapper = (JuceAAPWrapper*) target.plugin->plugin_specific;
    return wrapper->getPresetDataSize(index);
}

void juce_aap_wrapper_get_preset(AndroidAudioPluginExtensionTarget target, int32_t index, bool skipBinary, aap_preset_t *preset) {
    auto wrapper = (JuceAAPWrapper*) target.plugin->plugin_specific;
    return wrapper->getPreset(index, skipBinary, preset);
}

int32_t juce_aap_wrapper_get_preset_index(AndroidAudioPluginExtensionTarget target) {
    auto wrapper = (JuceAAPWrapper*) target.plugin->plugin_specific;
    return wrapper->getPresetIndex();
}

void juce_aap_wrapper_set_preset_index(AndroidAudioPluginExtensionTarget target, int32_t index) {
    auto wrapper = (JuceAAPWrapper*) target.plugin->plugin_specific;
    wrapper->setPresetIndex(index);
}

std::map<AndroidAudioPlugin *, std::unique_ptr<aap_state_extension_t>> state_ext_map{};
std::map<AndroidAudioPlugin *, std::unique_ptr<aap_presets_extension_t>> presets_ext_map{};

void* juceaap_get_extension(AndroidAudioPlugin *plugin, const char *uri) {
    if (strcmp(uri, AAP_STATE_EXTENSION_URI) == 0) {
        if (!state_ext_map[plugin]) {
            aap_state_extension_t newInstance{juceaap_get_state_size,
                                                juceaap_get_state,
                                                juceaap_set_state};

            state_ext_map[plugin] = std::make_unique<aap_state_extension_t>(newInstance);
        }
        return state_ext_map[plugin].get();
    }
    if (strcmp(uri, AAP_PRESETS_EXTENSION_URI) == 0) {
        if (!presets_ext_map[plugin]) {
            aap_presets_extension_t newInstance{juce_aap_wrapper_get_preset_count,
                                                juce_aap_wrapper_get_preset_data_size,
                                                juce_aap_wrapper_get_preset,
                                                juce_aap_wrapper_get_preset_index,
                                                juce_aap_wrapper_set_preset_index};

            presets_ext_map[plugin] = std::make_unique<aap_presets_extension_t>(newInstance);
        }
        return presets_ext_map[plugin].get();
    }
    return nullptr;
}

// Plugin factory -------------------------------------------------------------------

AndroidAudioPlugin *juceaap_instantiate(
        AndroidAudioPluginFactory *pluginFactory,
        const char *pluginUniqueId,
        int sampleRate,
        AndroidAudioPluginHost *host) {
    auto *ret = new AndroidAudioPlugin();
    auto *ctx = new JuceAAPWrapper(ret, pluginUniqueId, sampleRate, host);

    ret->plugin_specific = ctx;

    ret->prepare = juceaap_prepare;
    ret->activate = juceaap_activate;
    ret->process = juceaap_process;
    ret->deactivate = juceaap_deactivate;
    ret->get_extension = juceaap_get_extension;

    return ret;
}

void juceaap_release(
        AndroidAudioPluginFactory *pluginFactory,
        AndroidAudioPlugin *instance) {
    auto ctx = getWrapper(instance);
    if (ctx != nullptr) {
        delete ctx;
        instance->plugin_specific = nullptr;
    }
    if (presets_ext_map[instance]) {
        presets_ext_map[instance].reset(nullptr);
        presets_ext_map.erase(instance);
    }
    delete instance;
}

struct AndroidAudioPluginFactory juceaap_factory{
        juceaap_instantiate,
        juceaap_release,
        nullptr
};

extern "C" AndroidAudioPluginFactory *GetJuceAAPFactory() {
    return &juceaap_factory;
}


// The code below are used by aap-metadata-generator tool ----------------------------------

#define JUCEAAP_EXPORT_AAP_METADATA_SUCCESS 0
#define JUCEAAP_EXPORT_AAP_METADATA_INVALID_DIRECTORY 1
#define JUCEAAP_EXPORT_AAP_METADATA_INVALID_OUTPUT_FILE 2

void generate_xml_parameter_node(XmlElement *parent,
                                 const AudioProcessorParameterGroup::AudioProcessorParameterNode *node) {
    auto group = node->getGroup();
    if (group != nullptr) {
        auto childXml = parent->createNewChildElement("parameters");
        childXml->setAttribute("xmlns", "urn://androidaudioplugin.org/extensions/parameters");
        childXml->setAttribute("name", group->getName());
        for (auto childPara : *group)
            generate_xml_parameter_node(childXml, childPara);
    } else {
        auto para = node->getParameter();
        auto childXml = parent->createNewChildElement("parameter");
        childXml->setAttribute("id", para->getParameterIndex());
        childXml->setAttribute("name", para->getName(1024));
        childXml->setAttribute("direction", "input"); // JUCE does not support output parameter.
        if (!std::isnormal(para->getDefaultValue()))
            childXml->setAttribute("default", para->getDefaultValue());
        auto ranged = dynamic_cast<RangedAudioParameter *>(para);
        if (ranged) {
            auto range = ranged->getNormalisableRange();
            if (std::isnormal(range.start))
                childXml->setAttribute("minimum", range.start);
            if (std::isnormal(range.end))
                childXml->setAttribute("maximum", range.end);
            if (std::isnormal(ranged->getDefaultValue()))
                childXml->setAttribute("default", range.convertTo0to1(ranged->getDefaultValue()));
        }
        else
            childXml->setAttribute("default", para->getDefaultValue());
        childXml->setAttribute("content", "other");
    }
}

extern "C" {

__attribute__((used))
__attribute__((visibility("default")))
int generate_aap_metadata(const char *aapMetadataFullPath, const char *library = "libjuce_jni.so",
                          const char *entrypoint = "GetJuceAAPFactory") {

    // FIXME: start application loop

    auto filter = createPluginFilter();
    filter->enableAllBuses();

    String name = "aapports:juce/" + filter->getName();
    auto parameters = filter->getParameters();

    File aapMetadataFile{aapMetadataFullPath};
    if (!aapMetadataFile.getParentDirectory().exists()) {
        std::cerr << "Output directory '" << aapMetadataFile.getParentDirectory().getFullPathName()
                  << "' does not exist." << std::endl;
        return JUCEAAP_EXPORT_AAP_METADATA_INVALID_DIRECTORY;
    }
    std::unique_ptr <juce::XmlElement> pluginsElement{new XmlElement("plugins")};
    pluginsElement->setAttribute("xmlns", "urn:org.androidaudioplugin.core");
    auto pluginElement = pluginsElement->createNewChildElement("plugin");
    pluginElement->setAttribute("name", JucePlugin_Name);
    pluginElement->setAttribute("category", JucePlugin_IsSynth ? "Instrument" : "Effect");
    pluginElement->setAttribute("author", JucePlugin_Manufacturer);
    pluginElement->setAttribute("manufacturer", JucePlugin_ManufacturerWebsite);
    pluginElement->setAttribute("unique-id",
                                String::formatted("juceaap:%x", JucePlugin_PluginCode));
    //pluginElement->setAttribute("library", "lib" JucePlugin_Name ".so");
    pluginElement->setAttribute("library", library);
    pluginElement->setAttribute("entrypoint", entrypoint);
    pluginElement->setAttribute("assets", "");

    auto topLevelExtensionsElement = pluginElement->createNewChildElement("extensions");
    auto extensionElement = topLevelExtensionsElement->createNewChildElement("extension");
    extensionElement->setAttribute("uri", AAP_PRESETS_EXTENSION_URI);

    auto &tree = filter->getParameterTree();
    auto topLevelParametersElement = pluginElement->createNewChildElement("parameters");
    topLevelParametersElement->setAttribute("xmlns", "urn://androidaudioplugin.org/extensions/parameters");
    for (auto node : tree) {
        generate_xml_parameter_node(topLevelParametersElement, node);
    }

    auto topLevelPortsElement = pluginElement->createNewChildElement("ports");
    auto outChannels = filter->getBusesLayout().getMainOutputChannelSet();
    for (int i = 0; i < outChannels.size(); i++) {
        auto portXml = topLevelPortsElement->createNewChildElement("port");
        portXml->setAttribute("direction", "output");
        portXml->setAttribute("content", "audio");
        portXml->setAttribute("name",
                              outChannels.getChannelTypeName(outChannels.getTypeOfChannel(i)));
    }
    auto inChannels = filter->getBusesLayout().getMainInputChannelSet();
    for (int i = 0; i < inChannels.size(); i++) {
        auto portXml = topLevelPortsElement->createNewChildElement("port");
        portXml->setAttribute("direction", "input");
        portXml->setAttribute("content", "audio");
        portXml->setAttribute("name",
                              inChannels.getChannelTypeName(inChannels.getTypeOfChannel(i)));
    }
    if (filter->acceptsMidi()) {
        auto portXml = topLevelPortsElement->createNewChildElement("port");
        portXml->setAttribute("direction", "input");
        portXml->setAttribute("content", "midi2");
        portXml->setAttribute("name", "MIDI input");
    }
    if (filter->producesMidi()) {
        auto portXml = topLevelPortsElement->createNewChildElement("port");
        portXml->setAttribute("direction", "output");
        portXml->setAttribute("content", "midi2");
        portXml->setAttribute("name", "MIDI output");
    }

    FileOutputStream output{aapMetadataFile};
    if (!output.openedOk()) {
        std::cerr << "Cannot create output file '" << aapMetadataFile.getFullPathName() << "'"
                  << std::endl;
        return JUCEAAP_EXPORT_AAP_METADATA_INVALID_OUTPUT_FILE;
    }
    auto s = pluginsElement->toString();
    output.writeText(s, false, false, "\n");
    output.flush();

    delete filter;

    return JUCEAAP_EXPORT_AAP_METADATA_SUCCESS;
}

}
