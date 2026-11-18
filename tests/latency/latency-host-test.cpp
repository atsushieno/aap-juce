// Runs the actual AndroidAudioPluginInstance the way a JUCE host does, over a recording aap::PluginInstance.
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
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

// Delivers what JUCE posted to the message thread, which is this (main) thread here.
void runMessageLoop() {
    for (int i = 0; i < 10; i++)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
}

struct RecordingExtensions : xs::ClientStandardExtensions {
    aap::Result<int32_t> reply{0, ""};
    int queries{0};
    aap::Result<int32_t> getLatency() override {
        queries++;
        return reply;
    }
};

AndroidAudioPlugin fakePlugin{};
AndroidAudioPluginHost fakeHost{};
AndroidAudioPluginFactory fakeFactory{
    [](AndroidAudioPluginFactory*, const char*, AndroidAudioPluginHost*) -> AndroidAudioPlugin* {
        fakePlugin.activate = [](AndroidAudioPlugin*) {};
        fakePlugin.deactivate = [](AndroidAudioPlugin*) {};
        return &fakePlugin;
    },
    [](AndroidAudioPluginFactory*, AndroidAudioPlugin*) {}, nullptr};

struct RecordingInstance : PluginInstance {
    RecordingExtensions extensions;
    RecordingInstance(const PluginInformation* info) : PluginInstance(info, &fakeFactory, 8192) { completeInstantiation(); }
    ~RecordingInstance() override { releasePlugin(); }
    AndroidAudioPluginHost* getHostFacadeForCompleteInstantiation() override { return &fakeHost; }
    int32_t getInstanceId() override { return 1; }
    void prepare(int, int32_t) override { instantiation_state = PLUGIN_INSTANTIATION_STATE_INACTIVE; }
    void process(int32_t, int32_t) override {}
    void setupAAPXS() override {}
    xs::StandardExtensions& getStandardExtensions() override { return extensions; }
};

PluginInformation makeInfo() {
    return PluginInformation{false, "test", "Test", "Test", "", "1", "test", "", "", "", "Instrument", "", "", ""};
}

void declaredPlugin() {
    auto info = makeInfo();
    info.addExtension(PluginExtensionInformation{false, AAP_LATENCY_EXTENSION_URI});
    RecordingInstance native(&info);
    auto& ext = native.extensions;
    {
        juceaap::AndroidAudioPluginInstance instance(&native);
        check(instance.getLatencySamples() == 0 && ext.queries == 0, "no latency before prepareToPlay()");

        ext.reply = {64, ""};
        instance.prepareToPlay(48000, 64);
        check(instance.getLatencySamples() == 64 && ext.queries == 1, "prepareToPlay() takes the plugin's latency");

        // The notification comes on a Binder thread; the query runs on the message thread.
        ext.reply = {128, ""};
        std::thread([&] { juceaap::AndroidAudioPluginInstance::dispatchLatencyChanged(&native); }).join();
        check(ext.queries == 1, "the notification does not query on its own thread");
        runMessageLoop();
        check(instance.getLatencySamples() == 128 && ext.queries == 2, "a notified latency reaches the JUCE host");

        juceaap::AndroidAudioPluginInstance::dispatchLatencyChanged(&native);
        juceaap::AndroidAudioPluginInstance::dispatchLatencyChanged(&native);
        runMessageLoop();
        check(ext.queries == 3, "notifications before the update query once");

        ext.reply = {-1, ""};
        juceaap::AndroidAudioPluginInstance::dispatchLatencyChanged(&native);
        runMessageLoop();
        check(instance.getLatencySamples() == 0, "a negative latency is no latency");

        ext.reply = {512, "timeout"};
        juceaap::AndroidAudioPluginInstance::dispatchLatencyChanged(&native);
        runMessageLoop();
        check(instance.getLatencySamples() == 0, "a failed query keeps the last latency");

        ext.reply = {256, ""};
        juceaap::AndroidAudioPluginInstance::dispatchLatencyChanged(&native);
    }
    auto queries = ext.queries;
    runMessageLoop();
    juceaap::AndroidAudioPluginInstance::dispatchLatencyChanged(&native);
    runMessageLoop();
    check(ext.queries == queries, "notifications after the instance is gone do nothing");
}

void undeclaredPlugin() {
    auto info = makeInfo();
    RecordingInstance native(&info);
    juceaap::AndroidAudioPluginInstance instance(&native);
    native.extensions.reply = {64, ""};
    instance.prepareToPlay(48000, 64);
    juceaap::AndroidAudioPluginInstance::dispatchLatencyChanged(&native);
    runMessageLoop();
    check(instance.getLatencySamples() == 0 && native.extensions.queries == 0, "a plugin without the extension is never asked");
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
    puts("PASS: the plugin's latency reaches getLatencySamples() after prepareToPlay() and on notification");
    return 0;
}
