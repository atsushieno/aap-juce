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

static void fillPluginDescriptionFromNative(PluginDescription &description,
                                            const aap::PluginInformation &src) {
    description.name = src.getDisplayName();
    description.pluginFormatName = "AAP";

    description.category.clear();
    description.category += juce::String(src.getPrimaryCategory());

    // The closer one would be `src.getManufacturerName()`, but that results in messy grouping on mobile UI.
    description.manufacturerName = src.getPluginPackageName();

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
    for (int i = 0; i < src.getNumDeclaredPorts(); i++) {
        auto port = src.getDeclaredPort(i);
        auto dir = port->getPortDirection();
        if (dir == AAP_PORT_DIRECTION_INPUT)
            description.numInputChannels++;
        else if (dir == AAP_PORT_DIRECTION_OUTPUT)
            description.numOutputChannels++;
    }
    description.hasSharedContainer = false; //src.hasSharedContainer();
}

int processIncrementalCount{0};

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

    processIncrementalCount++;

    // FIXME: there is some glitch between how JUCE AudioBuffer assigns a channel for each buffer item
    //  and how AAP expects them.
    int juceInputsAssigned = 0;
    int numPorts = native->getNumPorts();
    auto *buffer = native->getAudioPluginBuffer();
    for (int portIndex = 0; portIndex < numPorts; portIndex++) {
        auto port = native->getPort(portIndex);
        auto portBuffer = buffer->buffers[portIndex];
        if (port->getPortDirection() != AAP_PORT_DIRECTION_INPUT)
            continue;
        if (port->getContentType() == AAP_CONTENT_TYPE_AUDIO && juceInputsAssigned < audioBuffer.getNumChannels())
            memcpy(portBuffer, (void *) audioBuffer.getReadPointer(juceInputsAssigned++), (size_t) audioBuffer.getNumSamples() * sizeof(float));
        else if (port->getContentType() == AAP_CONTENT_TYPE_MIDI2) { // MIDI2 is handled below
            // Convert MidiBuffer into MIDI 2.0 UMP stream on the AAP port
            size_t dstIntIndex = sizeof(AAPMidiBufferHeader) / sizeof(int32_t); // fill length later
            MidiMessage msg{};
            int pos{0};
            const double oneTick = 1 / 31250.0; // sec
            double secondsPerFrameJUCE = 1.0 / sample_rate; // sec
            MidiBuffer::Iterator iter{midiMessages};
            auto mbh = (AAPMidiBufferHeader*) portBuffer;
            uint32_t *dstI = (uint32_t *) portBuffer;
            *(dstI + 2) = 0; // reset to ensure that there is no message by default
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
            mbh->length = dstIntIndex * sizeof(uint32_t) - sizeof(AAPMidiBufferHeader);
            mbh->time_options = 0;
            for (int i = 0; i < 6; i++)
                mbh->reserved[i] = 0;
        } else {
            // control parameters should assigned when parameter is assigned.
            // FIXME: at this state there is no callbacks for parameter changes, so
            // any dynamic parameter changes are ignored at this state.
        }
    }

    // FIXME: RT unlock
}

void AndroidAudioPluginInstance::postProcessBuffers(AudioBuffer<float> &audioBuffer, MidiBuffer &midiMessages) {
    // FIXME: RT lock

    // FIXME: there is some glitch between how JUCE AudioBuffer assigns a channel for each buffer item
    //  and how AAP expects them.
    int juceOutputsAssigned = 0;
    int n = native->getNumPorts();
    auto *buffer = native->getAudioPluginBuffer();
    for (int i = 0; i < n; i++) {
        auto port = native->getPort(i);
        if (port->getPortDirection() != AAP_PORT_DIRECTION_OUTPUT)
            continue;
        if (port->getContentType() == AAP_CONTENT_TYPE_AUDIO &&
            juceOutputsAssigned < audioBuffer.getNumChannels())
            memcpy((void *) audioBuffer.getWritePointer(juceOutputsAssigned++), buffer->buffers[i],
                   buffer->num_frames * sizeof(float));
    }

    if (aap_midi_out_port >= 0) {
        auto mbh = (AAPMidiBufferHeader*) buffer->buffers[aap_midi_out_port];
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
        ((AAPMidiBufferHeader *) buffer->buffers[aap_midi_in_port])->length = 0;

    // FIXME: RT unlock
}

AndroidAudioPluginInstance::AndroidAudioPluginInstance(aap::PluginInstance *nativePlugin)
        : native(nativePlugin),
          sample_rate(-1) {
    // It is super awkward, but plugin parameter definition does not exist in juce::PluginInformation.
    // Only AudioProcessor.addParameter() works. So we handle them here.
    auto desc = nativePlugin->getPluginInformation();
    for (int i = 0; i < nativePlugin->getNumParameters(); i++) {
        auto para = nativePlugin->getParameter(i);
        portMapAapToJuce[i] = getNumParameters();
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
    if (aap_midi_in_port < buffer->num_buffers)
        return false; // there is no port that accepts parameter changes

    int32_t paramId = parameter->getAAPParameterId();
    if (paramId > UINT16_MAX) {
        aap::a_log_f(AAP_LOG_LEVEL_ERROR, AAP_JUCE_LOG_TAG,
                     "Unsupported attempt to set parameter index %d which is > %d", paramId,
                     UINT16_MAX);
        return false;
    }

    // FIXME: RT lock

    auto mbh = (AAPMidiBufferHeader*) buffer->buffers[aap_midi_in_port];
    uint32_t* dst = (uint32_t*) (void*) ((uint8_t*) buffer->buffers[aap_midi_in_port] + sizeof(AAPMidiBufferHeader) + mbh->length);
    mbh->length += 16;
    aapMidi2ParameterSysex8(dst, dst + 1, dst + 2, dst + 3, 0, 0, 0, 0, (uint16_t) paramId, newValue);

    // FIXME: RT unlock

    return true;
}

void
AndroidAudioPluginInstance::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) {
    sample_rate = (int) sampleRate;

    // minimum setup, as the pointers are not passed by JUCE framework side.
    int numPorts = native->getNumPorts();
    auto *buf = native->getAudioPluginBuffer(
            (size_t) numPorts, (size_t) maximumExpectedSamplesPerBlock, midi_buffer_size);
    native->prepare(maximumExpectedSamplesPerBlock, buf);

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

void AndroidAudioPluginInstance::processBlock(AudioBuffer<float> &audioBuffer,
                                              MidiBuffer &midiMessages) {
    preProcessBuffers(audioBuffer, midiMessages);
    native->process(native->getAudioPluginBuffer(), 0);
    postProcessBuffers(audioBuffer, midiMessages);
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
    auto src = native->getPluginInformation();
    fillPluginDescriptionFromNative(description, *src);
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
    plugin_client_connections = getPluginConnectionListFromJni(0, true);
    android_host = std::make_unique<aap::PluginClient>(plugin_client_connections, &plugin_list_snapshot);
    auto list = aap::PluginListSnapshot::queryServices();
    for (int i = 0; i < list.getNumPluginInformation(); i++) {
        auto d = list.getPluginInformation(i);
        auto dst = new PluginDescription();
        fillPluginDescriptionFromNative(*dst, *d);
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
            fillPluginDescriptionFromNative(*d, *p);
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
        // Once plugin service is bound, then continue connection.
        auto aapCallback = [=](int32_t instanceID, std::string error) {
            auto androidInstance = android_host->getInstance(instanceID);

            androidInstance->completeInstantiation();

            MessageManager::callAsync([=] {
                std::unique_ptr <AndroidAudioPluginInstance> instance{
                        new AndroidAudioPluginInstance(androidInstance)};
                callback(std::move(instance), error);
            });
        };
        android_host->createInstanceAsync(pluginInfo->getPluginID(), (int) initialSampleRate, false, aapCallback);
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
