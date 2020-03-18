
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
build-audiopluginhost: create-patched-pluginhost do-build-audiopluginhost
.PHONY:
do-build-audiopluginhost:
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
build-andes: create-patched-andes do-build-andes
.PHONY:
do-build-andes:
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

.PHONY:
build-sarah: create-patched-sarah do-build-sarah
.PHONY:
do-build-sarah:
	echo "PROJUCER is at $(PROJUCER_BIN)"
	APPNAME=SARAH PROJUCER=$(PROJUCER_BIN) ANDROID_SDK_ROOT=$(ANDROID_SDK_ROOT) ./build-sample.sh samples/SARAH/SARAH.jucer
.PHONY:
create-patched-sarah: samples/SARAH/.stamp 
samples/SARAH/.stamp: \
		external/SARAH/** \
		samples/andes-aap.patch \
		samples/override.SARAH.jucer \
		samples/sample-project.*
	./create-patched-juce-app.sh  SARAH  external/SARAH \
		samples/SARAH  ../sarah-aap.patch  0  samples/override.SARAH.jucer

.PHONY:
build-magical8bitplug2: create-patched-magical8bitplug2 do-build-magical8bitplug2
.PHONY:
do-build-magical8bitplug2:
	echo "PROJUCER is at $(PROJUCER_BIN)"
	APPNAME=Magical8bitPlug2 PROJUCER=$(PROJUCER_BIN) ANDROID_SDK_ROOT=$(ANDROID_SDK_ROOT) ./build-sample.sh samples/Magical8bitPlug2/Magical8bitPlug2.jucer
.PHONY:
create-patched-magical8bitplug2: samples/Magical8bitPlug2/.stamp 
samples/Magical8bitPlug2/.stamp: \
		external/Magical8bitPlug2/** \
		samples/andes-aap.patch \
		samples/override.Magical8bitPlug2.jucer \
		samples/sample-project.*
	./create-patched-juce-app.sh  Magical8bitPlug2  external/Magical8bitPlug2 \
		samples/Magical8bitPlug2  ../magical8bitplug2-aap.patch  1  samples/override.Magical8bitPlug2.jucer

.PHONY:
build-dexed: create-patched-dexed do-build-dexed
.PHONY:
do-build-dexed:
	echo "PROJUCER is at $(PROJUCER_BIN)"
	APPNAME=Dexed PROJUCER=$(PROJUCER_BIN) ANDROID_SDK_ROOT=$(ANDROID_SDK_ROOT) ./build-sample.sh samples/dexed/Dexed.jucer
.PHONY:
create-patched-dexed: samples/dexed/.stamp 
samples/dexed/.stamp: \
		external/dexed/** \
		samples/override.Dexed.jucer \
		samples/sample-project.*
	./create-patched-juce-app.sh  Dexed  external/dexed \
		samples/dexed  -  -  samples/override.Dexed.jucer
