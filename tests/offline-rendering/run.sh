#!/bin/sh
# Builds the actual wrapper and JUCE modules natively on macOS, and runs them against a probe processor.
# JUCE_MODULES selects the JUCE modules directory. AAP_JUCE_TEST_BUILD keeps the JUCE objects between runs.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(CDPATH= cd -- "$here/../.." && pwd)
aapcore=${AAP_CORE_DIR:-$repo/../aap-core}
modules=${JUCE_MODULES:-$repo/../aap-juce-dexed/external/dexed/libs/JUCE/modules}
ndk=${ANDROID_NDK_HOME:-$(ls -d "$HOME"/Library/Android/sdk/ndk/* | sort -V | tail -1)}
build=${AAP_JUCE_TEST_BUILD:-}
if [ -z "$build" ]; then
    build=$(mktemp -d "${TMPDIR:-/tmp}/aap-juce-offline.XXXXXX")
    trap 'rm -rf "$build"' EXIT
fi
mkdir -p "$build/jni"
# The wrapper is written for the Android jni.h, which differs from the one in JDKs.
cp "$ndk"/toolchains/llvm/prebuilt/*/sysroot/usr/include/jni.h "$build/jni/jni.h"
cat > "$build/compat.h" <<'HEADER'
#pragma once
#include <ctime>
inline int clock_nanosleep(clockid_t, int, const timespec* delay, timespec* remaining) {
    return nanosleep(delay, remaining);
}
HEADER
names="juce_core juce_events juce_data_structures juce_graphics juce_gui_basics juce_gui_extra juce_audio_basics juce_audio_processors"
defines="-DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DNDEBUG=1 -DJUCE_STANDALONE_APPLICATION=0 -DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0 -DJUCE_PLUGINHOST_AU=0 -DJUCE_PLUGINHOST_VST3=0"
defines="$defines -DJUCEAAP_ENABLED=1 -DJUCEAAP_HAVE_AUDIO_PLAYHEAD_NEW_POSITION_INFO=1 -DJUCEAAP_AUDIO_PROCESSOR_CHANGE_DETAILS_UNAVAILABLE=0"
for name in $names; do defines="$defines -DJUCE_MODULE_AVAILABLE_$name=1"; done
common="-std=c++20 -w -g -mmacosx-version-min=13.0 $defines -I$build/jni -I$modules"
objects=
for name in $names; do
    object=$build/$name.o
    objects="$objects $object"
    [ -f "$object" ] || echo "$name"
done | xargs -n1 -P2 -I{} "${CXX:-clang++}" $common -x objective-c++ -c "$modules/{}/{}.mm" -o "$build/{}.o"
for name in $names; do objects="$objects $build/$name.o"; done
"${CXX:-clang++}" $common -include jni.h -include "$build/compat.h" -I"$aapcore/include" -I"$aapcore/external/cmidi2" -I"$repo/aap-modules" \
    "$here/offline-rendering-test.cpp" "$repo/aap-modules/aap_audio_processors/aap_audio_processors.cpp" $objects \
    -framework Cocoa -framework IOKit -framework Accelerate -framework CoreAudio -framework CoreMIDI -framework AudioToolbox \
    -framework QuartzCore -framework Carbon -framework Security -framework Metal -framework MetalKit -framework WebKit \
    -framework CoreAudioKit -framework AVFoundation -framework CoreVideo -framework CoreImage -framework DiscRecording \
    -o "$build/offline-rendering-test"
"$build/offline-rendering-test"

# The host side, against the actual aap-core hosting code.
# The module's GUI and desktop scanning code needs these to compile here; they do not run.
cat > "$build/host-compat.h" <<'HEADER'
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
namespace juce { class AndroidViewComponent : public Component { public: void setView(void*) {} }; }
#define fillPluginDescriptionFromNative fillPluginDescriptionFromNativeCommon
HEADER
hosting=$aapcore/androidaudioplugin/src/main/cpp/core/hosting
aapxs=$aapcore/androidaudioplugin/src/main/cpp/core/aapxs
core_flags="-std=c++20 -w -g -include $build/compat.h -include $aapcore/tests/native/rt-platform-stub.h -I$aapcore/include -I$aapcore/external/cmidi2 -I$hosting -I$aapxs"
core_objects=
for source in PluginInstance PluginInstance.Local PluginInstance.Remote buffer-layout PluginHost PluginHost.Client PluginHost.Service PluginInformation \
    plugin-information-registry plugin-connections AAPXSMidi2RecipientSession AAPXSMidi2InitiatorSession aap_midi2_helper aapxs-transport recipient-aapxs realtime; do
    object=$build/core-$source.o
    [ -f "$object" ] || "${CXX:-clang++}" $core_flags -c "$hosting/$source.cpp" -o "$object"
    core_objects="$core_objects $object"
done
for source in aapxs-runtime standard-extensions parameters-aapxs presets-aapxs state-aapxs midi-aapxs urid-aapxs gui-aapxs buses-aapxs offline-rendering-aapxs typed-aapxs; do
    object=$build/core-$source.o
    [ -f "$object" ] || "${CXX:-clang++}" $core_flags -c "$aapxs/$source.cpp" -o "$object"
    core_objects="$core_objects $object"
done
"${CXX:-clang++}" $common -DJUCEAAP_HOSTED_PARAMETER=1 -DJUCEAAP_USE_UNIQUE_ID=1 -include jni.h -include "$build/compat.h" -include "$aapcore/tests/native/rt-platform-stub.h" -include "$build/host-compat.h" \
    -I"$aapcore/include" -I"$aapcore/external/cmidi2" -I"$hosting" -I"$repo/aap-modules" -I"$repo/aap-modules/aap_audio_plugin_client" \
    "$here/offline-rendering-host-test.cpp" "$repo/aap-modules/aap_audio_plugin_client/aap_audio_plugin_client.cpp" $core_objects $objects \
    -framework Cocoa -framework IOKit -framework Accelerate -framework CoreAudio -framework CoreMIDI -framework AudioToolbox \
    -framework QuartzCore -framework Carbon -framework Security -framework Metal -framework MetalKit -framework WebKit \
    -framework CoreAudioKit -framework AVFoundation -framework CoreVideo -framework CoreImage -framework DiscRecording \
    -o "$build/offline-rendering-host-test"
"$build/offline-rendering-host-test"
