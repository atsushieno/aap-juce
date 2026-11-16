// Runs the actual JUCE wrapper against a probe AudioProcessor, through the AAP tail extension.
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <aap/android-audio-plugin.h>
#include <aap/ext/offline-rendering.h>
#include <aap/ext/plugin-info.h>
#include <aap/ext/tail.h>

extern "C" AndroidAudioPluginFactory* GetJuceAAPFactory();

// aap::PluginHost::getInstanceById() is in libandroidaudioplugin; only the GUI JNI functions use it.
extern "C" void* aapTestNoInstanceById(void*, int) __asm__("__ZN3aap10PluginHost15getInstanceByIdEi");
extern "C" void* aapTestNoInstanceById(void*, int) { abort(); }

namespace {
// The tail that prepareToPlay() picks, by rendering mode, as a plugin choosing its engine would.
double realtimeTail{2.0}, offlineTail{2.0};

class Probe : public juce::AudioProcessor {
public:
    double tail{0};
    Probe() : AudioProcessor(BusesProperties().withOutput("Out", juce::AudioChannelSet::mono())) {}
    const juce::String getName() const override { return "Probe"; }
    void prepareToPlay(double, int) override { tail = isNonRealtime() ? offlineTail : realtimeTail; }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override { buffer.clear(); }
    double getTailLengthSeconds() const override { return tail; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
} *probe;
}

juce::AudioProcessor* createPluginFilter() { return probe = new Probe(); }

namespace {
constexpr int32_t AUDIO_OUT = 0, MIDI_IN = 1, MIDI_OUT = 2, PORTS = 3, MIDI_SIZE = 4096;

int32_t frames = 64;
std::vector<float> audio(4096);
std::vector<uint8_t> midiIn(MIDI_SIZE), midiOut(MIDI_SIZE);
int notifications{0};

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

aap_plugin_info_t getPluginInfo(aap_host_plugin_info_extension_t*, AndroidAudioPluginHost*, const char*) {
    aap_plugin_info_t info{};
    info.get_port_count = [](aap_plugin_info_t*) -> uint32_t { return PORTS; };
    info.get_port = [](aap_plugin_info_t*, uint32_t index) {
        aap_plugin_info_port_t port{};
        port.context = (void*) (intptr_t) index;
        port.content_type = [](aap_plugin_info_port_t* p) {
            return (intptr_t) p->context == AUDIO_OUT ? AAP_CONTENT_TYPE_AUDIO : AAP_CONTENT_TYPE_MIDI2;
        };
        port.direction = [](aap_plugin_info_port_t* p) {
            return (intptr_t) p->context == MIDI_IN ? AAP_PORT_DIRECTION_INPUT : AAP_PORT_DIRECTION_OUTPUT;
        };
        return port;
    };
    return info;
}
aap_host_plugin_info_extension_t pluginInfoExtension{getPluginInfo};
aap_tail_host_extension_t tailHostExtension{nullptr, [](aap_tail_host_extension_t*, AndroidAudioPluginHost*) { notifications++; }};
AndroidAudioPluginHost host{nullptr, [](AndroidAudioPluginHost*, const char* uri) -> void* {
    if (!strcmp(uri, AAP_PLUGIN_INFO_EXTENSION_URI)) return &pluginInfoExtension;
    if (!strcmp(uri, AAP_TAIL_EXTENSION_URI)) return &tailHostExtension;
    return nullptr;
}, [](AndroidAudioPluginHost*) {}};

aap_buffer_t makeBuffer() {
    aap_buffer_t buffer{};
    buffer.num_frames = [](aap_buffer_t*) -> int32_t { return frames; };
    buffer.num_ports = [](aap_buffer_t*) -> int32_t { return PORTS; };
    buffer.get_buffer = [](aap_buffer_t*, int32_t index) -> void* {
        return index == AUDIO_OUT ? (void*) audio.data() : index == MIDI_IN ? (void*) midiIn.data() : (void*) midiOut.data();
    };
    buffer.get_buffer_size = [](aap_buffer_t*, int32_t index) -> int32_t {
        return index == AUDIO_OUT ? frames * (int32_t) sizeof(float) : MIDI_SIZE;
    };
    return buffer;
}

void run(AndroidAudioPlugin* plugin) {
    auto buffer = makeBuffer();
    auto ext = (aap_tail_extension_t*) plugin->get_extension(plugin, AAP_TAIL_EXTENSION_URI);
    check(ext && ext->tail_length_in_seconds, "the wrapper provides the extension");
    auto tail = [&] { return ext->tail_length_in_seconds(ext, plugin); };

    // Values, as JUCE VST3 reads them: 0 or less is no tail, and infinity is an endless one.
    probe->tail = 1.5;
    check(tail() == 1.5, "a finite tail passes through");
    probe->tail = std::numeric_limits<double>::infinity();
    check(std::isinf(tail()) && tail() > 0, "an infinite tail passes through");
    probe->tail = -1;
    check(tail() == 0, "a negative tail is no tail");
    probe->tail = std::nan("");
    check(tail() == 0, "NaN is no tail");

    // prepare() needs no notification, as AAP refreshes the host's view after it.
    plugin->prepare(plugin, 44100, &buffer);
    check(probe->tail == 2.0 && tail() == 2.0, "prepareToPlay() chose the tail");
    check(notifications == 0, "prepare() sends no notification");

    probe->updateHostDisplay();
    check(notifications == 0, "updateHostDisplay() without a tail change sends nothing");
    probe->tail = 3.0;
    probe->updateHostDisplay();
    check(notifications == 1, "updateHostDisplay() after a tail change notifies the host");
    probe->updateHostDisplay(juce::AudioProcessorListener::ChangeDetails{}.withParameterInfoChanged(true));
    check(notifications == 1, "the same tail is not notified again");

    // A changed rendering mode prepares the plugin again in activate(), outside AAP's prepare().
    offlineTail = 10.0;
    auto offline = (aap_offline_rendering_extension_t*) plugin->get_extension(plugin, AAP_OFFLINE_RENDERING_EXTENSION_URI);
    offline->set_offline_mode(offline, plugin, AAP_OFFLINE_RENDERING_MODE_OFFLINE);
    plugin->activate(plugin);
    check(tail() == 10.0 && notifications == 2, "a tail changed by re-preparation in activate() is notified");
    plugin->deactivate(plugin);
    plugin->activate(plugin);
    check(notifications == 2, "activate() without re-preparation sends nothing");
    plugin->deactivate(plugin);

    // Another prepare() takes the current tail as known, so a later change compares against it.
    plugin->prepare(plugin, 44100, &buffer);
    check(notifications == 2, "prepare() still sends nothing");
    probe->updateHostDisplay();
    check(notifications == 2, "and the prepared tail counts as known");
}
}

int main() {
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    auto factory = GetJuceAAPFactory();
    auto plugin = factory->instantiate(factory, "probe", &host);
    try {
        run(plugin);
        factory->release(factory, plugin);
    } catch (const std::exception& e) {
        fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
    puts("PASS: JUCE tail values reach the extension, and tail changes notify the host once");
    return 0;
}
