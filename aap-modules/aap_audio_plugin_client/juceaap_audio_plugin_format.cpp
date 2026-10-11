#include "juceaap_audio_plugin_format.h"
#include <sys/mman.h>

#include <libgen.h>
#include <unistd.h>
#include "cmidi2.h"
#include <aap/ext/parameters.h>
#if ANDROID
#include <android/sharedmem.h>
#include <android/trace.h>
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

    for (int b = 0, n = native->getNumBuses(AAP_BUS_KIND_AUDIO, AAP_PORT_DIRECTION_INPUT); b < n; b++)
        description.numInputChannels += native->getBus(AAP_BUS_KIND_AUDIO, AAP_PORT_DIRECTION_INPUT, b)->getChannelCount();
    for (int b = 0, n = native->getNumBuses(AAP_BUS_KIND_AUDIO, AAP_PORT_DIRECTION_OUTPUT); b < n; b++)
        description.numOutputChannels += native->getBus(AAP_BUS_KIND_AUDIO, AAP_PORT_DIRECTION_OUTPUT, b)->getChannelCount();
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

    auto *buffer = native->getAudioPluginBuffer();

    // JUCE buses mirror the AAP audio buses (see createJuceBuses()).
    // Channels that JUCE does not feed (e.g. a bus disabled only on the JUCE side) get silence.
    for (int b = 0, nb = native->getNumBuses(AAP_BUS_KIND_AUDIO, AAP_PORT_DIRECTION_INPUT); b < nb; b++) {
        auto bus = native->getBus(AAP_BUS_KIND_AUDIO, AAP_PORT_DIRECTION_INPUT, b);
        auto juceChannels = b < getBusCount(true) ? getChannelCountOfBus(true, b) : 0;
        for (int ch = 0, nc = bus->getChannelCount(); ch < nc; ch++) {
            auto dst = buffer->get_buffer(buffer, bus->getPortIndex(ch));
            auto juceChannel = ch < juceChannels ? getChannelIndexInProcessBlockBuffer(true, b, ch) : -1;
            if (juceChannel >= 0 && juceChannel < audioBuffer.getNumChannels())
                memcpy(dst, (void *) audioBuffer.getReadPointer(juceChannel), audioBuffer.getNumSamples() * sizeof(float));
            else
                memset(dst, 0, audioBuffer.getNumSamples() * sizeof(float));
        }
    }

    if (aap_midi_in_port >= 0) { // it should be usually true as it supports all parameter changes.
        auto mbh = (AAPMidiBufferHeader*) buffer->get_buffer(buffer, aap_midi_in_port);

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
                    *(dstI + dstIntIndex) = cmidi2_ump_jr_timestamp_direct((uint32_t)(timestampTicks) % 62500); // 2 sec.
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

    auto *buffer = native->getAudioPluginBuffer();

    for (int b = 0, nb = std::min(native->getNumBuses(AAP_BUS_KIND_AUDIO, AAP_PORT_DIRECTION_OUTPUT), getBusCount(false)); b < nb; b++) {
        auto bus = native->getBus(AAP_BUS_KIND_AUDIO, AAP_PORT_DIRECTION_OUTPUT, b);
        for (int ch = 0, nc = std::min(bus->getChannelCount(), getChannelCountOfBus(false, b)); ch < nc; ch++) {
            auto juceChannel = getChannelIndexInProcessBlockBuffer(false, b, ch);
            if (juceChannel < audioBuffer.getNumChannels())
                memcpy((void *) audioBuffer.getWritePointer(juceChannel), buffer->get_buffer(buffer, bus->getPortIndex(ch)),
                       audioBuffer.getNumSamples() * sizeof(float));
        }
    }

    if (aap_midi_out_port >= 0) {
        auto mbh = (AAPMidiBufferHeader*) buffer->get_buffer(buffer, aap_midi_out_port);
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
        ((AAPMidiBufferHeader *) buffer->get_buffer(buffer, aap_midi_in_port))->length = 0;

    // FIXME: RT unlock
}

static juce::AudioChannelSet toJuceChannelSet(const aap::BusInformation* bus) {
    if (!bus->isEnabled() || bus->getChannelCount() == 0)
        return juce::AudioChannelSet::disabled();
    if (strcmp(bus->getLayout(), AAP_BUS_LAYOUT_MONO) == 0)
        return juce::AudioChannelSet::mono();
    if (strcmp(bus->getLayout(), AAP_BUS_LAYOUT_STEREO) == 0)
        return juce::AudioChannelSet::stereo();
    return juce::AudioChannelSet::discreteChannels(bus->getChannelCount());
}

// Each AAP audio bus becomes a JUCE bus, in the same order.
juce::AudioProcessor::BusesProperties AndroidAudioPluginInstance::createJuceBuses(aap::PluginInstance* native) {
    juce::AudioProcessor::BusesProperties ret{};
    for (auto direction : {AAP_PORT_DIRECTION_INPUT, AAP_PORT_DIRECTION_OUTPUT}) {
        for (int b = 0, n = native->getNumBuses(AAP_BUS_KIND_AUDIO, direction); b < n; b++) {
            auto bus = native->getBus(AAP_BUS_KIND_AUDIO, direction, b);
            ret.addBus(direction == AAP_PORT_DIRECTION_INPUT, bus->getName(), toJuceChannelSet(bus));
        }
    }
    return ret;
}

static std::string toAAPBusLayout(const juce::AudioChannelSet& set) {
    if (set == juce::AudioChannelSet::mono())
        return AAP_BUS_LAYOUT_MONO;
    if (set == juce::AudioChannelSet::stereo())
        return AAP_BUS_LAYOUT_STEREO;
    if (set == juce::AudioChannelSet::createLCR())
        return "lcr";
    if (set == juce::AudioChannelSet::quadraphonic())
        return "quad";
    if (set == juce::AudioChannelSet::create5point1())
        return "5.1";
    if (set == juce::AudioChannelSet::create7point1())
        return "7.1";
    if (set.getAmbisonicOrder() >= 0)
        return "ambisonic(" + std::to_string(set.getAmbisonicOrder()) + ")";
    return aap::BusInformation::getDefaultLayoutForChannelCount(set.size());
}

// Each JUCE bus has the channel count of its AAP bus, or is disabled only on the JUCE side.
bool AndroidAudioPluginInstance::matchesPluginBusLayout(const BusesLayout& layouts) const {
    for (bool isInput : {true, false}) {
        auto& sets = isInput ? layouts.inputBuses : layouts.outputBuses;
        auto direction = isInput ? AAP_PORT_DIRECTION_INPUT : AAP_PORT_DIRECTION_OUTPUT;
        if (sets.size() != native->getNumBuses(AAP_BUS_KIND_AUDIO, direction))
            return false;
        for (int b = 0; b < sets.size(); b++)
            if (!sets[b].isDisabled() && sets[b].size() != native->getBus(AAP_BUS_KIND_AUDIO, direction, b)->getChannelCount())
                return false;
    }
    return true;
}

// JUCE may change layouts without canApplyBusesLayout() (e.g. setPlayConfigDetails()), so
// only those that need no change on the plugin side are supported here.
bool AndroidAudioPluginInstance::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return adopting_plugin_layout || matchesPluginBusLayout(layouts);
}

// The AAP plugin accepts or rejects a whole layout atomically, so it is requested here: JUCE
// changes its buses right after this check succeeds. The host prepares the instance again.
bool AndroidAudioPluginInstance::canApplyBusesLayout(const BusesLayout& layouts) const {
    if (adopting_plugin_layout || layouts == getBusesLayout() || matchesPluginBusLayout(layouts))
        return true;
    auto remote = dynamic_cast<aap::RemotePluginInstance*>(native);
    if (!remote)
        return false;
    aap_bus_layout_request_t request{};
    for (bool isInput : {true, false}) {
        auto& sets = isInput ? layouts.inputBuses : layouts.outputBuses;
        auto direction = isInput ? AAP_PORT_DIRECTION_INPUT : AAP_PORT_DIRECTION_OUTPUT;
        if (sets.size() != native->getNumBuses(AAP_BUS_KIND_AUDIO, direction))
            return false;
        for (int b = 0; b < sets.size() && request.count < AAP_MAX_BUSES; b++) {
            auto& bus = request.buses[request.count++];
            bus.id = native->getBus(AAP_BUS_KIND_AUDIO, direction, b)->getId();
            bus.enabled = !sets[b].isDisabled();
            bus.channel_count = sets[b].size();
            strncpy(bus.layout, toAAPBusLayout(sets[b]).c_str(), AAP_MAX_BUS_LAYOUT_CHARS - 1);
        }
    }
    bool wasActive = remote->getInstanceState() == aap::PLUGIN_INSTANTIATION_STATE_ACTIVE;
    if (wasActive)
        remote->deactivate();
    auto error = remote->applyBusLayout(request);
    if (!error.empty()) {
        aap::a_log_f(AAP_LOG_LEVEL_WARN, AAP_JUCE_LOG_TAG, "The bus layout was not applied: %s", error.c_str());
        if (wasActive && remote->getInstanceState() == aap::PLUGIN_INSTANTIATION_STATE_INACTIVE)
            remote->activate();
        return false;
    }
    return true;
}

bool AndroidAudioPluginInstance::canApplyBusCountChange(bool isInput, bool isAdding, BusProperties& outProperties) {
    if (!adopting_plugin_layout)
        return false;
    if (isAdding) {
        auto bus = native->getBus(AAP_BUS_KIND_AUDIO, isInput ? AAP_PORT_DIRECTION_INPUT : AAP_PORT_DIRECTION_OUTPUT,
                                  getBusCount(isInput));
        if (!bus)
            return false;
        outProperties = {bus->getName(), toJuceChannelSet(bus), bus->isEnabled()};
    }
    return true;
}

// Makes the JUCE buses follow the AAP audio buses again (see createJuceBuses()).
void AndroidAudioPluginInstance::adoptPluginBusLayout() {
    adopting_plugin_layout = true;
    BusesLayout layouts;
    for (bool isInput : {true, false}) {
        auto direction = isInput ? AAP_PORT_DIRECTION_INPUT : AAP_PORT_DIRECTION_OUTPUT;
        auto count = native->getNumBuses(AAP_BUS_KIND_AUDIO, direction);
        while (getBusCount(isInput) < count && addBus(isInput)) {}
        while (getBusCount(isInput) > count && removeBus(isInput)) {}
        if (getBusCount(isInput) != count) {
            aap::a_log_f(AAP_LOG_LEVEL_WARN, AAP_JUCE_LOG_TAG, "Could not follow the plugin bus count");
            adopting_plugin_layout = false;
            return;
        }
        for (int b = 0; b < count; b++)
            (isInput ? layouts.inputBuses : layouts.outputBuses).add(toJuceChannelSet(native->getBus(AAP_BUS_KIND_AUDIO, direction, b)));
    }
    if (!setBusesLayout(layouts))
        aap::a_log_f(AAP_LOG_LEVEL_WARN, AAP_JUCE_LOG_TAG, "Could not follow the plugin bus layout");
    adopting_plugin_layout = false;
}

// Like JUCE VST3 hosting does for restartComponent(kIoChanged): on the message thread, the
// instance is released, follows the new bus layout, and is prepared again if it was active.
void AndroidAudioPluginInstance::handleAsyncUpdate() {
    auto flags = pending_bus_changes.exchange(0);
    auto remote = dynamic_cast<aap::RemotePluginInstance*>(native);
    if (!(flags & AAP_BUSES_CHANGED_LAYOUT) || !remote)
        return;
    suspendProcessing(true);
    bool wasActive = remote->getInstanceState() == aap::PLUGIN_INSTANTIATION_STATE_ACTIVE;
    releaseResources();
    auto error = remote->refreshBusLayout();
    if (error.empty()) {
        adoptPluginBusLayout();
        if (wasActive && sample_rate > 0 && block_size > 0)
            prepareToPlay(sample_rate, block_size);
    } else
        aap::a_log_f(AAP_LOG_LEVEL_ERROR, AAP_JUCE_LOG_TAG, "Failed to refresh the bus layout: %s", error.c_str());
    suspendProcessing(false);
    updateHostDisplay();
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

    if (auto remote = dynamic_cast<aap::RemotePluginInstance*>(nativePlugin))
        remote->setBusesChangedHandler([this](uint32_t flags) {
            pending_bus_changes |= flags;
            triggerAsyncUpdate();
        });
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
    int32_t paramId = parameter->getAAPParameterId();
    if (paramId > UINT16_MAX) {
        aap::a_log_f(AAP_LOG_LEVEL_ERROR, AAP_JUCE_LOG_TAG,
                     "Unsupported attempt to set parameter index %d which is > %d", paramId,
                     UINT16_MAX);
        return false;
    }

    uint32_t message[4];
    auto transportValue = aapParameterPlainToTransportUint32(parameter->impl->getMinimumValue(),
                                                             parameter->impl->getMaximumValue(),
                                                             newValue);
    aapMidi2ParameterSysex8(message, message + 1, message + 2, message + 3, 0, 0, 0, 0, (uint16_t) paramId, transportValue);

    return native->tryAddEventUmpInput(message, sizeof(message));
}

void
AndroidAudioPluginInstance::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) {
    sample_rate = (int) sampleRate;
    block_size = maximumExpectedSamplesPerBlock;

    native->prepare(maximumExpectedSamplesPerBlock, (int32_t) sampleRate);

    aap_midi_in_port = native->getMainEventPortIndex(AAP_PORT_DIRECTION_INPUT);
    aap_midi_out_port = native->getMainEventPortIndex(AAP_PORT_DIRECTION_OUTPUT);

    native->activate();
}

void AndroidAudioPluginInstance::releaseResources() {
    native->deactivate();
}

AndroidAudioPluginInstance::~AndroidAudioPluginInstance() {
    // It returns after any running handler call, so nothing triggers an update afterwards.
    if (auto remote = dynamic_cast<aap::RemotePluginInstance*>(native))
        remote->setBusesChangedHandler({});
    cancelPendingUpdate();
    // it does not dispose here; whatever allocated the instance (and passed to the constructor) is responsible.
    native->deactivate();
}

#define MAX_ACCEPTABLE_PROCESS_NANOSECCONDS 10000000 // I think it's fair to say that one plugin taking 10msec. is not appropriate...

int32_t n_warned{0};

const char *AAP_JUCE_TRACE_SECTION_NAME = "aap-juce:host:process";
const char *AAP_JUCE_DSP_TRACE_SECTION_NAME = "aap-juce:host:dsp-process";

void AndroidAudioPluginInstance::processBlock(AudioBuffer<float> &audioBuffer,
                                              MidiBuffer &midiMessages) {
    struct timespec tsBegin, tsDspBegin, tsDspEnd, tsEnd;
#if ANDROID
    if (ATrace_isEnabled()) {
        clock_gettime(CLOCK_REALTIME, &tsBegin);
        ATrace_beginSection(AAP_JUCE_TRACE_SECTION_NAME);
    }
#endif
    preProcessBuffers(audioBuffer, midiMessages);

#if ANDROID
    if (ATrace_isEnabled()) {
        clock_gettime(CLOCK_REALTIME, &tsDspBegin);
        ATrace_beginSection(AAP_JUCE_DSP_TRACE_SECTION_NAME);
    }
#endif
    native->process(audioBuffer.getNumSamples(), 0);
#if ANDROID
    if (ATrace_isEnabled()) {
        clock_gettime(CLOCK_REALTIME, &tsDspEnd);
        ATrace_setCounter(AAP_JUCE_DSP_TRACE_SECTION_NAME,
                          (tsDspEnd.tv_sec - tsDspBegin.tv_sec) * 1000000000 + tsDspEnd.tv_nsec - tsDspBegin.tv_nsec);
        ATrace_endSection();
    }
#endif

    postProcessBuffers(audioBuffer, midiMessages);
#if ANDROID
    if (ATrace_isEnabled()) {
        clock_gettime(CLOCK_REALTIME, &tsEnd);
        ATrace_setCounter(AAP_JUCE_TRACE_SECTION_NAME,
                          (tsEnd.tv_sec - tsBegin.tv_sec) * 1000000000 + tsEnd.tv_nsec - tsBegin.tv_nsec);
        ATrace_endSection();
    }
#endif
}

bool AndroidAudioPluginInstance::hasMidiPort(bool isInput) const {
    for (int i = 0; i < native->getNumPorts(); i++) {
        auto p = native->getPort(i);
        if (p->getPortDirection() == (isInput ? AAP_PORT_DIRECTION_INPUT : AAP_PORT_DIRECTION_OUTPUT) &&
            (p->getContentType() == AAP_CONTENT_TYPE_MIDI || p->getContentType() == AAP_CONTENT_TYPE_MIDI2) &&
            strstr(p->getName(), "System MIDI") == nullptr)
            return true;
    }
    return false;
}

class AndroidAudioProcessorEditor : public AudioProcessorEditor {
public:
    AndroidAudioProcessorEditor(AudioProcessor *audioProcessor)
            : AudioProcessorEditor(audioProcessor),
              primaryDisplayMetrics(capturePrimaryDisplayMetrics()) {
        setBounds(getDefaultEditorBounds(primaryDisplayMetrics));
    }

    virtual ~AndroidAudioProcessorEditor() {
        getAudioProcessor()->editorBeingDeleted(this);
    }

protected:
    struct DisplayMetrics {
        Rectangle<int> userArea;
        double scale;
    };

    Rectangle<int> getDefaultEditorBounds() const {
        return getDefaultEditorBounds(primaryDisplayMetrics);
    }

    Rectangle<int> getNativeEditorBounds() const {
        return getNativeEditorBounds(primaryDisplayMetrics);
    }

    Rectangle<int> toAndroidPixelBounds(Rectangle<int> bounds) const {
        return toAndroidPixelBounds(bounds, primaryDisplayMetrics);
    }

    const DisplayMetrics& getPrimaryDisplayMetrics() const {
        return primaryDisplayMetrics;
    }

    static Rectangle<int> getDefaultEditorBounds(const DisplayMetrics& metrics) {
        auto screen = metrics.userArea;
        auto width = screen.getWidth() < 600 ? screen.getWidth() : 600;
        auto height = screen.getHeight() < 400 ? screen.getHeight() : 400;
        return {0, 0, width, height};
    }

    static Rectangle<int> getNativeEditorBounds(const DisplayMetrics& metrics) {
        auto screen = metrics.userArea;
        return {0, 0, jmax(1, screen.getWidth()), jmax(1, screen.getHeight())};
    }

    static Rectangle<int> toAndroidPixelBounds(Rectangle<int> bounds, const DisplayMetrics& metrics) {
        auto scale = metrics.scale;
        return {
            roundToInt(bounds.getX() * scale),
            roundToInt(bounds.getY() * scale),
            roundToInt(bounds.getWidth() * scale),
            roundToInt(bounds.getHeight() * scale)
        };
    }

    static DisplayMetrics capturePrimaryDisplayMetrics() {
        if (auto* display = Desktop::getInstance().getDisplays().getPrimaryDisplay())
            return { display->userArea, jmax(1.0, display->scale) };
        return { {0, 0, 600, 400}, 1.0 };
    }

    DisplayMetrics primaryDisplayMetrics;
};

class AndroidNativeAudioProcessorEditor : public AndroidAudioProcessorEditor {
public:
    AndroidNativeAudioProcessorEditor(AudioProcessor *audioProcessor, aap::RemotePluginInstance* native)
            : AndroidAudioProcessorEditor(audioProcessor),
              preferredSizeThread(*this),
              instance((aap::RemotePluginInstance*) native) {
        // We have to make sure to instantiate SurfaceView, which is done at this call.
        instance->prepareSurfaceControlForRemoteNativeUI();
        // Then, before trying to connect to SurfaceControlViewHost, we have to ensure to have client display ID.
        // To ensure that, we have to attach the SurfaceView to the window.
        auto nativeView = instance->getRemoteNativeView();
        auto aView = new AndroidViewComponent();
        aView->setView(nativeView);
        nativeViewComponent = aView;
        this->addAndMakeVisible(aView);
        resized();
        updatePreferredBoundsAndConnect();
    }

    ~AndroidNativeAudioProcessorEditor() override {
        if (nativeViewComponent != nullptr)
            nativeViewComponent->setView(nullptr);

        preferredSizeThread.stopThread(-1);
    }

    void resized() override {
        if (nativeViewComponent)
            nativeViewComponent->setBounds(getLocalBounds());
    }

private:
    class PreferredSizeThread : public Thread {
    public:
        explicit PreferredSizeThread(AndroidNativeAudioProcessorEditor& ownerEditor)
                : Thread("AAP native UI preferred size query"),
                  owner(ownerEditor) {
        }

        void run() override {
            owner.updatePreferredBoundsAndConnectOnWorker();
        }

    private:
        AndroidNativeAudioProcessorEditor& owner;
    };

    void updatePreferredBoundsAndConnect() {
        preferredSizeThread.startThread();
    }

    void updatePreferredBoundsAndConnectOnWorker() {
        Component::SafePointer<AndroidNativeAudioProcessorEditor> safeThis { this };
        auto* remoteInstance = instance;
        const auto metrics = getPrimaryDisplayMetrics();

        int32_t preferredWidth = 0;
        int32_t preferredHeight = 0;
        auto preferredBounds = getDefaultEditorBounds(metrics);

        if (remoteInstance != nullptr &&
            remoteInstance->getRemoteNativeViewPreferredSize(preferredWidth, preferredHeight) &&
            preferredWidth > 0 && preferredHeight > 0) {
            auto screen = metrics.userArea;
            auto scale = metrics.scale;
            auto width = jlimit(1, screen.getWidth(), roundToInt(preferredWidth / scale));
            auto height = jlimit(1, screen.getHeight(), roundToInt(preferredHeight / scale));
            preferredBounds = {0, 0, width, height};
        }

        MessageManager::callAsync([safeThis, preferredBounds] {
            if (safeThis.getComponent() != nullptr)
                safeThis->applyBoundsAndConnect(preferredBounds);
        });
    }

    void applyBoundsAndConnect(Rectangle<int> bounds) {
        if (connected)
            return;
        connected = true;
        setSize(bounds.getWidth(), bounds.getHeight());
        resized();
        auto androidBounds = toAndroidPixelBounds(getBounds());
        instance->connectRemoteNativeView(androidBounds.getWidth(), androidBounds.getHeight());
    }

    PreferredSizeThread preferredSizeThread;
    aap::RemotePluginInstance* instance{};
    AndroidViewComponent* nativeViewComponent{};
    bool connected{false};
};

class AndroidWebAudioProcessorEditor : public AndroidAudioProcessorEditor {
public:
    AndroidWebAudioProcessorEditor(AudioProcessor *audioProcessor, aap::RemotePluginInstance* instance)
    : AndroidAudioProcessorEditor(audioProcessor) {
        auto aView = new AndroidViewComponent();
        auto webView = instance->getRemoteWebView();
        aView->setView(webView);

        aView->setBounds(getBounds());
        this->addAndMakeVisible(aView);
    }
};

bool AndroidAudioPluginInstance::hasEditor() const {
    auto instance = (aap::RemotePluginInstance*) native;
    auto info = instance->getPluginInformation();
    if (info->getUiViewFactory().empty() && info->getUiWeb().empty())
            return false;
    return true;
}

AudioProcessorEditor *AndroidAudioPluginInstance::createEditor() {
    auto instance = (aap::RemotePluginInstance*) native;
    auto info = instance->getPluginInformation();
    if (info->getUiViewFactory().empty())
        return new AndroidWebAudioProcessorEditor(this, instance);
    else
        return new AndroidNativeAudioProcessorEditor(this, instance);
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
        auto identifier = pluginInfo->getPluginID();
        auto service = plugin_client_connections->getServiceHandleForConnectedPlugin(pluginInfo->getPluginPackageName(), pluginInfo->getPluginLocalName());
        if (service != nullptr) {
            auto result = android_host->createInstance(identifier, true);
            aapCallback(result.value, result.error);
        } else {
            std::function<void(std::string&)> cb = [identifier,aapCallback,this](std::string& error) {
                if (error.empty()) {
                    auto result = android_host->createInstance(identifier, true);
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
