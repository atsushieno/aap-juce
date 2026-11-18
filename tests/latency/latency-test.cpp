// Runs the actual JUCE wrapper against a probe AudioProcessor, through the AAP latency extension.
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <aap/android-audio-plugin.h>
#include <aap/ext/offline-rendering.h>
#include <aap/ext/plugin-info.h>
#include <aap/ext/latency.h>

extern "C" AndroidAudioPluginFactory* GetJuceAAPFactory();

// aap::PluginHost::getInstanceById() is in libandroidaudioplugin; only the GUI JNI functions use it.
extern "C" void* aapTestNoInstanceById(void*, int) __asm__("__ZN3aap10PluginHost15getInstanceByIdEi");
extern "C" void* aapTestNoInstanceById(void*, int) { abort(); }

namespace {
// The latency that prepareToPlay() sets, by rendering mode, as a plugin choosing its engine would.
int realtimeLatency{64}, offlineLatency{64};

class Probe : public juce::AudioProcessor {
public:
    Probe() : AudioProcessor(BusesProperties().withOutput("Out", juce::AudioChannelSet::mono())) {}
    const juce::String getName() const override { return "Probe"; }
    void prepareToPlay(double, int) override { setLatencySamples(isNonRealtime() ? offlineLatency : realtimeLatency); }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override { buffer.clear(); }
    double getTailLengthSeconds() const override { return 0; }
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
bool notifiedOnMessageThread{true};

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// Delivers what JUCE posted to the message thread, which is this (main) thread here.
void runMessageLoop() {
    for (int i = 0; i < 10; i++)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
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
aap_latency_host_extension_t latencyHostExtension{nullptr, [](aap_latency_host_extension_t*, AndroidAudioPluginHost*) {
    notifications++;
    notifiedOnMessageThread &= juce::MessageManager::existsAndIsCurrentThread();
}};
AndroidAudioPluginHost host{nullptr, [](AndroidAudioPluginHost*, const char* uri) -> void* {
    if (!strcmp(uri, AAP_PLUGIN_INFO_EXTENSION_URI)) return &pluginInfoExtension;
    if (!strcmp(uri, AAP_LATENCY_EXTENSION_URI)) return &latencyHostExtension;
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
    auto ext = (aap_latency_extension_t*) plugin->get_extension(plugin, AAP_LATENCY_EXTENSION_URI);
    check(ext && ext->latency_in_samples, "the wrapper provides the extension");
    auto latency = [&] { return ext->latency_in_samples(ext, plugin); };
    check(latency() == 0 && notifications == 0, "no latency before prepare()");

    // prepareToPlay() sets the latency, as JUCE plugins do, and the host hears of it.
    plugin->prepare(plugin, 44100, &buffer);
    check(latency() == 64, "prepareToPlay() chose the latency");
    check(notifications == 1, "the latency set in prepare() is notified");
    plugin->prepare(plugin, 44100, &buffer);
    check(notifications == 1, "the same latency is not notified again");

    probe->setLatencySamples(128);
    check(latency() == 128 && notifications == 2, "setLatencySamples() on the message thread notifies at once");
    probe->updateHostDisplay();
    check(notifications == 2, "updateHostDisplay() without a latency change sends nothing");
    probe->setLatencySamples(-1);
    check(latency() == 0 && notifications == 3, "a negative latency is no latency");

    // From another thread (such as the audio thread) the RT-unsafe notification is deferred.
    std::thread([] { probe->setLatencySamples(256); }).join();
    check(latency() == 256 && notifications == 3, "another thread does not notify by itself");
    runMessageLoop();
    check(notifications == 4 && notifiedOnMessageThread, "the message thread notifies instead");
    std::thread([] { probe->setLatencySamples(300); probe->setLatencySamples(400); }).join();
    runMessageLoop();
    check(latency() == 400 && notifications == 5, "deferred changes notify once");

    // A changed rendering mode prepares the plugin again in activate(), outside AAP's prepare().
    offlineLatency = 1024;
    auto offline = (aap_offline_rendering_extension_t*) plugin->get_extension(plugin, AAP_OFFLINE_RENDERING_EXTENSION_URI);
    offline->set_offline_mode(offline, plugin, AAP_OFFLINE_RENDERING_MODE_OFFLINE);
    plugin->activate(plugin);
    check(latency() == 1024 && notifications == 6, "a latency changed by re-preparation in activate() is notified");
    plugin->deactivate(plugin);
    plugin->activate(plugin);
    check(notifications == 6, "activate() without re-preparation sends nothing");
    plugin->deactivate(plugin);
}
}

int main() {
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    auto factory = GetJuceAAPFactory();
    auto plugin = factory->instantiate(factory, "probe", &host);
    try {
        run(plugin);
        // A notification still pending at release is dropped.
        std::thread([] { probe->setLatencySamples(2048); }).join();
        auto before = notifications;
        factory->release(factory, plugin);
        runMessageLoop();
        check(notifications == before, "no notification after release");
    } catch (const std::exception& e) {
        fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
    puts("PASS: JUCE latency reaches the extension, and latency changes notify the host once, on the message thread");
    return 0;
}
