
#include <ctime>
#include <juce_audio_processors/juce_audio_processors.h>
#include "aap/android-audio-plugin.h"
#include "aap/core/host/plugin-host.h"
#include "aap/unstable/logging.h"
#include "aap/ext/presets.h"
#include "aap/ext/state.h"
#include "aap/ext/midi.h"
#include "aap/ext/parameters.h"
#include "aap/ext/plugin-info.h"
#include "aap/ext/gui.h"
#include "cmidi2.h"

#if ANDROID
#include <dlfcn.h>
#include <jni.h>
#include <android/looper.h>
#include <android/trace.h>
#endif

using namespace juce;

#define AAP_JUCE_TAG "AAP-JUCE"

extern juce::AudioProcessor *
createPluginFilter(); // it is defined in each Audio plugin project (by Projucer).

extern "C" void juce_juceEventsAndroidStartApp();

extern "C" { int juce_aap_wrapper_last_error_code{0}; }

#define JUCEAAP_SUCCESS 0
#define JUCEAAP_ERROR_INVALID_BUFFER -1
#define JUCEAAP_ERROR_PROCESS_BUFFER_ALTERED -2
#define JUCEAAP_ERROR_CHANNEL_IN_OUT_NUM_MISMATCH -3

// JUCE-AAP port mappings:
//
// 	JUCE AudioBuffer 0..nOut-1 -> AAP output ports 0..nOut-1
//  JUCE AudioBuffer nOut..nIn+nOut-1 -> AAP input ports nOut..nIn+nOut-1
//  IF exists JUCE MIDI input buffer -> AAP MIDI input port nIn+nOut
//  IF exists JUCE MIDI output buffer -> AAP MIDI output port last

class JuceAAPWrapper : juce::AudioPlayHead, juce::AudioProcessorListener {
    AndroidAudioPlugin *aap;
    const char *plugin_unique_id;
    int sample_rate;
    AndroidAudioPluginHost host;
    aap_buffer_t *buffer;
    aap_state_t state{nullptr, 0};
    juce::AudioProcessor *juce_processor;
    juce::HeapBlock<float*> juce_channels;
    juce::AudioSampleBuffer juce_audio_buffer;

    juce::MidiBuffer juce_midi_messages;
    int32_t aap_midi2_in_port{-1}, aap_midi2_out_port{-1};
    std::map<int32_t,int32_t> aap_to_juce_portmap_in{};
    std::map<int32_t,int32_t> aap_to_juce_portmap_out{};
    std::map<int32_t,int32_t> portmap_juce_to_aap_out{};

#if JUCEAAP_HAVE_AUDIO_PLAYHEAD_NEW_POSITION_INFO
    juce::AudioPlayHead::PositionInfo play_head_position;
#else
    juce::AudioPlayHead::CurrentPositionInfo play_head_position;
#endif

public:
    JuceAAPWrapper(AndroidAudioPlugin *plugin, const char *pluginUniqueId, int sampleRate,
                   AndroidAudioPluginHost *aapHost)
            : aap(plugin), sample_rate(sampleRate), host(*aapHost) {
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

        // Note that if we did not have invoked MessageManager::getInstance() until here, it will crash.
        // It must have been done at initialiseJUCE().
        juce_processor = createPluginFilter();

        buildParameterList();

        juce_processor->addListener(this);
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

    AndroidAudioPluginHost& getHost() { return host; }

    const char* getPluginId() { return plugin_unique_id; }

    juce::OwnedArray<aap_parameter_info_t> aapParams{};
    juce::HashMap<int32_t,int32_t> aapParamIdToEnumIndex{};
    juce::OwnedArray<aap_parameter_enum_t> aapEnums{};

    void registerParameter(juce::String path, juce::AudioProcessorParameter* para) {
        aap_parameter_info_t info;
        strncpy(info.path, path.toRawUTF8(), sizeof(info.path));
        info.stable_id = static_cast<int16_t>(para->getParameterIndex());
        auto nameMax = sizeof(info.display_name);
        const char* paramName = para->getName((int32_t) nameMax).toRawUTF8();
        strncpy(info.display_name, paramName, nameMax);
        auto ranged = dynamic_cast<juce::RangedAudioParameter *>(para);
        if (ranged) {
            auto range = ranged->getNormalisableRange();
            if (std::isnormal(range.start) || range.start == 0.0)
                info.min_value = range.start;
            if (std::isnormal(range.end) || range.end == 0.0)
                info.max_value = range.end;
            if (std::isnormal(ranged->getDefaultValue()) || ranged->getDefaultValue() == 0.0)
                info.default_value = range.convertTo0to1(ranged->getDefaultValue());
        }
        else if (std::isnormal(para->getDefaultValue()))
            info.default_value = para->getDefaultValue();
        auto names = para->getAllValueStrings();
        if (!names.isEmpty()) {
            aapParamIdToEnumIndex.set(info.stable_id, aapEnums.size());
            for (auto name : names) {
                aap_parameter_enum_t e;
                e.value = para->getValueForText(name);
                strncpy(e.name, name.toRawUTF8(), sizeof(e.name));
                aapEnums.add(new aap_parameter_enum_t(e));
            }
        }
        aapParams.add(new aap_parameter_info_t(info));
    }

    void registerParameters(juce::String path, const juce::AudioProcessorParameterGroup::AudioProcessorParameterNode* node) {
        auto group = node->getGroup();
        if (group != nullptr) {
            juce::String curPath = path + "/" + group->getName();
            for (const auto childPara : *group)
                registerParameters(curPath, childPara);
        } else {
            auto para = node->getParameter();
            registerParameter(path, para);
        }
    }

    void buildParameterList() {
        aapParams.clear();
        aapParamIdToEnumIndex.clear();
        aapEnums.clear();

        auto &tree = juce_processor->getParameterTree();
        for (auto node : tree)
            registerParameters("", node);
        if (aapParams.size() == 0)
            for (auto para : juce_processor->getParameters())
                registerParameter("", para);
        if (aapParams.size() == 0) {
            // Some classic plugins (such as OB-Xd) do not return parameter objects.
            // They still return parameter names, so we can still generate AAP parameters.
            for (int i = 0, n = juce_processor->getNumParameters(); i < n; i++) {
                aap_parameter_info_t p;
                p.stable_id = (int16_t) i;
                strncpy(p.display_name, juce_processor->getParameterName(i).toRawUTF8(), sizeof(p.display_name));
                p.min_value = 0.0;
                p.max_value = 1.0;
                p.default_value = juce_processor->getParameter(i);
                aapParams.add(new aap_parameter_info_t(p));
            }
        }
    }

    // juce::AudioProcessorListener implementation
    void audioProcessorParameterChanged(juce::AudioProcessor* processor, int parameterIndex, float newValue) override {
    }

#if JUCEAAP_AUDIO_PROCESSOR_CHANGE_DETAILS_UNAVAILABLE
    void audioProcessorChanged(juce::AudioProcessor* processor) override {
        auto ext = (aap_parameters_host_extension_t *) host.get_extension(&host, AAP_PARAMETERS_EXTENSION_URI);
        if (ext)
            ext->notify_parameters_changed(ext, &host);
    }
#else
    void audioProcessorChanged(juce::AudioProcessor* processor, const juce::AudioProcessorListener::ChangeDetails &details) override {
        if (details.parameterInfoChanged) {
            auto ext = (aap_parameters_host_extension_t *) host.get_extension(&host, AAP_PARAMETERS_EXTENSION_URI);
            if (ext)
                ext->notify_parameters_changed(ext, &host);
        }
    }
#endif

    void addAndroidView(void* parentLinearLayout) {
        auto creator = [&] {
            auto editor = juce_processor->createEditorIfNeeded();
            editor->setVisible(true);
            editor->addToDesktop(0, parentLinearLayout);
        };
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            creator();
        else
            juce::MessageManager::callAsync(creator);
    }

    void allocateBuffer(aap_buffer_t *aapBuffer) {
        if (!aapBuffer) {
            errno = juce_aap_wrapper_last_error_code = JUCEAAP_ERROR_INVALID_BUFFER;
            aap::a_log_f(AAP_LOG_LEVEL_ERROR, AAP_JUCE_TAG, "null buffer passed to allocateBuffer()");
            return;
        }

        juce_channels.calloc(juce_processor->getMainBusNumInputChannels() + juce_processor->getMainBusNumOutputChannels());

        juce_audio_buffer.setSize(juce_processor->getMainBusNumOutputChannels(), aapBuffer->num_frames(*aapBuffer));

        // allocates juce_buffer. No need to interpret content.
        this->buffer = aapBuffer;
        if (juce_processor->getBusCount(true) > 0) {
            juce_processor->getBus(true, 0)->enable();
        }
        if (juce_processor->getBusCount(false) > 0)
            juce_processor->getBus(false, 0)->enable();
        juce_midi_messages.clear();
    }

    inline int getNumberOfChannelsOfBus(juce::AudioPluginInstance::Bus *bus) {
        return bus ? bus->getNumberOfChannels() : 0;
    }

    void prepare(aap_buffer_t *buffer) {
        allocateBuffer(buffer);
        if (juce_aap_wrapper_last_error_code != JUCEAAP_SUCCESS)
            return;

        // retrieve AAP MDI2 ports. They are different from juce::AudioProcessor.acceptsMidi()
        auto pluginInfoExt = (aap_host_plugin_info_extension_t*) host.get_extension(&host, AAP_PLUGIN_INFO_EXTENSION_URI);
        if (pluginInfoExt) {
            aap_to_juce_portmap_in.clear();
            aap_to_juce_portmap_out.clear();
            portmap_juce_to_aap_out.clear();
            int jucePortInIdx = 0, jucePortOutIdx = 0;
            int nOut = juce_processor->getMainBusNumOutputChannels();

            auto pluginInfo = pluginInfoExt->get(pluginInfoExt, &host, plugin_unique_id);
            for (int i = 0, n = pluginInfo.get_port_count(&pluginInfo); i < n; i++) {
                auto port = pluginInfo.get_port(&pluginInfo, i);
                switch (port.content_type(&port)) {
                    case AAP_CONTENT_TYPE_MIDI2:
                        if (port.direction(&port) == AAP_PORT_DIRECTION_INPUT)
                            aap_midi2_in_port = i;
                        else
                            aap_midi2_out_port = i;
                        break;
                    case AAP_CONTENT_TYPE_AUDIO:
                        if (port.direction(&port) == AAP_PORT_DIRECTION_INPUT)
                            aap_to_juce_portmap_in[i] = jucePortInIdx++;
                        else {
                            portmap_juce_to_aap_out[jucePortOutIdx] = i;
                            aap_to_juce_portmap_out[i] = jucePortOutIdx++;
                        }
                        break;
                    default:
                        break;
                }
            }
        }

#if JUCEAAP_HAVE_AUDIO_PLAYHEAD_NEW_POSITION_INFO
        play_head_position.setBpm(120);
#else
        play_head_position.resetToDefault();
        play_head_position.bpm = 120;
#endif

        juce_processor->setPlayConfigDetails(
                getNumberOfChannelsOfBus(juce_processor->getBus(true, 0)),
                getNumberOfChannelsOfBus(juce_processor->getBus(false, 0)),
                sample_rate, buffer->num_frames(*buffer));
        juce_processor->setPlayHead(this);

        juce_processor->prepareToPlay(sample_rate, buffer->num_frames(*buffer));
    }

    void activate() {
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
    }

    int32_t current_bpm = 120; // FIXME: provide way to adjust it
    int32_t default_time_division = 192;

    uint8_t sysex_buffer[4096];
    int32_t sysex_offset{0};


    bool readMidi2Parameter(uint8_t *group, uint8_t* channel, uint8_t* key, uint8_t* extra,
                            uint16_t *index, float *value, cmidi2_ump* ump) {
        auto raw = (uint32_t*) ump;
        return aapReadMidi2ParameterSysex8(group, channel, key, extra, index, value,
                                           *raw, *(raw + 1), *(raw + 2), *(raw + 3));
    }

    void processMidiInputs(aap_buffer_t *audioBuffer) {

        sysex_offset = 0;
        auto midiInBuf = (AAPMidiBufferHeader*) audioBuffer->get_buffer(*audioBuffer, aap_midi2_in_port);
        auto umpStart = ((uint8_t*) midiInBuf) + sizeof(AAPMidiBufferHeader);
        int32_t positionInJRTimestamp = 0;

        // Process parameter changes first. The rest is handled only if the JUCE plugin accepts MIDI.
        CMIDI2_UMP_SEQUENCE_FOREACH(umpStart, midiInBuf->length, iter) {
            auto ump = (cmidi2_ump*) (void*) iter;

            uint8_t paramGroup, paramChannel, paramKey{0}, paramExtra{0};
            uint16_t paramId;
            float paramValue;
            if (readMidi2Parameter(&paramGroup, &paramChannel, &paramKey, &paramExtra, &paramId, &paramValue, ump)) {
                auto param = juce_processor->getParameterTree().getParameters(true)[paramId];
                if (param != nullptr) {
                    param->setValue(paramValue);
                    param->sendValueChangedMessageToListeners (paramValue);
                }
                else
                    // The processor is as traditional as not providing parameter tree. We have to resort to traditional API.
                    juce_processor->setParameter(paramId, paramValue);
                continue;
            }
        }

        if (!juce_processor->acceptsMidi())
            return;

        juce_midi_messages.clear();

        // FIXME: for complete support for AudioPlayHead::CurrentPositionInfo, we would also
        //   have to store bpm and timeSignature, based on MIDI messages.

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
                            MidiMessage(statusByte,
                                        cmidi2_ump_get_midi1_byte2(ump),
                                        cmidi2_ump_get_midi1_byte3(ump)),
                            sampleNumber);
                    break;
                case CMIDI2_MESSAGE_TYPE_MIDI_2_CHANNEL: {
                    switch (statusCode) {
                        case CMIDI2_STATUS_RPN: {
                            auto data = cmidi2_ump_get_midi2_rpn_data(ump);
                            juce_midi_messages.addEvent(
                                    MidiMessage(CMIDI2_STATUS_CC + channel,
                                                CMIDI2_CC_RPN_MSB,
                                                cmidi2_ump_get_midi2_rpn_msb(ump)),
                                    sampleNumber);
                            juce_midi_messages.addEvent(
                                    MidiMessage(CMIDI2_STATUS_CC + channel,
                                                CMIDI2_CC_RPN_LSB,
                                                cmidi2_ump_get_midi2_rpn_lsb(ump)),
                                    sampleNumber);
                            juce_midi_messages.addEvent(
                                    MidiMessage(CMIDI2_STATUS_CC + channel,
                                                CMIDI2_CC_DTE_MSB,
                                                (uint8_t) (data >> 25) & 0x7F),
                                    sampleNumber);
                            juce_midi_messages.addEvent(
                                    MidiMessage(CMIDI2_STATUS_CC + channel,
                                                CMIDI2_CC_DTE_MSB,
                                                (uint8_t) (data >> 18) & 0x7F),
                                    sampleNumber);
                        } break;
                        case CMIDI2_STATUS_NRPN: {
                            auto data = cmidi2_ump_get_midi2_nrpn_data(ump);
                            juce_midi_messages.addEvent(
                                    MidiMessage(CMIDI2_STATUS_CC + channel,
                                                CMIDI2_CC_NRPN_MSB,
                                                cmidi2_ump_get_midi2_nrpn_msb(ump)),
                                    sampleNumber);
                            juce_midi_messages.addEvent(
                                    MidiMessage(CMIDI2_STATUS_CC + channel,
                                                CMIDI2_CC_NRPN_LSB,
                                                cmidi2_ump_get_midi2_nrpn_lsb(ump)),
                                    sampleNumber);
                            juce_midi_messages.addEvent(
                                    MidiMessage(CMIDI2_STATUS_CC + channel,
                                                CMIDI2_CC_DTE_MSB,
                                                (uint8_t) (data >> 25) & 0x7F),
                                    sampleNumber);
                            juce_midi_messages.addEvent(
                                    MidiMessage(CMIDI2_STATUS_CC + channel,
                                                CMIDI2_CC_DTE_MSB,
                                                (uint8_t) (data >> 18) & 0x7F),
                                    sampleNumber);
                        } break;
                        case CMIDI2_STATUS_NOTE_OFF:
                        case CMIDI2_STATUS_NOTE_ON:
                            juce_midi_messages.addEvent(
                                    MidiMessage(statusCode,
                                                cmidi2_ump_get_midi2_note_note(ump),
                                                cmidi2_ump_get_midi2_note_velocity(ump) /
                                                0x200),
                                    sampleNumber);
                            break;
                        case CMIDI2_STATUS_PAF:
                            juce_midi_messages.addEvent(
                                    MidiMessage(statusCode,
                                                cmidi2_ump_get_midi2_note_note(ump),
                                                (uint8_t) (cmidi2_ump_get_midi2_paf_data(ump) /
                                                           0x2000000)),
                                    sampleNumber);
                            break;
                        case CMIDI2_STATUS_CC:
                            juce_midi_messages.addEvent(
                                    MidiMessage(statusCode,
                                                cmidi2_ump_get_midi2_cc_index(ump),
                                                (uint8_t) (cmidi2_ump_get_midi2_cc_data(ump) /
                                                           0x2000000)),
                                    sampleNumber);
                            break;
                        case CMIDI2_STATUS_PROGRAM:
                            if (cmidi2_ump_get_midi2_program_options(ump) &
                                CMIDI2_PROGRAM_CHANGE_OPTION_BANK_VALID) {
                                juce_midi_messages.addEvent(
                                        MidiMessage(CMIDI2_STATUS_CC + channel,
                                                    CMIDI2_CC_BANK_SELECT,
                                                    cmidi2_ump_get_midi2_program_bank_msb(ump)),
                                        sampleNumber);
                                juce_midi_messages.addEvent(
                                        MidiMessage(CMIDI2_STATUS_CC + channel,
                                                    CMIDI2_CC_BANK_SELECT_LSB,
                                                    cmidi2_ump_get_midi2_program_bank_lsb(ump)),
                                        sampleNumber);
                            }
                            juce_midi_messages.addEvent(
                                    MidiMessage(statusByte,
                                                cmidi2_ump_get_midi2_program_program(ump)),
                                    sampleNumber);
                            break;
                        case CMIDI2_STATUS_CAF:
                            juce_midi_messages.addEvent(
                                    MidiMessage(statusByte,
                                                (uint8_t) (cmidi2_ump_get_midi2_caf_data(ump) /
                                                           0x2000000)),
                                    sampleNumber);
                            break;
                        case CMIDI2_STATUS_PITCH_BEND: {
                            auto data = cmidi2_ump_get_midi2_pitch_bend_data(ump);
                            juce_midi_messages.addEvent(
                                    MidiMessage(statusByte,
                                                (uint8_t) (data >> 18) & 0x7F,
                                                (uint8_t) (data >> 25) & 0x7F),
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

    void processMidiOutputs(aap_buffer_t* buffer) {
        // This part is not really verified... we need some JUCE plugin that generates some outputs.
        void *dst = buffer->get_buffer(*buffer, aap_midi2_out_port);
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

    void resetJuceChannels(aap_buffer_t *audioBuffer) {
        int nOut = juce_processor->getMainBusNumOutputChannels();
        int nIn = juce_processor->getMainBusNumInputChannels();

        for (int i = 0; i < audioBuffer->num_ports(*audioBuffer); i++) {
            // Assign JUCE out channel buffer ONLY IF it is not assigned for inputs.
            // To achieve that, first fill output buffer pointers, then overwrite by input buffer pointers.
            auto iterOut = aap_to_juce_portmap_out.find(i);
            if (iterOut != aap_to_juce_portmap_out.end())
                juce_channels[iterOut->second] = (float*) audioBuffer->get_buffer(*audioBuffer, i);
            auto iterIn = aap_to_juce_portmap_in.find(i);
            if (iterIn != aap_to_juce_portmap_in.end())
                juce_channels[iterIn->second] = (float*) audioBuffer->get_buffer(*audioBuffer, i);
        }

        juce_audio_buffer = juce::AudioBuffer<float>(juce_channels, jmax(nIn, nOut), audioBuffer->num_frames(*audioBuffer));
    }

    const char *AAP_JUCE_TRACE_SECTION_NAME = "aap-juce_process";
    const char *AAP_JUCE_DSP_TRACE_SECTION_NAME = "aap-juce_process_dsp";

    void process(aap_buffer_t *audioBuffer, int32_t frameCount, int64_t timeoutInNanoseconds) {
#if ANDROID
        struct timespec tsBegin, tsEnd;
        struct timespec tsDspBegin, tsDspEnd;
        if (ATrace_isEnabled()) {
            clock_gettime(CLOCK_REALTIME, &tsBegin);
            ATrace_beginSection(AAP_JUCE_TRACE_SECTION_NAME);
        }
#endif
        resetJuceChannels(audioBuffer);

        if (aap_midi2_in_port >= 0)
            processMidiInputs(audioBuffer);

        // process data by the JUCE plugin
#if ANDROID
        if (ATrace_isEnabled()) {
            clock_gettime(CLOCK_REALTIME, &tsDspBegin);
            ATrace_beginSection(AAP_JUCE_DSP_TRACE_SECTION_NAME);
        }
#endif

        juce_processor->processBlock(juce_audio_buffer, juce_midi_messages);

#if ANDROID
        if (ATrace_isEnabled()) {
            clock_gettime(CLOCK_REALTIME, &tsDspEnd);
            ATrace_setCounter(AAP_JUCE_DSP_TRACE_SECTION_NAME,
                              (tsDspEnd.tv_sec - tsDspBegin.tv_sec) * 1000000000 + tsDspEnd.tv_nsec - tsDspBegin.tv_nsec);
            ATrace_endSection();
        }
#endif

#if JUCEAAP_HAVE_AUDIO_PLAYHEAD_NEW_POSITION_INFO
        play_head_position.setTimeInSamples(*play_head_position.getTimeInSamples() + audioBuffer->num_frames(*audioBuffer));
        auto thisTimeInSeconds = 1.0 * audioBuffer->num_frames(*audioBuffer) / sample_rate;
        play_head_position.setPpqPosition(*play_head_position.getPpqPosition() + *play_head_position.getBpm() / 60 * 4 * thisTimeInSeconds);
#else
        auto numFrames = audioBuffer->num_frames(*audioBuffer);
        if (frameCount > numFrames) {
            aap::a_log_f(AAP_LOG_LEVEL_ERROR, AAP_JUCE_TAG, "frameCount is bigger than numFrames from aap_buffer_t.");
            frameCount = numFrames;
        }
        play_head_position.timeInSamples += frameCount;
        auto thisTimeInSeconds = 1.0 * frameCount / sample_rate;
        play_head_position.timeInSeconds += thisTimeInSeconds;
        play_head_position.ppqPosition += play_head_position.bpm / 60 * 4 * thisTimeInSeconds;
#endif

        // There isn't anything we can send to AAP MIDI2 output port if it does not exist, so far.
        if (juce_processor->producesMidi())
            processMidiOutputs(buffer);

        int numAudioIns = juce_processor->getMainBusNumInputChannels();
        int numAudioOuts = juce_processor->getMainBusNumOutputChannels();
        int numCopied = numAudioIns < numAudioOuts ? numAudioIns : numAudioOuts;
        for (int i = 0; i < numCopied; i++) {
            auto aapPortIndex = portmap_juce_to_aap_out[i];
            memcpy(audioBuffer->get_buffer(*audioBuffer, aapPortIndex), juce_channels[i], audioBuffer->num_frames(*audioBuffer) * sizeof(float));
        }

#if ANDROID
        if (ATrace_isEnabled()) {
            clock_gettime(CLOCK_REALTIME, &tsEnd);
            ATrace_setCounter(AAP_JUCE_TRACE_SECTION_NAME,
                              (tsEnd.tv_sec - tsBegin.tv_sec) * 1000000000 + tsEnd.tv_nsec - tsBegin.tv_nsec);
            ATrace_endSection();
        }
#endif
    }

    void onDispose() {
        juce_channels.free();
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

    void getPreset(int32_t index, aap_preset_t* preset) {
        preset->id = index;
        juce_processor->getProgramName(index).copyToUTF8(preset->name, AAP_PRESETS_EXTENSION_MAX_NAME_LENGTH);
    }

    int32_t getPresetIndex() {
        return juce_processor->getCurrentProgram();
    }

    void setPresetIndex(int32_t index) {
        juce_processor->setCurrentProgram(index);
    }

    int32_t getAAPParameterCount() { return aapParams.size(); }
    aap_parameter_info_t getAAPParameterInfo(int index) { return *aapParams[index]; }
    AudioProcessorParameter* findJUCEParameter(int id) {
        for (auto p : juce_processor->getParameterTree().getParameters(true))
            if (p->getParameterIndex() == id)
                return p;
        return nullptr;
    }
    double getAAPParameterProperty(int32_t parameterId, int32_t propertyId) {
        for (auto info: aapParams) {
            if (info->stable_id == parameterId) {
                switch (propertyId) {
                    case AAP_PARAMETER_PROPERTY_MIN_VALUE:
                        return info->min_value;
                    case AAP_PARAMETER_PROPERTY_MAX_VALUE:
                        return info->max_value;
                    case AAP_PARAMETER_PROPERTY_DEFAULT_VALUE:
                        return info->default_value;
                    case AAP_PARAMETER_PROPERTY_IS_DISCRETE: {
                        auto p = findJUCEParameter(parameterId);
                        return p != nullptr && p->isDiscrete();
                    }
                    // JUCE does not have it (yet?)
                    case AAP_PARAMETER_PROPERTY_PRIORITY:
                        return 0;
                }
            }
        }
        return 0;
    }
    int32_t getAAPEnumerationCount(int32_t parameterId) {
        auto p = findJUCEParameter(parameterId);
        return p != nullptr ? p->getAllValueStrings().size() : 0;
    }
    aap_parameter_enum_t getAAPEnumeration(int32_t parameterId, int32_t enumIndex) {
        int32_t baseIndex = aapParamIdToEnumIndex[parameterId];
        return *aapEnums[baseIndex + enumIndex];
    }

    AudioProcessor* getAudioProcessor() { return juce_processor; }
};

// AAP plugin API ----------------------------------------------------------------------

JuceAAPWrapper *getWrapper(AndroidAudioPlugin *plugin) {
    return (JuceAAPWrapper *) plugin->plugin_specific;
}

void juceaap_prepare(
        AndroidAudioPlugin *plugin,
        aap_buffer_t *audioBuffer) {
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
        aap_buffer_t *audioBuffer,
        int32_t frameCount,
        int64_t timeoutInNanoseconds) {
    getWrapper(plugin)->process(audioBuffer, frameCount, timeoutInNanoseconds);
}

// Extensions ----------------------------------------------------------------------

// state extension

size_t juceaap_get_state_size(aap_state_extension_t* ext, AndroidAudioPlugin* plugin) {
    return getWrapper(plugin)->getStateSize();
}

void juceaap_get_state(aap_state_extension_t* ext, AndroidAudioPlugin* plugin, aap_state_t *result) {
    getWrapper(plugin)->getState(result);
}

void juceaap_set_state(aap_state_extension_t* ext, AndroidAudioPlugin* plugin, aap_state_t *input) {
    getWrapper(plugin)->setState(input);
}

// presets extension

int32_t juce_aap_wrapper_get_preset_count(aap_presets_extension_t* ext, AndroidAudioPlugin* plugin) {
    auto wrapper = (JuceAAPWrapper*) plugin->plugin_specific;
    return wrapper->getPresetCount();
}

void juce_aap_wrapper_get_preset(aap_presets_extension_t* ext, AndroidAudioPlugin* plugin, int32_t index, aap_preset_t *preset, aapxs_completion_callback callback, void* callbackContext) {
    auto wrapper = (JuceAAPWrapper*) plugin->plugin_specific;
    wrapper->getPreset(index, preset);
    if (callback)
        callback(callbackContext, plugin);
}

int32_t juce_aap_wrapper_get_preset_index(aap_presets_extension_t* ext, AndroidAudioPlugin* plugin) {
    auto wrapper = (JuceAAPWrapper*) plugin->plugin_specific;
    return wrapper->getPresetIndex();
}

void juce_aap_wrapper_set_preset_index(aap_presets_extension_t* ext, AndroidAudioPlugin* plugin, int32_t index) {
    auto wrapper = (JuceAAPWrapper*) plugin->plugin_specific;
    wrapper->setPresetIndex(index);
}

// parameters extension

int32_t juceaap_get_parameter_count(aap_parameters_extension_t* ext, AndroidAudioPlugin* plugin) {
    auto wrapper = (JuceAAPWrapper*) plugin->plugin_specific;
    return wrapper->getAAPParameterCount();
}

aap_parameter_info_t juceaap_get_parameter(aap_parameters_extension_t* ext, AndroidAudioPlugin* plugin, int32_t index) {
    auto wrapper = (JuceAAPWrapper*) plugin->plugin_specific;
    return wrapper->getAAPParameterInfo(index);
}

double juceaap_get_parameter_property(aap_parameters_extension_t* ext, AndroidAudioPlugin* plugin, int32_t parameterId, int32_t propertyId) {
    auto wrapper = (JuceAAPWrapper*) plugin->plugin_specific;
    return wrapper->getAAPParameterProperty(parameterId, propertyId);
}

int32_t juceaap_get_enumeration_count(aap_parameters_extension_t* ext, AndroidAudioPlugin* plugin, int32_t parameterId) {
    auto wrapper = (JuceAAPWrapper*) plugin->plugin_specific;
    return wrapper->getAAPEnumerationCount(parameterId);
}

aap_parameter_enum_t juceaap_get_enumeration(aap_parameters_extension_t* ext, AndroidAudioPlugin* plugin, int32_t parameterId, int32_t enumIndex) {
    auto wrapper = (JuceAAPWrapper*) plugin->plugin_specific;
    return wrapper->getAAPEnumeration(parameterId, enumIndex);
}

aap_plugin_info_t juceaap_get_plugin_info(AndroidAudioPlugin* plugin) {
    auto wrapper = (JuceAAPWrapper*) plugin->plugin_specific;
    auto* host = &wrapper->getHost();
    auto hostExt = (aap_host_plugin_info_extension_t*) host->get_extension(host, AAP_PLUGIN_INFO_EXTENSION_URI);
    return hostExt->get(hostExt, host, wrapper->getPluginId());
}

aap_parameters_extension_t parameters_ext{nullptr,
                                          juceaap_get_parameter_count,
                                          juceaap_get_parameter,
                                          juceaap_get_parameter_property,
                                          juceaap_get_enumeration_count,
                                          juceaap_get_enumeration
                                          };

std::map<AndroidAudioPlugin *, std::unique_ptr<aap_state_extension_t>> state_ext_map{};
std::map<AndroidAudioPlugin *, std::unique_ptr<aap_presets_extension_t>> presets_ext_map{};

void* juceaap_get_extension(AndroidAudioPlugin *plugin, const char *uri) {
    if (strcmp(uri, AAP_PARAMETERS_EXTENSION_URI) == 0) {
        return &parameters_ext;
    }
    if (strcmp(uri, AAP_STATE_EXTENSION_URI) == 0) {
        if (!state_ext_map[plugin]) {
            aap_state_extension_t newInstance{nullptr,
                                              juceaap_get_state_size,
                                              juceaap_get_state,
                                              juceaap_set_state};

            state_ext_map[plugin] = std::make_unique<aap_state_extension_t>(newInstance);
        }
        return state_ext_map[plugin].get();
    }
    if (strcmp(uri, AAP_PRESETS_EXTENSION_URI) == 0) {
        if (!presets_ext_map[plugin]) {
            aap_presets_extension_t newInstance{nullptr,
                                                juce_aap_wrapper_get_preset_count,
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
    ret->get_plugin_info = juceaap_get_plugin_info;

    return ret;
}

void juceaap_release(
        AndroidAudioPluginFactory *pluginFactory,
        AndroidAudioPlugin *instance) {
    auto ctx = getWrapper(instance);
    if (ctx != nullptr) {
        ctx->onDispose();
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

JNIEXPORT extern "C" AndroidAudioPluginFactory *GetJuceAAPFactory() {
    return &juceaap_factory;
}

extern "C"
JNIEXPORT void JNICALL
Java_org_androidaudioplugin_juce_JuceAudioProcessorEditorView_addAndroidComponentPeerViewTo(
        JNIEnv *env, jclass clazz, jlong pluginServiceNative, jstring plugin_id, jint instanceId,
        jobject parentLinearLayout) {
    juce_juceEventsAndroidStartApp();
    auto service = (aap::PluginService *) pluginServiceNative;
    auto instance = service->getLocalInstance(instanceId);
    auto plugin = instance->getPlugin();
    auto wrapper = (JuceAAPWrapper*) plugin->plugin_specific;
    wrapper->addAndroidView(parentLinearLayout);
}