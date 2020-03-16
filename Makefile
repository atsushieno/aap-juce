
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
build-samples: create-patched-pluginhost
	echo "PROJUCER is at $(PROJUCER_BIN)"
	APPNAME=AudioPluginHost PROJUCER=$(PROJUCER_BIN) ANDROID_SDK_ROOT=$(ANDROID_SDK_ROOT) ./build-sample.sh samples/AudioPluginHost/AudioPluginHost.jucer

.PHONY:
create-patched-pluginhost: samples/AudioPluginHost/.stamp 

samples/AudioPluginHost/.stamp: \
		external/juce_emscripten/extras/AudioPluginHost/** \
		samples/juceaaphost.patch \
		samples/override.AudioPluginHost.jucer \
		samples/sample-project.*
	if [ -d samples/AudioPluginHost-AutoBackup ]; then \
		echo "make sure that you don't retain backup as 'AudioPluginHost-AutoBackup'. It is a backup to ensure that you have a clean AudioPluginHost. If you really need a backup then give a different name." && exit 2 ; \
	fi
	if [ -d samples/AudioPluginHost ]; then \
		mv samples/AudioPluginHost samples/AudioPluginHost-AutoBackup ; \
	fi
	cp -R external/juce_emscripten/extras/AudioPluginHost samples/AudioPluginHost
	cd samples/AudioPluginHost && patch -i ../juceaaphost.patch -p6
	cp samples/override.AudioPluginHost.jucer samples/AudioPluginHost/AudioPluginHost.jucer
	touch samples/AudioPluginHost/.stamp
