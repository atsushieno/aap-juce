// Runs the actual JUCE wrapper and JUCE modules against a probe AudioProcessor, through the AAP plugin API.
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <aap/android-audio-plugin.h>
#include <aap/ext/offline-rendering.h>
#include <aap/ext/plugin-info.h>

extern "C" AndroidAudioPluginFactory* GetJuceAAPFactory();

// aap::PluginHost::getInstanceById() is in libandroidaudioplugin; only the GUI JNI functions use it.
extern "C" void* aapTestNoInstanceById(void*, int) __asm__("__ZN3aap10PluginHost15getInstanceByIdEi");
extern "C" void* aapTestNoInstanceById(void*, int) { abort(); }

namespace {
struct Observed {
    int prepares{0};
    int releases{0};
    bool nonRealtimeAtPrepare{false};
    double sampleRate{0};
    int samplesPerBlock{0};
    bool nonRealtimeAtProcess{false};
    int blocks{0};
} observed;

class Probe : public juce::AudioProcessor {
public:
    Probe() : AudioProcessor(BusesProperties().withOutput("Out", juce::AudioChannelSet::mono())) {}
    const juce::String getName() const override { return "Probe"; }
    void prepareToPlay(double rate, int samples) override {
        observed.prepares++;
        observed.nonRealtimeAtPrepare = isNonRealtime();
        observed.sampleRate = rate;
        observed.samplesPerBlock = samples;
    }
    void releaseResources() override { observed.releases++; }
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        observed.nonRealtimeAtProcess = isNonRealtime();
        observed.blocks++;
        buffer.clear();
    }
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
};
}

juce::AudioProcessor* createPluginFilter() { return new Probe(); }

namespace {
constexpr int32_t AUDIO_OUT = 0, MIDI_IN = 1, MIDI_OUT = 2, PORTS = 3, MIDI_SIZE = 4096;
constexpr auto REALTIME = AAP_OFFLINE_RENDERING_MODE_REALTIME;
constexpr auto OFFLINE = AAP_OFFLINE_RENDERING_MODE_OFFLINE;

int32_t frames = 64;
std::vector<float> audio(4096);
std::vector<uint8_t> midiIn(MIDI_SIZE), midiOut(MIDI_SIZE);

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
AndroidAudioPluginHost host{nullptr, [](AndroidAudioPluginHost*, const char* uri) -> void* {
    return strcmp(uri, AAP_PLUGIN_INFO_EXTENSION_URI) ? nullptr : &pluginInfoExtension;
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
    auto ext = (aap_offline_rendering_extension_t*) plugin->get_extension(plugin, AAP_OFFLINE_RENDERING_EXTENSION_URI);
    check(ext && ext->set_offline_mode, "the wrapper provides the extension");
    auto setMode = [&](aap_offline_rendering_mode_t mode) {
        check(ext->set_offline_mode(ext, plugin, mode) == AAP_OFFLINE_RENDERING_RESULT_OK, "set_offline_mode succeeds");
    };
    auto process = [&] {
        plugin->process(plugin, &buffer, frames, 0);
        return observed.nonRealtimeAtProcess;
    };

    // Set before prepare(): the plugin's only prepareToPlay() sees it.
    setMode(OFFLINE);
    check(observed.prepares == 0, "set_offline_mode itself does not prepare");
    plugin->prepare(plugin, 44100, &buffer);
    check(observed.prepares == 1 && observed.nonRealtimeAtPrepare, "prepareToPlay() sees the mode set before prepare()");
    plugin->activate(plugin);
    check(observed.prepares == 1, "no extra prepareToPlay() when the mode did not change");
    check(process(), "processBlock() sees offline mode");
    plugin->deactivate(plugin);

    // Set after prepare(): prepareToPlay() runs again with the same configuration.
    auto releases = observed.releases;
    setMode(REALTIME);
    check(observed.prepares == 1, "the slow part does not run inside the extension call");
    plugin->activate(plugin);
    check(observed.prepares == 2 && !observed.nonRealtimeAtPrepare, "prepareToPlay() runs again for a changed mode");
    check(observed.releases == releases + 1, "and releaseResources() precedes it");
    check(observed.sampleRate == 44100 && observed.samplesPerBlock == frames, "with the same configuration");
    check(!process(), "processBlock() sees realtime mode");
    plugin->deactivate(plugin);

    // No change: nothing is prepared again.
    setMode(REALTIME);
    plugin->activate(plugin);
    check(observed.prepares == 2, "no extra prepareToPlay() for the same mode");
    plugin->deactivate(plugin);

    // A changed mode, then another prepare(): only one prepareToPlay(), with the new block size.
    setMode(OFFLINE);
    frames = 128;
    plugin->prepare(plugin, 44100, &buffer);
    check(observed.prepares == 3 && observed.nonRealtimeAtPrepare && observed.samplesPerBlock == 128, "prepare() applies a pending mode");
    plugin->activate(plugin);
    check(observed.prepares == 3, "activate() has nothing left to apply");
    check(process(), "processBlock() stays offline");
    plugin->deactivate(plugin);

    // The re-prepare uses the latest block size.
    setMode(REALTIME);
    plugin->activate(plugin);
    check(observed.prepares == 4 && observed.samplesPerBlock == 128, "re-prepare uses the latest block size");
    plugin->deactivate(plugin);
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
    puts("PASS: the mode reaches prepareToPlay() and processBlock(), set before or after prepare()");
    return 0;
}
