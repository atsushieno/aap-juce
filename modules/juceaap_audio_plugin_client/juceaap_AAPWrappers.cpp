
#include <ctime>
#include <juce_audio_processors/juce_audio_processors.h>
#include "aap/android-audio-plugin.h"
#include "aap/unstable/logging.h"
#include "aap/ext/presets.h"
#include "aap/ext/state.h"

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
//  JUCE parameters (flattened) 0..p-1 -> AAP ports 0..p-1
// 	JUCE AudioBuffer 0..nOut-1 -> AAP output ports p..p+nOut-1
//  JUCE AudioBuffer nOut..nIn+nOut-1 -> AAP input ports p+nOut..p+nIn+nOut-1
//  IF exists JUCE MIDI input buffer -> AAP MIDI input port p+nIn+nOut
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

    juce::AudioPlayHead::CurrentPositionInfo play_head_position;

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

    bool getCurrentPosition(juce::AudioPlayHead::CurrentPositionInfo &result) override {
        result = play_head_position;
        return true;
    }

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

        play_head_position.resetToDefault();
        play_head_position.bpm = 120;

        juce_processor->setPlayConfigDetails(
                getNumberOfChannelsOfBus(juce_processor->getBus(true, 0)),
                getNumberOfChannelsOfBus(juce_processor->getBus(false, 0)),
                sample_rate, buffer->num_frames);
        juce_processor->setPlayHead(this);

        juce_processor->prepareToPlay(sample_rate, buffer->num_frames);
    }

    void activate() {
        juce_channels.calloc(juce_processor->getMainBusNumInputChannels() + juce_processor->getMainBusNumOutputChannels());
        play_head_position.isPlaying = true;
    }

    void deactivate() {
        play_head_position.isPlaying = false;
        juce_channels.free();
    }

    int32_t current_bpm = 120; // FIXME: provide way to adjust it
    int32_t default_time_division = 192;

    std::vector<float> cached_parameter_values;

    void process(AndroidAudioPluginBuffer *audioBuffer, long timeoutInNanoseconds) {
#if JUCEAAP_LOG_PERF
        struct timespec timeSpecBegin, timeSpecEnd;
        struct timespec procTimeSpecBegin, procTimeSpecEnd;
        clock_gettime(CLOCK_REALTIME, &timeSpecBegin);
#endif

        int nPara = juce_processor->getParameters().size();
        // For JUCE plugins, we can take at best one parameter value to be processed.
        for (int i = 0; i < nPara; i++) {
            float v = ((float *) audioBuffer->buffers[i])[0];
            if (cached_parameter_values[i] != v) {
                auto param = juce_processor->getParameterTree().getParameters(true)[i];
                param->setValue(v);
                cached_parameter_values[i] = v;
                param->sendValueChangedMessageToListeners (v);
            }
        }

        int nOut = juce_processor->getMainBusNumOutputChannels();
        int nBuf = juce_processor->getMainBusNumInputChannels() +
                   juce_processor->getMainBusNumOutputChannels();

        // FIXME: replace them with fixed pointers at prepare()
        for (int i = 0; i < nOut; i++)
            juce_channels[i] = (float*) audioBuffer->buffers[i + nPara];
        for (int i = nOut; i < nBuf; i++)
            juce_channels[i] = (float*) audioBuffer->buffers[i + nPara];

        /*
        for (int i = 0; i < nOut; i++)
            memset((void *) juce_buffer.getWritePointer(i), 0,
                   sizeof(float) * audioBuffer->num_frames);
        for (int i = nOut; i < nBuf; i++)
            memcpy((void *) juce_buffer.getWritePointer(i - nOut), audioBuffer->buffers[i + nPara],
                   sizeof(float) * audioBuffer->num_frames);
        */

        int rawTimeDivision = default_time_division;

        if (juce_processor->acceptsMidi()) {
            // FIXME: for complete support for AudioPlayHead::CurrentPositionInfo, we would also
            //   have to store bpm and timeSignature, based on MIDI messages.

            juce_midi_messages.clear();
            void *src = audioBuffer->buffers[nPara + nBuf];
            uint8_t *csrc = (uint8_t *) src;
            int *isrc = (int *) src;
            int32_t timeDivision = rawTimeDivision = isrc[0];
            timeDivision = timeDivision < 0 ? -timeDivision : timeDivision & 0xFF;
            int32_t srcEnd = isrc[1] + 8;
            int32_t srcN = 8;
            uint8_t running_status = 0;
            uint64_t ticks = 0;
            while (srcN < srcEnd) {
                long timecode = 0;
                int digits = 0;
                while (csrc[srcN] >= 0x80 && srcN < srcEnd) // variable length
                    timecode += ((csrc[srcN++] - 0x80) << (7 * digits++));
                if (srcN == srcEnd)
                    break; // invalid data
                timecode += (csrc[srcN++] << (7 * digits));

                int32_t srcEventStart = srcN;
                uint8_t statusByte = csrc[srcN] >= 0x80 ? csrc[srcN] : running_status;
                running_status = statusByte;
                uint8_t eventType = statusByte & 0xF0;
                uint32_t midiEventSize = 3;
                int sysexPos = srcN;
                double timestamp;
                switch (eventType) {
                    case 0xF0:
                        midiEventSize = 2; // F0 + F7
                        while (csrc[sysexPos++] != 0xF7 && sysexPos < srcEnd)
                            midiEventSize++;
                        break;
                    case 0xC0:
                    case 0xD0:
                    case 0xF1:
                    case 0xF3:
                    case 0xF9:
                        midiEventSize = 2;
                        break;
                    case 0xF6:
                    case 0xF7:
                        midiEventSize = 1;
                        break;
                    default:
                        if (eventType > 0xF8)
                            midiEventSize = 1;
                        break;
                }

                // JUCE does not have appropriate semantics on how time code is passed.
                // We consume AAP MIDI message timestamps accordingly, convert them into position in current frame,
                // and for MIDI output we just pass them through.
                if (rawTimeDivision < 0) {
                    // Hour:Min:Sec:Frame. 1sec = `timeDivision` * frames.
                    ticks += (
                            (((timecode & 0xFF000000) >> 24) * 60 + ((timecode & 0xFF0000) >> 16)) *
                            60 + ((timecode & 0xFF00) >> 8) * timeDivision + (timecode & 0xFF));
                    timestamp = 1.0 * ticks / timeDivision * sample_rate;
                } else {
                    ticks += timecode;
                    timestamp += 60.0f / current_bpm * ticks / timeDivision;
                }
                MidiMessage m;
                bool skip = false;
                switch (midiEventSize) {
                    case 1:
                        skip = true; // there is no way to create MidiMessage for F6 (tune request) and F7 (end of sysex)
                        break;
                    case 2:
                        m = MidiMessage{csrc[srcEventStart], csrc[srcEventStart + 1]};
                        break;
                    case 3:
                        m = MidiMessage{csrc[srcEventStart], csrc[srcEventStart + 1],
                                        csrc[srcEventStart + 2]};
                        break;
                    default: // sysex etc.
                        m = MidiMessage{csrc + (int32_t) srcEventStart, (int32_t) midiEventSize};
                        break;
                }
                if (!skip) {
                    m.setChannel((statusByte & 0x0F) + 1); // they accept 1..16, not 0..15...
                    juce_midi_messages.addEvent(m, timestamp);
                }
                srcN += midiEventSize;
            }
        }

        // process data by the JUCE plugin
#if JUCEAAP_LOG_PERF
        clock_gettime(CLOCK_REALTIME, &procTimeSpecBegin);
#endif
        juce::AudioSampleBuffer juceAudioBuffer(juce_channels, jmax(nBuf - nOut, nOut), audioBuffer->num_frames);

        // Should this be required? I thought juceAudioBuffer is assigned all those input pointers...
        for (int i = nOut; i < nBuf; i++)
            memcpy((void *) juceAudioBuffer.getWritePointer(i - nOut), audioBuffer->buffers[i + nPara],
                   sizeof(float) * audioBuffer->num_frames);

        juce_processor->processBlock(juceAudioBuffer, juce_midi_messages);
#if JUCEAAP_LOG_PERF
        clock_gettime(CLOCK_REALTIME, &procTimeSpecEnd);
        long procTimeDiff = (procTimeSpecEnd.tv_sec - procTimeSpecBegin.tv_sec) * 1000000000 + procTimeSpecEnd.tv_nsec - procTimeSpecBegin.tv_nsec;
#endif
        play_head_position.timeInSamples += audioBuffer->num_frames;
        auto thisTimeInSeconds = 1.0 * audioBuffer->num_frames / sample_rate;
        play_head_position.timeInSeconds += thisTimeInSeconds;
        play_head_position.ppqPosition += play_head_position.bpm / 60 * 4 * thisTimeInSeconds;

        /*
        for (int i = 0; i < nOut; i++)
            memcpy(audioBuffer->buffers[i + nPara], (void *) juce_buffer.getReadPointer(i),
                   sizeof(float) * audioBuffer->num_frames);
        */

        if (juce_processor->producesMidi()) {
            int32_t bufIndex = nPara + nBuf + (juce_processor->acceptsMidi() ? 1 : 0);
            void *dst = buffer->buffers[bufIndex];
            uint8_t *cdst = (uint8_t *) dst;
            int *idst = (int *) dst;
            MidiBuffer::Iterator iterator{juce_midi_messages};
            const uint8_t *data;
            int32_t eventSize, eventPos, dstBufSize = 0;
            while (iterator.getNextEvent(data, eventSize, eventPos)) {
                memcpy(cdst + 8 + dstBufSize, data + eventPos, eventSize);
                dstBufSize += eventSize;
            }
            idst[0] = rawTimeDivision;
            idst[1] = dstBufSize;
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
        auto childXml = parent->createNewChildElement("ports");
        childXml->setAttribute("name", group->getName());
        for (auto childPara : *group)
            generate_xml_parameter_node(childXml, childPara);
    } else {
        auto para = node->getParameter();
        auto childXml = parent->createNewChildElement("port");
        childXml->setAttribute("name", para->getName(1024));
        childXml->setAttribute("direction", "input"); // JUCE does not support output parameter.
        if (!std::isnormal(para->getDefaultValue()))
            childXml->setAttribute("pp:default", para->getDefaultValue());
        auto ranged = dynamic_cast<RangedAudioParameter *>(para);
        if (ranged) {
            auto range = ranged->getNormalisableRange();
            if (std::isnormal(range.start))
                childXml->setAttribute("pp:minimum", range.start);
            if (std::isnormal(range.end))
                childXml->setAttribute("pp:maximum", range.end);
            if (std::isnormal(ranged->getDefaultValue()))
                childXml->setAttribute("pp:default", range.convertTo0to1(ranged->getDefaultValue()));
        }
        else
            childXml->setAttribute("pp:default", para->getDefaultValue());
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
    pluginsElement->setAttribute("xmlns:pp", "urn:org.androidaudioplugin.port");
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
    auto topLevelPortsElement = pluginElement->createNewChildElement("ports");
    for (auto node : tree) {
        generate_xml_parameter_node(topLevelPortsElement, node);
    }

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
        portXml->setAttribute("content", "midi");
        portXml->setAttribute("name", "MIDI input");
    }
    if (filter->producesMidi()) {
        auto portXml = topLevelPortsElement->createNewChildElement("port");
        portXml->setAttribute("direction", "output");
        portXml->setAttribute("content", "midi");
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
