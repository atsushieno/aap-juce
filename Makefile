
JUCE_DIR=$(shell pwd)/external/juce_emscripten
PROJUCER_BIN=$(JUCE_DIR)/extras/Projucer/Builds/LinuxMakefile/build/Projucer

.PHONY:
all: build

.PHONY:
build: prepare build-aap build-samples

.PHONY:
prepare: build-projucer

.PHONY:
build-projucer: $(PROJUCER_BIN)

$(PROJUCER_BIN):
	make -C $(JUCE_DIR)/extras/Projucer/Builds/LinuxMakefile

.PHONY:
build-aap:
	cd external/android-audio-plugin-framework && make

.PHONY:
build-samples: build-audiopluginhost build-andes

.PHONY:
build-audiopluginhost: create-patched-pluginhost
	echo "PROJUCER is at $(PROJUCER_BIN)"
	APPNAME=AudioPluginHost PROJUCER=$(PROJUCER_BIN) ANDROID_SDK_ROOT=$(ANDROID_SDK_ROOT) SKIP_METADATA_GENERATOR=1 ./build-sample.sh samples/AudioPluginHost/AudioPluginHost.jucer

.PHONY:
create-patched-pluginhost: samples/AudioPluginHost/.stamp 

samples/AudioPluginHost/.stamp: \
		external/juce_emscripten/extras/AudioPluginHost/** \
		samples/juceaaphost.patch \
		samples/override.AudioPluginHost.jucer \
		samples/sample-project.*
	./create-patched-juce-app.sh  AudioPluginHost  external/juce_emscripten/extras/AudioPluginHost \
		samples/AudioPluginHost  ../juceaaphost.patch  6  samples/override.AudioPluginHost.jucer


.PHONY:
build-andes: create-patched-andes
	echo "PROJUCER is at $(PROJUCER_BIN)"
	APPNAME=Andes_1 PROJUCER=$(PROJUCER_BIN) ANDROID_SDK_ROOT=$(ANDROID_SDK_ROOT) ./build-sample.sh samples/andes/Andes_1.jucer

.PHONY:
create-patched-andes: samples/andes/.stamp 

samples/andes/.stamp: \
		external/andes/** \
		samples/andes-aap.patch \
		samples/override.Andes-1.jucer \
		samples/sample-project.*
	./create-patched-juce-app.sh  Andes_1  external/andes \
		samples/andes  ../andes-aap.patch  0  samples/override.Andes-1.jucer

