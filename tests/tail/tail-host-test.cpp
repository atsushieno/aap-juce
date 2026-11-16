// Runs the actual AndroidAudioPluginInstance the way a JUCE host does, over a recording aap::PluginInstance.
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include "aap_audio_plugin_client/juceaap_audio_plugin_format.h"
#include "aap/core/host/shared-memory-store.h"
#include "aap/core/realtime.h"

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

// Answers like the tail AAPXS client; `declared` false keeps the real one, which has no tail client here.
struct RecordingExtensions : xs::ClientStandardExtensions {
    bool declared{true};
    aap::Result<double> reply{0.0, ""};
    int queries{0};
    aap::Result<double> getTailLength() override {
        if (!declared || aap::RealtimeScope::isActive())
            return ClientStandardExtensions::getTailLength();
        queries++;
        return reply;
    }
};

AndroidAudioPlugin fakePlugin{};
AndroidAudioPluginHost fakeHost{};
AndroidAudioPluginFactory fakeFactory{
    [](AndroidAudioPluginFactory*, const char*, AndroidAudioPluginHost*) -> AndroidAudioPlugin* { return &fakePlugin; },
    [](AndroidAudioPluginFactory*, AndroidAudioPlugin*) {}, nullptr};

struct RecordingInstance : PluginInstance {
    RecordingExtensions extensions;
    RecordingInstance(const PluginInformation* info) : PluginInstance(info, &fakeFactory, 8192) { completeInstantiation(); }
    ~RecordingInstance() override { releasePlugin(); }
    AndroidAudioPluginHost* getHostFacadeForCompleteInstantiation() override { return &fakeHost; }
    int32_t getInstanceId() override { return 1; }
    void prepare(int, int32_t) override {}
    void process(int32_t, int32_t) override {}
    void setupAAPXS() override {}
    xs::StandardExtensions& getStandardExtensions() override { return extensions; }
};

PluginInformation makeInfo() {
    return PluginInformation{false, "test", "Test", "Test", "", "1", "test", "", "", "", "Instrument", "", "", ""};
}

void declaredPlugin() {
    auto info = makeInfo();
    info.addExtension(PluginExtensionInformation{false, AAP_TAIL_EXTENSION_URI});
    RecordingInstance native(&info);
    juceaap::AndroidAudioPluginInstance instance(&native);
    auto& ext = native.extensions;

    ext.reply = {1.5, ""};
    check(instance.getTailLengthSeconds() == 1.5, "the plugin's tail reaches the JUCE host");
    ext.reply = {std::numeric_limits<double>::infinity(), ""};
    auto infinite = instance.getTailLengthSeconds();
    check(std::isinf(infinite) && infinite > 0, "an infinite tail reaches it as JUCE's infinity");
    ext.reply = {4.0, ""};
    check(instance.getTailLengthSeconds() == 4.0, "each call asks again, so a changed tail shows at once");

    ext.reply = {0.0, "timeout"};
    check(instance.getTailLengthSeconds() == 4.0, "a failed query gives the last answer");

    auto queries = ext.queries;
    {
        RealtimeScope realtime;
        check(instance.getTailLengthSeconds() == 4.0, "a realtime caller gets the last answer");
    }
    check(ext.queries == queries, "without asking the plugin");
}

void undeclaredPlugin() {
    auto info = makeInfo();
    RecordingInstance native(&info);
    native.extensions.declared = false;
    juceaap::AndroidAudioPluginInstance instance(&native);
    check(instance.getTailLengthSeconds() == 0, "a plugin without the extension has no tail");
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
    puts("PASS: getTailLengthSeconds() reports the plugin's tail, and the last answer when it cannot ask");
    return 0;
}
