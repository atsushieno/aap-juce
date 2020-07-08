
MINIMIZE_INTERMEDIATES=0
NDK_VERSION=21.2.6472646
JUCE_DIR=$(shell pwd)/external/juce_emscripten
PROJUCER_BIN_LINUX=$(JUCE_DIR)/extras/Projucer/Builds/LinuxMakefile/build/Projucer
PROJUCER_BIN_DARWIN=$(JUCE_DIR)/extras/Projucer/Builds/MacOSX/build/Debug/Projucer.app/Contents/MacOS/Projucer
GRADLE_BUILD_TYPE=Release

ifeq ($(shell uname), Linux)
	PROJUCER_BIN=$(PROJUCER_BIN_LINUX)
else
ifeq ($(shell uname), Darwin)
	PROJUCER_BIN=$(PROJUCER_BIN_DARWIN)
else
	PROJUCER_BIN=___error___
endif
endif


.PHONY:
all: build

.PHONY:
build: prepare build-aap build-samples

.PHONY:
prepare: build-projucer

.PHONY:
build-projucer: $(PROJUCER_BIN)

$(PROJUCER_BIN_LINUX):
	make -C $(JUCE_DIR)/extras/Projucer/Builds/LinuxMakefile
	if [ $(MINIMIZE_INTERMEDIATES) ] ; then \
		rm -rf $(JUCE_DIR)/extras/Projucer/Builds/LinuxMakefile/build/intermediate/ ; \
	fi

$(PROJUCER_BIN_DARWIN):
	xcodebuild -project $(JUCE_DIR)/extras/Projucer/Builds/MacOSX/Projucer.xcodeproj
	if [ $(MINIMIZE_INTERMEDIATES) ] ; then \
		rm -rf $(JUCE_DIR)/extras/Projucer/Builds/MacOSX/build/intermediate/ ; \
	fi

.PHONY:
build-aap:
	cd external/android-audio-plugin-framework && make MINIMIZE_INTERMEDIATES=$(MINIMIZE_INTERMEDIATES)

.PHONY:
build-samples: build-audiopluginhost build-andes build-sarah build-magical8bitplug2 build-dexed build-obxd

.PHONY:
dist:
	mkdir -p release-builds
	mv  samples/AudioPluginHost/Builds/Android/app/build/outputs/apk/release_/release/app-release_-release.apk  release-builds/AudioPluginHost-release.apk
	mv  samples/andes/Builds/Android/app/build/outputs/apk/release_/release/app-release_-release.apk   release-builds/andes-release.apk
	mv  samples/SARAH/Builds/Android/app/build/outputs/apk/release_/release/app-release_-release.apk   release-builds/SARAH-release.apk
	mv  samples/dexed/Builds/Android/app/build/outputs/apk/release_/release/app-release_-release.apk  release-builds/dexed-release.apk
	mv  samples/Magical8bitPlug2/Builds/Android/app/build/outputs/apk/release_/release/app-release_-release.apk  release-builds/Magical8bitPlug2-release.apk


.PHONY:
build-audiopluginhost: create-patched-pluginhost do-build-audiopluginhost
.PHONY:
do-build-audiopluginhost:
	echo "PROJUCER is at $(PROJUCER_BIN)"
	NDK_VERSION=$(NDK_VERSION) APPNAME=AudioPluginHost PROJUCER=$(PROJUCER_BIN) ANDROID_SDK_ROOT=$(ANDROID_SDK_ROOT) SKIP_METADATA_GENERATOR=1 GRADLE_BUILD_TYPE=$(GRADLE_BUILD_TYPE) ./build-sample.sh samples/AudioPluginHost/AudioPluginHost.jucer

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
	NDK_VERSION=$(NDK_VERSION) APPNAME=Andes_1 PROJUCER=$(PROJUCER_BIN) ANDROID_SDK_ROOT=$(ANDROID_SDK_ROOT) GRADLE_BUILD_TYPE=$(GRADLE_BUILD_TYPE) ./build-sample.sh samples/andes/Andes_1.jucer
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
	NDK_VERSION=$(NDK_VERSION) APPNAME=SARAH PROJUCER=$(PROJUCER_BIN) ANDROID_SDK_ROOT=$(ANDROID_SDK_ROOT) GRADLE_BUILD_TYPE=$(GRADLE_BUILD_TYPE) ./build-sample.sh samples/SARAH/SARAH.jucer
.PHONY:
create-patched-sarah: samples/SARAH/.stamp 
samples/SARAH/.stamp: \
		external/SARAH/** \
		samples/sarah-aap.patch \
		samples/override.SARAH.jucer \
		samples/sample-project.*
	./create-patched-juce-app.sh  SARAH  external/SARAH \
		samples/SARAH  ../sarah-aap.patch  0  samples/override.SARAH.jucer

.PHONY:
build-magical8bitplug2: create-patched-magical8bitplug2 do-build-magical8bitplug2
.PHONY:
do-build-magical8bitplug2:
	echo "PROJUCER is at $(PROJUCER_BIN)"
	NDK_VERSION=$(NDK_VERSION) APPNAME=Magical8bitPlug2 PROJUCER=$(PROJUCER_BIN) ANDROID_SDK_ROOT=$(ANDROID_SDK_ROOT) GRADLE_BUILD_TYPE=$(GRADLE_BUILD_TYPE) ./build-sample.sh samples/Magical8bitPlug2/Magical8bitPlug2.jucer
.PHONY:
create-patched-magical8bitplug2: samples/Magical8bitPlug2/.stamp 
samples/Magical8bitPlug2/.stamp: \
		external/Magical8bitPlug2/** \
		samples/magical8bitplug2-aap.patch \
		samples/override.Magical8bitPlug2.jucer \
		samples/sample-project.*
	./create-patched-juce-app.sh  Magical8bitPlug2  external/Magical8bitPlug2 \
		samples/Magical8bitPlug2  ../magical8bitplug2-aap.patch  1  samples/override.Magical8bitPlug2.jucer

.PHONY:
build-dexed: create-patched-dexed do-build-dexed
.PHONY:
do-build-dexed:
	echo "PROJUCER is at $(PROJUCER_BIN)"
	MINIMIZE_INTERMEDIATES=$(MINIMIZE_INTERMEDIATES) NDK_VERSION=$(NDK_VERSION) APPNAME=Dexed PROJUCER=$(PROJUCER_BIN) ANDROID_SDK_ROOT=$(ANDROID_SDK_ROOT) GRADLE_BUILD_TYPE=$(GRADLE_BUILD_TYPE) ./build-sample.sh samples/dexed/Dexed.jucer
.PHONY:
create-patched-dexed: samples/dexed/.stamp 
samples/dexed/.stamp: \
		external/dexed/** \
		samples/override.Dexed.jucer \
		samples/sample-project.*
	./create-patched-juce-app.sh  Dexed  external/dexed \
		samples/dexed  -  -  samples/override.Dexed.jucer

.PHONY:
build-obxd: create-patched-obxd do-build-obxd
.PHONY:
do-build-obxd:
	echo "PROJUCER is at $(PROJUCER_BIN)"
	NDK_VERSION=$(NDK_VERSION) APPNAME=OB_Xd PROJUCER=$(PROJUCER_BIN) ANDROID_SDK_ROOT=$(ANDROID_SDK_ROOT) GRADLE_BUILD_TYPE=$(GRADLE_BUILD_TYPE) ./build-sample.sh samples/OB-Xd/OB-Xd.jucer
.PHONY:
create-patched-obxd: samples/OB-Xd/.stamp 
samples/OB-Xd/.stamp: \
		external/OB-Xd/** \
		samples/andes-aap.patch \
		samples/override.OB-Xd.jucer \
		samples/sample-project.*
	./create-patched-juce-app.sh  OB_Xd  external/OB-Xd \
		samples/OB-Xd  ../obxd-aap.patch  0  samples/override.OB-Xd.jucer

