#include <juce_audio_processors/juce_audio_processors.h>
#include <aap/core/host/audio-plugin-host.h>
#include <aap/core/host/plugin-client-system.h>
#include <aap/core/host/android/audio-plugin-host-android.h>
#include <aap/ext/midi.h>

using namespace juce;

namespace juceaap {

class AndroidAudioPluginParameter;

class AndroidAudioPluginInstance : public juce::AudioPluginInstance {
    friend class AndroidAudioPluginParameter;

    aap::PluginInstance *native;
    int32_t aap_midi_in_port{-1}, aap_midi_out_port{-1};
    uint8_t midi_output_store[4096];
    uint32_t midi_buffer_size{4096};
    int sample_rate;
    std::map<int32_t,int32_t> portMapAapToJuce{};
    void preProcessBuffers(AudioBuffer<float> &audioBuffer, MidiBuffer &midiMessages);
    void postProcessBuffers(AudioBuffer<float> &buffer, MidiBuffer &midiMessages);

    bool parameterValueChanged(AndroidAudioPluginParameter* parameter, float newValue);

public:

    AndroidAudioPluginInstance(aap::PluginInstance *nativePlugin);
    ~AndroidAudioPluginInstance() override;

    inline const String getName() const override {
        return native->getPluginInformation()->getDisplayName();
    }

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;

    void releaseResources() override;

    void processBlock(AudioBuffer<float> &audioBuffer, MidiBuffer &midiMessages) override;

    double getTailLengthSeconds() const override;

    bool hasMidiPort(bool isInput) const;

    inline bool acceptsMidi() const override {
        return hasMidiPort(true);
    }

    inline bool producesMidi() const override {
        return hasMidiPort(false);
    }

    AudioProcessorEditor *createEditor() override;

    inline bool hasEditor() const override {
        return native->getPluginInformation()->hasEditor();
    }

    inline int getNumPrograms() override {
        return native->getStandardExtensions().getPresetCount();
    }

    inline int getCurrentProgram() override {
        return native->getStandardExtensions().getCurrentPresetIndex();
    }

    inline void setCurrentProgram(int index) override {
        native->getStandardExtensions().setCurrentPresetIndex(index);
    }

    inline const String getProgramName(int index) override {
        return native->getStandardExtensions().getPresetName(index);
    }

    inline void changeProgramName(int index, const String &newName) override {
        // LAMESPEC: this shoud not exist.
        // AudioUnit implementation causes assertion failure (not implemented).
        // JUCE VST3 implementation ignores it.
        // We would just follow JUCE VST3 way.
    }

    inline void getStateInformation(juce::MemoryBlock &destData) override {
        auto state = native->getStandardExtensions().getState();
        destData.setSize((size_t) state.data_size);
        destData.copyFrom(state.data, 0, (size_t) state.data_size);
    }

    inline void setStateInformation(const void *data, int sizeInBytes) override {
        native->getStandardExtensions().setState(data, (size_t) sizeInBytes);
    }

    void fillInPluginDescription(PluginDescription &description) const override;
};

class AndroidAudioPluginParameter : public juce::AudioParameterFloat {
    friend class AndroidAudioPluginInstance;

    int aap_parameter_id;
    AndroidAudioPluginInstance *instance;
    const aap::ParameterInformation* impl;

    AndroidAudioPluginParameter(int aapParameterId, AndroidAudioPluginInstance* audioPluginInstance, const aap::ParameterInformation* parameterInfo)
            :  juce::AudioParameterFloat(String{parameterInfo->getId()}, parameterInfo->getName(),
                                         static_cast<float>(parameterInfo->getMinimumValue()),
                                         static_cast<float>(parameterInfo->getMaximumValue()),
                                         static_cast<float>(parameterInfo->getDefaultValue())),
               aap_parameter_id(aapParameterId), instance(audioPluginInstance), impl(parameterInfo)
    {
    }

public:
    int getAAPParameterId() const { return aap_parameter_id; }
    void valueChanged(float newValue) override;
};

class AndroidAudioPluginFormat : public juce::AudioPluginFormat {
    aap::PluginListSnapshot plugin_list_snapshot;
    aap::PluginClientConnectionList* plugin_client_connections;
    std::unique_ptr<aap::PluginClient> android_host;
    OwnedArray<PluginDescription> juce_plugin_descs;
    HashMap<const aap::PluginInformation *, PluginDescription *> cached_descs;

    const aap::PluginInformation *findPluginInformationFrom(const PluginDescription &desc);

public:
    AndroidAudioPluginFormat();

    ~AndroidAudioPluginFormat() override;

    inline String getName() const override {
        return "AAP";
    }

    void findAllTypesForFile(OwnedArray <PluginDescription> &results,
                             const String &fileOrIdentifier) override;

    inline bool fileMightContainThisPluginType(const String &fileOrIdentifier) override {
        // FIXME: limit to aap_metadata.xml ?
        return true;
    }

    inline String getNameOfPluginFromIdentifier(const String &fileOrIdentifier) override {
        auto pluginInfo = aap::PluginListSnapshot::queryServices().getPluginInformation(fileOrIdentifier.toStdString());
        return pluginInfo != nullptr ? String(pluginInfo->getDisplayName()) : String();
    }

    inline bool pluginNeedsRescanning(const PluginDescription &description) override {
        // This is not a concept for AAP. Android service information cannot be retrieved without querying.
        return true;
    }

    inline bool doesPluginStillExist(const PluginDescription &description) override {
        auto list = aap::PluginListSnapshot::queryServices();
        return list.getPluginInformation(description.fileOrIdentifier.toRawUTF8()) != nullptr;
    }

    inline bool canScanForPlugins() const override {
        return true;
    }

    StringArray searchPathsForPlugins(const FileSearchPath &directoriesToSearch,
                                      bool recursive,
                                      bool allowPluginsWhichRequireAsynchronousInstantiation = false) override;

    // Note:
    // Unlike desktop system, it is not practical to either look into file systems
    // on Android. And it is simply impossible to "enumerate" asset directories.
    // Therefore we simply return empty list.
    FileSearchPath getDefaultLocationsToSearch() override;

    inline bool isTrivialToScan() const override { return false; }

    inline void createPluginInstance(const PluginDescription &description,
                              double initialSampleRate,
                              int initialBufferSize,
                              PluginCreationCallback callback) override;

protected:
    inline bool requiresUnblockedMessageThreadDuringCreation(
            const PluginDescription &description) const noexcept override {
        // Android bindService() callback needs main thread, thus it must not be blocking.
        return true;
    }
};

} // namespace
