#include "juceaap_audio_plugin_format.h"
#include <sys/mman.h>

#include <libgen.h>
#include <unistd.h>
#include "cmidi2.h"
#include <aap/ext/parameters.h>
#if ANDROID
#include <android/sharedmem.h>
#else
namespace aap {
extern void aap_parse_plugin_descriptor_into(const char* pluginPackageName, const char* pluginLocalName, const char* xmlfile, std::vector<PluginInformation*>& plugins);
}
#endif

using namespace aap;
using namespace juce;

#define AAP_JUCE_LOG_TAG "AAP-JUCE"

namespace juceaap {

double AndroidAudioPluginInstance::getTailLengthSeconds() const {
    return native->getTailTimeInMilliseconds() / 1000.0;
}

static void fillPluginDescriptionFromNativeCommon(PluginDescription &description,
                                            const aap::PluginInformation &src) {
    description.descriptiveName = src.getDisplayName();
    description.name = src.getDisplayName();
    description.pluginFormatName = "AAP";

    description.category.clear();
    description.category += juce::String(src.getPrimaryCategory());

    description.manufacturerName = src.getDeveloperName();

    description.version = src.getVersion();
    // JUCE plugin identifier is "PluginID" in AAP (not "identifier_string").
    description.fileOrIdentifier = src.getPluginID();
    // So far this is as hacky as AudioUnit implementation.
    description.lastFileModTime = Time();
    description.lastInfoUpdateTime = Time(src.getLastInfoUpdateTime());
#if JUCEAAP_USE_UNIQUE_ID
    description.uniqueId = String(src.getPluginID()).hashCode();
#else
    description.uid = String(src.getPluginID()).hashCode();
#endif
    description.isInstrument = src.isInstrument();
    description.hasSharedContainer = false; //src.hasSharedContainer();
}

static void fillPluginDescriptionFromNativeDescription(PluginDescription &description,
                                            const aap::PluginInformation &src) {
    fillPluginDescriptionFromNativeCommon(description, src);

    for (int i = 0; i < src.getNumDeclaredPorts(); i++) {
        auto port = src.getDeclaredPort(i);
        auto dir = port->getPortDirection();
        if (dir == AAP_PORT_DIRECTION_INPUT)
            description.numInputChannels++;
        else if (dir == AAP_PORT_DIRECTION_OUTPUT)
            description.numOutputChannels++;
    }
}

static void fillPluginDescriptionFromNativeInstance(PluginDescription &description, aap::PluginInstance *native) {
    const auto src = *native->getPluginInformation();
    fillPluginDescriptionFromNativeCommon(description, src);

    for (int i = 0; i < native->getNumPorts(); i++) {
        auto port = native->getPort(i);
        auto dir = port->getPortDirection();
        if (dir == AAP_PORT_DIRECTION_INPUT)
            description.numInputChannels++;
        else if (dir == AAP_PORT_DIRECTION_OUTPUT)
            description.numOutputChannels++;
    }
}

static void* handleSysex7(uint64_t data, void* context) {
    auto mbh = (AAPMidiBufferHeader*) context;
    auto p = (uint32_t*) (void*) ((uint8_t*) mbh) + mbh->length + sizeof (mbh);
    p[0] = data >> 32;
    p[1] = data & 0xFFFFFFFF;
    return nullptr;
}

void AndroidAudioPluginInstance::preProcessBuffers(AudioBuffer<float> &audioBuffer,
                                                        MidiBuffer &midiMessages) {
    // FIXME: RT lock

    // FIXME: there is some glitch between how JUCE AudioBuffer assigns a channel for each buffer item
    //  and how AAP expects them.
    int juceInputsAssigned = 0;
    auto *buffer = native->getAudioPluginBuffer();

    for (int portIndex = 0, numPorts = native->getNumPorts(); portIndex < numPorts; portIndex++) {
        auto port = native->getPort(portIndex);
        if (port->getPortDirection() != AAP_PORT_DIRECTION_INPUT)
            continue;
        if (port->getContentType() == AAP_CONTENT_TYPE_AUDIO &&
            juceInputsAssigned < audioBuffer.getNumChannels())
            memcpy(buffer->get_buffer(*buffer, portIndex), (void *) audioBuffer.getReadPointer(juceInputsAssigned++),
                   audioBuffer.getNumSamples() * sizeof(float));
    }

    if (aap_midi_in_port >= 0) { // it should be usually true as it supports all parameter changes.
        auto mbh = (AAPMidiBufferHeader*) buffer->get_buffer(*buffer, aap_midi_in_port);

        // Convert MidiBuffer into MIDI 2.0 UMP stream on the AAP port
        MidiMessage msg{};
        int pos{0};
        const double oneTick = 1 / 31250.0; // sec
        double secondsPerFrameJUCE = 1.0 / sample_rate; // sec
        MidiBuffer::Iterator iter{midiMessages};
        size_t dstIntIndex = mbh->length;
        uint32_t *dstI = (uint32_t *) (void*) (mbh + 1);

        while (iter.getNextEvent(msg, pos)) {
            // generate UMP Timestamps only when message has non-zero timestamp.
            double timestamp = msg.getTimeStamp();
            if (timestamp != 0) {
                double timestampSeconds = timestamp * secondsPerFrameJUCE;
                int32_t timestampTicks = (int32_t) (timestampSeconds / oneTick);
                do {
                    *(dstI + dstIntIndex) = cmidi2_ump_jr_timestamp_direct(
                            0, (uint32_t)(timestampTicks) % 62500); // 2 sec.
                    timestampTicks -= 62500;
                    dstIntIndex++;
                } while (timestampTicks > 0);
            }
            // then generate UMP for the status byte
            if (msg.isSysEx()) {
                cmidi2_ump_sysex7_process(0, (void *) msg.getRawData(), handleSysex7, mbh);
            } else if (msg.isMetaEvent()) {
                // FIXME: we will transmit META events into some UMP which seems coming to the next UMP spec.
                //  https://www.midi.org/midi-articles/details-about-midi-2-0-midi-ci-profiles-and-property-exchange
            } else {
                auto data = msg.getRawData();
                *(dstI + dstIntIndex) = (uint32_t) cmidi2_ump_midi1_message(
                        0, data[0], (uint8_t) msg.getChannel(), data[1], data[2]);
                dstIntIndex++;
            }
        }
        mbh->length = dstIntIndex * sizeof(uint32_t);
        mbh->time_options = 0;
        for (int i = 0; i < 6; i++)
            mbh->reserved[i] = 0;
    }

    // FIXME: RT unlock
}

void AndroidAudioPluginInstance::postProcessBuffers(AudioBuffer<float> &audioBuffer, MidiBuffer &midiMessages) {
    // FIXME: RT lock

    int n = native->getNumPorts();
    auto *buffer = native->getAudioPluginBuffer();

    int juceOutputsAssigned = 0;
    for (int i = 0; i < n; i++) {
        auto port = native->getPort(i);
        if (port->getPortDirection() != AAP_PORT_DIRECTION_OUTPUT)
            continue;
        if (port->getContentType() == AAP_CONTENT_TYPE_AUDIO && juceOutputsAssigned < audioBuffer.getNumChannels()) {
            memcpy((void *) audioBuffer.getWritePointer(juceOutputsAssigned++), buffer->get_buffer(*buffer, i), audioBuffer.getNumSamples() * sizeof(float));
        }
    }

    if (aap_midi_out_port >= 0) {
        auto mbh = (AAPMidiBufferHeader*) buffer->get_buffer(*buffer, aap_midi_out_port);
        mbh->length = 0;
        auto ump = (cmidi2_ump*) (mbh + 1);
        cmidi2_midi_conversion_context context;
        cmidi2_midi_conversion_context_initialize(&context);
        context.ump = ump;
        context.ump_num_bytes = mbh->length;
        context.midi1 = midi_output_store;
        // FIXME: in the future we support sample accurate outputs
        context.skip_delta_time = false;
        cmidi2_convert_ump_to_midi1(&context);
        for (uint32_t midi1At = 0; midi1At < context.midi1_num_bytes; ) {
            size_t size = cmidi2_midi1_get_message_size(midi_output_store + midi1At, context.midi1_num_bytes - midi1At);
            midiMessages.addEvent(midi_output_store + midi1At, (int) size, 0);
            midi1At += size;
        }
    }

    // clean up room for the next JUCE parameter changes cycle
    if (aap_midi_in_port >= 0)
        ((AAPMidiBufferHeader *) buffer->get_buffer(*buffer, aap_midi_in_port))->length = 0;

    // FIXME: RT unlock
}

juce::AudioProcessor::BusesProperties AndroidAudioPluginInstance::createJuceBuses(aap::PluginInstance* native) {
    juce::AudioProcessor::BusesProperties ret{};
    int32_t numAudioIns = 0, numAudioOuts = 0;
    for (int32_t i = 0, n = native->getNumPorts(); i < n; i++) {
        auto port = native->getPort(i);
        if (port->getContentType() != AAP_CONTENT_TYPE_AUDIO)
            continue;
        if (port->getPortDirection() == AAP_PORT_DIRECTION_INPUT)
            numAudioIns++;
        else
            numAudioOuts++;
    }
    switch (numAudioIns) {
        case 0:
            break;
        case 1:
            ret.addBus(true, "Mono Input", juce::AudioChannelSet::mono());
            break;
        stereo_input:
        case 2:
            ret.addBus(true, "Stereo Input", juce::AudioChannelSet::stereo());
            break;
        default:
            aap::a_log_f(AAP_LOG_LEVEL_WARN, AAP_JUCE_LOG_TAG,
                         "TODO: the plugin has more audio input channels than stereo, which is not implemented in aap-juce. Treating it as stereo.");
            goto stereo_input;
    }
    switch (numAudioOuts) {
        case 0: break;
        case 1: ret.addBus(false, "Mono Output", juce::AudioChannelSet::mono()); break;
        stereo_output:
        case 2: ret.addBus(false, "Stereo Output", juce::AudioChannelSet::stereo()); break;
        default:
            aap::a_log_f(AAP_LOG_LEVEL_WARN, AAP_JUCE_LOG_TAG,
                         "TODO: the plugin has more audio output channels than stereo, which is not implemented in aap-juce. Treating it as stereo.");
            goto stereo_output;
    }
    return ret;
}

AndroidAudioPluginInstance::AndroidAudioPluginInstance(aap::PluginInstance* nativePlugin)
        : juce::AudioPluginInstance(createJuceBuses(nativePlugin)), native(nativePlugin),
          sample_rate(-1) {

    // It is super awkward, but plugin parameter definition does not exist in juce::PluginInformation.
    // Only AudioProcessor.addParameter() works. So we handle them here.
    for (int i = 0; i < nativePlugin->getNumParameters(); i++) {
        auto para = nativePlugin->getParameter(i);
#if JUCEAAP_HOSTED_PARAMETER
        addHostedParameter(std::unique_ptr<AndroidAudioPluginParameter>(new AndroidAudioPluginParameter(i, this, para)));
#else
        addParameter(new AndroidAudioPluginParameter(i, this, para));
#endif
    }
}

void AndroidAudioPluginParameter::valueChanged(float newValue) {
    instance->parameterValueChanged(this, newValue);
}

bool AndroidAudioPluginInstance::parameterValueChanged(AndroidAudioPluginParameter* parameter, float newValue)
{
    int i = parameter->getAAPParameterId();

    if(getNumParameters() <= i)
        return false; // too early to reach here.

    // In AAP V2 protocol, parameters are sent over MIDI2 port as UMP.
    auto *buffer = native->getAudioPluginBuffer();
    if (aap_midi_in_port < 0)
        return false; // there is no port that accepts parameter changes
    if ((uint32_t) aap_midi_in_port >= buffer->num_ports(*buffer))
        return false; // buffer does not seem prepared yet

    int32_t paramId = parameter->getAAPParameterId();
    if (paramId > UINT16_MAX) {
        aap::a_log_f(AAP_LOG_LEVEL_ERROR, AAP_JUCE_LOG_TAG,
                     "Unsupported attempt to set parameter index %d which is > %d", paramId,
                     UINT16_MAX);
        return false;
    }

    // FIXME: RT lock

    auto mbh = (AAPMidiBufferHeader*) buffer->get_buffer(*buffer, aap_midi_in_port);
    uint32_t* dst = (uint32_t*) (void*) ((uint8_t*) buffer->get_buffer(*buffer, aap_midi_in_port) + sizeof(AAPMidiBufferHeader) + mbh->length);
    mbh->length += 16;
    aapMidi2ParameterSysex8(dst, dst + 1, dst + 2, dst + 3, 0, 0, 0, 0, (uint16_t) paramId, newValue);

    // FIXME: RT unlock

    return true;
}

void
AndroidAudioPluginInstance::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) {
    sample_rate = (int) sampleRate;

    native->prepare(maximumExpectedSamplesPerBlock);

    for (int i = 0, n = native->getNumPorts(); i < n; i++) {
        auto port = native->getPort(i);
        if (port->getContentType() == AAP_CONTENT_TYPE_MIDI2) {
            if (port->getPortDirection() == AAP_PORT_DIRECTION_INPUT)
                aap_midi_in_port = i;
            else
                aap_midi_out_port = i;
        }

        native->activate();
    }
}

void AndroidAudioPluginInstance::releaseResources() {
    native->deactivate();
}

AndroidAudioPluginInstance::~AndroidAudioPluginInstance() {
    native->dispose();
}

#define MAX_ACCEPTABLE_PROCESS_NANOSECCONDS 10000000 // I think it's fair to say that one plugin taking 10msec. is not appropriate...

int32_t n_warned{0};

void AndroidAudioPluginInstance::processBlock(AudioBuffer<float> &audioBuffer,
                                              MidiBuffer &midiMessages) {
    struct timespec ts_start, ts_pre, ts_proc, ts_end;
    bool empty = midiMessages.isEmpty(); // store it here before postProcessBuffers() replaces the content.

    //auto buffer = native->getAudioPluginBuffer();
    //buffer->num_frames = static_cast<size_t>(audioBuffer.getNumSamples());

    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts_start);
    preProcessBuffers(audioBuffer, midiMessages);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts_pre);
    // FIXME: here we would need to pass the exact number of frames.
    //  Right now we only pass maximumExpectedSampleBlock as the fixed frame size, which is *wrong*.
    native->process(0);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts_proc);
    postProcessBuffers(audioBuffer, midiMessages);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts_end);
    /* Enable this for debugging if you have doubts on your hosting plugin(s)...
    if (!empty) {
        aap::a_log_f(AAP_LOG_LEVEL_INFO, AAP_JUCE_LOG_TAG, "processBlock() start %lu",
                     ts_start.tv_nsec + ts_start.tv_sec * 1000000000);
        aap::a_log_f(AAP_LOG_LEVEL_INFO, AAP_JUCE_LOG_TAG, "processBlock()   pre %lu",
                     ts_pre.tv_nsec + ts_pre.tv_sec * 1000000000);
        aap::a_log_f(AAP_LOG_LEVEL_INFO, AAP_JUCE_LOG_TAG, "processBlock()  proc %lu",
                     ts_proc.tv_nsec + ts_proc.tv_sec * 1000000000);
        aap::a_log_f(AAP_LOG_LEVEL_INFO, AAP_JUCE_LOG_TAG, "processBlock()   end %lu",
                     ts_end.tv_nsec + ts_end.tv_sec * 1000000000);
    }
    */
    long durationNanoseconds = (ts_end.tv_sec - ts_start.tv_sec) * 1000000000 + ts_end.tv_nsec - ts_start.tv_nsec;
    if (n_warned++ < 100 && durationNanoseconds > MAX_ACCEPTABLE_PROCESS_NANOSECCONDS)
        aap::a_log_f(AAP_LOG_LEVEL_WARN, AAP_JUCE_LOG_TAG, "processBlock() exceeds 10msec. : %f", durationNanoseconds / 1000000.0);
}

bool AndroidAudioPluginInstance::hasMidiPort(bool isInput) const {
    for (int i = 0; i < native->getNumPorts(); i++) {
        auto p = native->getPort(i);
        if (p->getPortDirection() ==
            (isInput ? AAP_PORT_DIRECTION_INPUT : AAP_PORT_DIRECTION_OUTPUT) &&
                (p->getContentType() == AAP_CONTENT_TYPE_MIDI || p->getContentType() == AAP_CONTENT_TYPE_MIDI2))
            return true;
    }
    return false;
}

AudioProcessorEditor *AndroidAudioPluginInstance::createEditor() {
    return nullptr;
}

void AndroidAudioPluginInstance::fillInPluginDescription(PluginDescription &description) const {
    fillPluginDescriptionFromNativeInstance(description, native);
}

const aap::PluginInformation *
AndroidAudioPluginFormat::findPluginInformationFrom(const PluginDescription &desc) {
    auto list = aap::PluginListSnapshot::queryServices();
    return list.getPluginInformation(desc.fileOrIdentifier.toRawUTF8());
}

AndroidAudioPluginFormat::AndroidAudioPluginFormat() {
    plugin_list_snapshot = aap::PluginListSnapshot::queryServices();
#if ANDROID
    // FIXME: retrieve serviceConnectorInstanceId, not 0
    plugin_client_connections = aap::getPluginConnectionListByConnectorInstanceId(0, true);
    android_host = std::make_unique<aap::PluginClient>(plugin_client_connections, &plugin_list_snapshot);
    auto list = aap::PluginListSnapshot::queryServices();
    for (int i = 0; i < list.getNumPluginInformation(); i++) {
        auto d = list.getPluginInformation(i);
        auto dst = new PluginDescription();
        fillPluginDescriptionFromNativeDescription(*dst, *d);
        cached_descs.set(d, dst);
    }
#else
    // FIXME: desktop implementation is missing
#endif
}

AndroidAudioPluginFormat::~AndroidAudioPluginFormat() {}

void AndroidAudioPluginFormat::findAllTypesForFile(OwnedArray <PluginDescription> &results,
                                                   const String &fileOrIdentifier) {
    // For Android `fileOrIdentifier` is a service name, and for desktop it is a specific `aap_metadata.xml` file.
#if ANDROID
    auto id = fileOrIdentifier.toRawUTF8();
    // So far there is no way to perform query (without Java help) it is retrieved from cached list.
    auto snapshot = aap::PluginListSnapshot::queryServices();
    for (int i = 0; i < snapshot.getNumPluginInformation(); i++) {
        auto p = snapshot.getPluginInformation(i);
        if (strcmp(p->getPluginPackageName().c_str(), id) == 0) {
            auto d = new PluginDescription();
            juce_plugin_descs.add(d);
            fillPluginDescriptionFromNativeCommon(*d, *p);
            results.add(d);
        }
    }
#else
    auto metadataPath = fileOrIdentifier.toRawUTF8();
    std::vector<PluginInformation*> plugins{};
    aap::aap_parse_plugin_descriptor_into(nullptr, nullptr, metadataPath, plugins);
    for (auto p : plugins) {
        auto dst = new PluginDescription();
        juce_plugin_descs.add(dst); // to automatically free when disposing `this`.
        fillPluginDescriptionFromNative(*dst, *p);
        results.add(dst);
        // FIXME: not sure if we should add it here. There is no ther timing to do this at desktop.
        cached_descs.set(p, dst);
    }
#endif
}

void AndroidAudioPluginFormat::createPluginInstance(const PluginDescription &description,
                                                    double initialSampleRate,
                                                    int initialBufferSize,
                                                    PluginCreationCallback callback) {
    auto pluginInfo = findPluginInformationFrom(description);
    if (pluginInfo == nullptr) {
        String error("");
        error << "Android Audio Plugin " << description.name << "was not found.";
        callback(nullptr, error);
    } else {
        // If the plugin service is not connected yet, then connect asynchronously with the callback
        // that processes instancing and invoke user callback (PluginCreationCallback).
        std::function<void(int32_t,std::string&)> aapCallback = [this, callback](int32_t instanceID, std::string& error) {
            auto androidInstance = android_host->getInstanceById(instanceID);

            callback(std::make_unique<AndroidAudioPluginInstance>(androidInstance), error);
        };
        int sampleRate = (int) initialSampleRate;
        auto identifier = pluginInfo->getPluginID();
        auto service = plugin_client_connections->getServiceHandleForConnectedPlugin(pluginInfo->getPluginPackageName(), pluginInfo->getPluginLocalName());
        if (service != nullptr) {
            auto result = android_host->createInstance(identifier, sampleRate, true);
            aapCallback(result.value, result.error);
        } else {
            std::function<void(std::string&)> cb = [identifier,sampleRate,aapCallback,this](std::string& error) {
                if (error.empty()) {
                    auto result = android_host->createInstance(identifier, sampleRate, true);
                    aapCallback(result.value, result.error);
                }
                else
                    aapCallback(-1, error);
            };
            // Make sure we launch a non-main thread
            Thread::launch([this, pluginInfo, cb] {
                PluginClientSystem::getInstance()->ensurePluginServiceConnected(plugin_client_connections, pluginInfo->getPluginPackageName(), cb);
            });
        }
    }
}

StringArray AndroidAudioPluginFormat::searchPathsForPlugins(const FileSearchPath &directoriesToSearch,
                                  bool recursive,
                                  bool allowPluginsWhichRequireAsynchronousInstantiation) {
    std::vector<std::string> paths{};
    StringArray ret{};
    auto list = aap::PluginListSnapshot::queryServices();
    for (int i = 0; i < list.getNumPluginInformation(); i++) {
        auto package = list.getPluginInformation(i)->getPluginPackageName();
        if (!std::binary_search(paths.begin(), paths.end(), package))
            paths.emplace_back(package);
    }
    for (auto p : paths)
        ret.add(p);
    return ret;
}

FileSearchPath AndroidAudioPluginFormat::getDefaultLocationsToSearch() {
    FileSearchPath ret{};
#if ANDROID
    // JUCE crashes if invalid path is returned here, so we have to resort to dummy path.
    //  JUCE design is lame on file systems in general.
    File dir{"/"};
    ret.add(dir);
#else
    for (auto path : PluginClientSystem::getInstance()->getPluginPaths()) {
        if(!File::isAbsolutePath(path)) // invalid path
            continue;
        File dir{path};
        if (dir.exists())
            ret.add(dir);
    }
#endif
    return ret;
}

} // namespace
