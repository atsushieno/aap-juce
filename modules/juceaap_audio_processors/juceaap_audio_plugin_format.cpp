#define ENABLE_MIDI2 1
#include "juceaap_audio_plugin_format.h"
#include <sys/mman.h>

#include <libgen.h>
#include <unistd.h>
#include "cmidi2.h"
#if ANDROID
#include <android/sharedmem.h>
#else
namespace aap {
extern void aap_parse_plugin_descriptor_into(const char* pluginPackageName, const char* pluginLocalName, const char* xmlfile, std::vector<PluginInformation*>& plugins);
}
#endif

using namespace aap;
using namespace juce;

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
    for (int i = 0; i < src.getNumPorts(); i++) {
        auto port = src.getPort(i);
        auto dir = port->getPortDirection();
        if (dir == AAP_PORT_DIRECTION_INPUT)
            description.numInputChannels++;
        else if (dir == AAP_PORT_DIRECTION_OUTPUT)
            description.numOutputChannels++;
    }
    description.hasSharedContainer = src.hasSharedContainer();
}

int processIncrementalCount{0};

void AndroidAudioPluginInstance::fillNativeInputBuffers(AudioBuffer<float> &audioBuffer,
                                                        MidiBuffer &midiBuffer) {
    processIncrementalCount++;

    // FIXME: there is some glitch between how JUCE AudioBuffer assigns a channel for each buffer item
    //  and how AAP expects them.
    int juceInputsAssigned = 0;
    auto aapDesc = this->native->getPluginInformation();
    int numPorts = aapDesc->getNumPorts();
    auto *buffer = native->getAudioPluginBuffer(audioBuffer.getNumChannels(), audioBuffer.getNumSamples());
    for (int portIndex = 0; portIndex < numPorts; portIndex++) {
        auto port = aapDesc->getPort(portIndex);
        auto portBuffer = buffer->buffers[portIndex];
        if (port->getPortDirection() != AAP_PORT_DIRECTION_INPUT)
            continue;
        if (port->getContentType() == AAP_CONTENT_TYPE_AUDIO && juceInputsAssigned < audioBuffer.getNumChannels())
            memcpy(portBuffer, (void *) audioBuffer.getReadPointer(juceInputsAssigned++), (size_t) audioBuffer.getNumSamples() * sizeof(float));
#if ENABLE_MIDI2
        else if (port->getContentType() == AAP_CONTENT_TYPE_MIDI) { // MIDI2 is handled below
#else
        else if (port->getContentType() == AAP_CONTENT_TYPE_MIDI || port->getContentType() == AAP_CONTENT_TYPE_MIDI2) {
#endif
            // Convert MidiBuffer into MIDI 1.0 message stream on the AAP port
            int dstIndex = 8; // fill length later
            MidiMessage msg{};
            int pos{0};
            // FIXME: time unit must be configurable.
            double oneTick = 1 / 480.0; // sec
            double secondsPerFrameJUCE = 1.0 / sample_rate; // sec
            MidiBuffer::Iterator iter{midiBuffer};
            *((int32_t*) portBuffer) = 480; // time division
            *((int32_t*) portBuffer + 2) = 0; // reset to ensure that there is no message by default
            while (iter.getNextEvent(msg, pos)) {
                double timestamp = msg.getTimeStamp();
                double timestampSeconds = timestamp * secondsPerFrameJUCE;
                int32_t timestampTicks = (int32_t) (timestampSeconds / oneTick);
                do {
                    *((uint8_t*) portBuffer + dstIndex++) = (uint8_t) timestampTicks % 0x80;
                    timestampTicks /= 0x80;
                } while (timestampTicks > 0x80);
                memcpy(((uint8_t*) portBuffer) + dstIndex, msg.getRawData(), msg.getRawDataSize());
                dstIndex += msg.getRawDataSize();
            }
            // AAP MIDI buffer is length-prefixed raw MIDI data.
            *((int32_t*) portBuffer + 1) = dstIndex - 8;
        } else if (port->getContentType() == AAP_CONTENT_TYPE_MIDI2) {
            // Convert MidiBuffer into MIDI 2.0 UMP stream on the AAP port
            int dstIntIndex = 8; // fill length later
            MidiMessage msg{};
            int pos{0};
            double oneTick = 1 / 31250.0; // sec
            double secondsPerFrameJUCE = 1.0 / sample_rate; // sec
            MidiBuffer::Iterator iter{midiBuffer};
            int32_t *dstI = (int32_t *) portBuffer;
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
                    int sysexSize = msg.getSysExDataSize();
                    int32_t numPackets = cmidi2_ump_sysex7_get_num_packets(sysexSize);
                    // FIXME; implement rest
                } else if (msg.isMetaEvent()) {
                    // FIXME: should we transmit META events in some wrapped manner like what we do for ktmidi MIDI 2.0 support?
                } else {
                    auto data = msg.getRawData();
                    *(dstI + dstIntIndex) = cmidi2_ump_midi1_message(0, data[0], (uint8_t) msg.getChannel(), data[1], data[2]);
                    dstIntIndex++;
                }
            }
            *dstI = (dstIntIndex - 8) * sizeof(uint32_t); // UMP length
            *(dstI + 1) = 0; // reserved
            // FIXME: use some constant for MIDI 2.0 Protocol (and Version 1)
            *(dstI + 2) = 2; // MIDI CI Protocol in AAP
            for (int i = 3; i < 8; i++)
                *(dstI + i) = 0; // reserved
        } else {
            // control parameters should assigned when parameter is assigned.
            // FIXME: at this state there is no callbacks for parameter changes, so
            // any dynamic parameter changes are ignored at this state.
        }
    }
}

void AndroidAudioPluginInstance::fillNativeOutputBuffers(AudioBuffer<float> &audioBuffer) {
    // FIXME: there is some glitch between how JUCE AudioBuffer assigns a channel for each buffer item
    //  and how AAP expects them.
    int juceOutputsAssigned = 0;
    auto aapDesc = this->native->getPluginInformation();
    int n = aapDesc->getNumPorts();
    auto *buffer = native->getAudioPluginBuffer(audioBuffer.getNumChannels(), audioBuffer.getNumSamples());
    for (int i = 0; i < n; i++) {
        auto port = aapDesc->getPort(i);
        if (port->getPortDirection() != AAP_PORT_DIRECTION_OUTPUT)
            continue;
        if (port->getContentType() == AAP_CONTENT_TYPE_AUDIO &&
            juceOutputsAssigned < audioBuffer.getNumChannels())
            memcpy((void *) audioBuffer.getWritePointer(juceOutputsAssigned++), buffer->buffers[i],
                   buffer->num_frames * sizeof(float));
    }
}

AndroidAudioPluginInstance::AndroidAudioPluginInstance(aap::PluginInstance *nativePlugin)
        : native(nativePlugin),
          sample_rate(-1) {
    // It is super awkward, but plugin parameter definition does not exist in juce::PluginInformation.
    // Only AudioProcessor.addParameter() works. So we handle them here.
    auto desc = nativePlugin->getPluginInformation();
    for (int i = 0; i < desc->getNumPorts(); i++) {
        auto port = desc->getPort(i);
        if (port->getPortDirection() != AAP_PORT_DIRECTION_INPUT || port->getContentType() != AAP_CONTENT_TYPE_UNDEFINED)
            continue;
        portMapAapToJuce[i] = getNumParameters();
#if JUCEAAP_HOSTED_PARAMETER
        addHostedParameter(std::unique_ptr<AndroidAudioPluginParameter>(new AndroidAudioPluginParameter(i, this, port)));
#else
        addParameter(new AndroidAudioPluginParameter(i, this, port));
#endif
    }
}

void AndroidAudioPluginInstance::updateParameterValue(AndroidAudioPluginParameter* parameter)
{
    int i = parameter->getAAPParameterIndex();

    if(getNumParameters() <= i) return; // too early to reach here.

    // In JUCE, control parameters are passed as a single value, while LV2 (in general?) takes values as a buffer.
    // It is inefficient to fill buffer every time, so we just set a value here.
    // FIXME: it is actually arguable which is more efficient, as it is possible that JUCE control parameters are
    //  more often assigned. Maybe we should have something like port properties that describes more characteristics
    //  like LV2 "expensive" port property (if that really is for it), and make use of them.
    //  related: https://github.com/atsushieno/android-audio-plugin-framework/issues/80
    //
    // This should not be the most likely case, but IF it is called *before* prepareToPlay() the audio buffer might be unallocated yet.
    auto *buffer = native->getAudioPluginBuffer(i, sizeof(float));
    float v = this->getParameters()[portMapAapToJuce[i]]->getValue();
    std::fill_n((float *) buffer->buffers[i], buffer->num_frames, v);
}

void
AndroidAudioPluginInstance::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) {
    sample_rate = (int) sampleRate;

    // minimum setup, as the pointers are not passed by JUCE framework side.
    int n = native->getPluginInformation()->getNumPorts();
    auto *buf = native->getAudioPluginBuffer(n, maximumExpectedSamplesPerBlock);
    native->prepare(maximumExpectedSamplesPerBlock, buf);

    for (int i = 0; i < n; i++) {
        auto port = native->getPluginInformation()->getPort(i);
        if (port->getContentType() == AAP_CONTENT_TYPE_UNDEFINED && port->getPortDirection() == AAP_PORT_DIRECTION_INPUT)
            updateParameterValue(dynamic_cast<AndroidAudioPluginParameter*>(getParameters()[portMapAapToJuce[i]]));
    }
    native->activate();
}

void AndroidAudioPluginInstance::releaseResources() {
    native->deactivate();
}

void AndroidAudioPluginInstance::destroyResources() {
    native->dispose();
}

void AndroidAudioPluginInstance::processBlock(AudioBuffer<float> &audioBuffer,
                                              MidiBuffer &midiMessages) {
    fillNativeInputBuffers(audioBuffer, midiMessages);
    native->process(native->getAudioPluginBuffer(audioBuffer.getNumChannels(), audioBuffer.getNumSamples()), 0);
    fillNativeOutputBuffers(audioBuffer);
}

bool AndroidAudioPluginInstance::hasMidiPort(bool isInput) const {
    auto d = native->getPluginInformation();
    for (int i = 0; i < d->getNumPorts(); i++) {
        auto p = d->getPort(i);
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
    // FIXME: retrieve serviceConnectorInstanceId, not 0
    plugin_client_connections = getPluginConnectionListFromJni(0, true);
    android_host = std::make_unique<aap::PluginClient>(plugin_client_connections, &plugin_list_snapshot);
#if ANDROID
    auto list = aap::PluginListSnapshot::queryServices();
    for (int i = 0; i < list.getNumPluginInformation(); i++) {
        auto d = list.getPluginInformation(i);
        auto dst = new PluginDescription();
        fillPluginDescriptionFromNative(*dst, *d);
        cached_descs.set(d, dst);
    }
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
#if ENABLE_MIDI2
        // add MidiCIExtension
        if (pluginInfo->hasMidi2Ports()) {
            midi_ci_extension->protocol = 2;
            midi_ci_extension->protocolVersion = 0;
            AndroidAudioPluginExtension ext;
            ext.uri = AAP_MIDI_CI_EXTENSION_URI;
            ext.data = &midi_ci_extension;
            ext.transmit_size = sizeof(MidiCIExtension);
            android_host->addCommonHostExtension(AAP_MIDI_CI_EXTENSION_URI, sizeof(MidiCIExtension));
        }
#endif

        // Once plugin service is bound, then continue connection.
        auto aapCallback = [=](int32_t instanceID, std::string error) {
            auto androidInstance = android_host->getInstance(instanceID);

            auto connections = android_host->getConnections();
            auto dMem = (uint64_t) connections;

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
#if ANDROID
    auto list = aap::PluginListSnapshot::queryServices();
    for (int i = 0; i < list.getNumPluginInformation(); i++) {
        auto package = list.getPluginInformation(i)->getPluginPackageName();
        if (!std::binary_search(paths.begin(), paths.end(), package))
            paths.emplace_back(package);
    }
#else
    for (int i = 0; i < directoriesToSearch.getNumPaths(); i++)
        getPluginHostPAL()->getAAPMetadataPaths(directoriesToSearch[i].getFullPathName().toRawUTF8(), paths);
#endif
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
    for (auto path : getPluginHostPAL()->getPluginPaths()) {
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
