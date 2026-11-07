// Runs the actual AndroidAudioPluginInstance the way a JUCE host does, over a recording aap::PluginInstance.
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include "aap_audio_plugin_client/juceaap_audio_plugin_format.h"
#include "aap/core/host/shared-memory-store.h"

using namespace aap;

namespace aap { void aap_parse_plugin_descriptor_into(const char*, const char*, const char*, std::vector<PluginInformation*>&) { std::abort(); } }

// Platform setup that the Android build provides; nothing here reaches them.
AndroidAudioPluginFactory* GetDesktopAudioPluginFactoryClientBridge(aap::PluginClient*) { std::abort(); }
aap::PluginClientSystem* aap::PluginClientSystem::getInstance() { std::abort(); }
bool aap::AbstractPluginBuffer::initialize(int32_t, int32_t) { std::abort(); }
int32_t aap::ClientPluginSharedMemoryStore::allocateClientBuffer(size_t, size_t, PluginInstance&, size_t) { std::abort(); }
int32_t aap::ServicePluginSharedMemoryStore::allocateServiceBuffer(std::vector<int32_t>&, size_t, PluginInstance&, size_t) { std::abort(); }
int32_t aap::ClientPluginSharedMemoryStore::allocateClientBufferPool(const aap_buffer_layout_t&, PluginInstance&) { std::abort(); }
std::string aap::ServicePluginSharedMemoryStore::completeServicePoolInitialization(const aap_buffer_layout_t&, int32_t, PluginInstance&) { std::abort(); }

namespace {
void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::string> events;

struct RecordingExtensions : xs::ClientStandardExtensions {
    bool refuse{false};
    aap::Result<aap_offline_rendering_result_t> setOfflineMode(aap_offline_rendering_mode_t mode) override {
        events.push_back(mode == AAP_OFFLINE_RENDERING_MODE_OFFLINE ? "offline" : "realtime");
        return {refuse ? AAP_OFFLINE_RENDERING_RESULT_GENERIC_FAILURE : AAP_OFFLINE_RENDERING_RESULT_OK, ""};
    }
};

AndroidAudioPlugin fakePlugin{};
AndroidAudioPluginHost fakeHost{};
AndroidAudioPluginFactory fakeFactory{
    [](AndroidAudioPluginFactory*, const char*, AndroidAudioPluginHost*) -> AndroidAudioPlugin* {
        fakePlugin.activate = [](AndroidAudioPlugin*) { events.push_back("activate"); };
        fakePlugin.deactivate = [](AndroidAudioPlugin*) { events.push_back("deactivate"); };
        return &fakePlugin;
    },
    [](AndroidAudioPluginFactory*, AndroidAudioPlugin*) {}, nullptr};

struct RecordingInstance : PluginInstance {
    RecordingExtensions extensions;
    RecordingInstance(const PluginInformation* info) : PluginInstance(info, &fakeFactory, 8192) { completeInstantiation(); }
    ~RecordingInstance() override { releasePlugin(); }
    AndroidAudioPluginHost* getHostFacadeForCompleteInstantiation() override { return &fakeHost; }
    int32_t getInstanceId() override { return 1; }
    void prepare(int, int32_t) override {
        events.push_back("prepare");
        instantiation_state = PLUGIN_INSTANTIATION_STATE_INACTIVE;
    }
    void process(int32_t, int32_t) override {}
    void setupAAPXS() override {}
    xs::StandardExtensions& getStandardExtensions() override { return extensions; }
};

std::string takeEvents() {
    std::string joined;
    for (auto& event : events) joined += (joined.empty() ? "" : " ") + event;
    events.clear();
    return joined;
}
void expect(const char* expected, const std::string& what) {
    auto actual = takeEvents();
    check(actual == expected, what + ": expected [" + expected + "], got [" + actual + "]");
}

PluginInformation makeInfo() {
    return PluginInformation{false, "test", "Test", "Test", "", "1", "test", "", "", "", "Instrument", "", "", ""};
}

void declaredPlugin() {
    auto info = makeInfo();
    info.addExtension(PluginExtensionInformation{false, AAP_OFFLINE_RENDERING_EXTENSION_URI});
    RecordingInstance native(&info);
    juceaap::AndroidAudioPluginInstance instance(&native);

    instance.prepareToPlay(48000, 64);
    expect("prepare activate", "the default mode sends nothing, so older services stay usable");
    instance.releaseResources();
    expect("deactivate", "release");

    instance.setNonRealtime(true);
    expect("", "setNonRealtime() does no IPC, as JUCE hosts may call it from anywhere");
    instance.prepareToPlay(48000, 64);
    expect("prepare offline activate", "the mode reaches the plugin after prepare and before activate");
    instance.releaseResources();
    takeEvents();

    instance.prepareToPlay(48000, 64);
    expect("prepare activate", "the same mode is not sent again");

    instance.setNonRealtime(false);
    expect("", "a change while active waits, as AAP would reject it");
    instance.releaseResources();
    takeEvents();
    instance.prepareToPlay(48000, 64);
    expect("prepare realtime activate", "the change applies at the next prepareToPlay()");
    instance.releaseResources();
    takeEvents();

    native.extensions.refuse = true;
    instance.setNonRealtime(true);
    instance.prepareToPlay(48000, 64);
    expect("prepare offline activate", "a refusal does not stop the host from playing");
    instance.releaseResources();
    takeEvents();
    native.extensions.refuse = false;
    instance.prepareToPlay(48000, 64);
    expect("prepare offline activate", "a refused mode is requested again");
    instance.releaseResources();
    takeEvents();
}

void undeclaredPlugin() {
    auto info = makeInfo();
    RecordingInstance native(&info);
    juceaap::AndroidAudioPluginInstance instance(&native);
    instance.setNonRealtime(true);
    instance.prepareToPlay(48000, 64);
    expect("prepare activate", "nothing is sent to a plugin that does not declare the extension");
    instance.releaseResources();
    takeEvents();
}
}

int main() {
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    try {
        declaredPlugin();
        undeclaredPlugin();
    } catch (const std::exception& e) {
        fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
    puts("PASS: setNonRealtime() reaches a declaring plugin between prepare and activate, and nothing else is sent");
    return 0;
}
