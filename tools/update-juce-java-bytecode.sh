#!/bin/bash
#
# Regenerates the dex byte-code that JUCE embeds for its Java classes with native methods, from the
# Java sources in the (patched) JUCE tree. It automates what juce_core/native/java/README.txt describes.
#
# JUCE loads such a class (e.g. com.rmsl.juce.ComponentPeerView) from the app class loader if the APK
# contains it, and otherwise from the embedded byte-code, through a class loader that belongs to each
# JUCE library. The latter is what lets more than one JUCE plugin library live in one process: each
# library registers its native methods to its own copy of the class. Therefore such an app must not
# compile those Java sources, and the embedded byte-code has to contain our patches to them (e.g.
# juce-component-peer-view-touch.patch and component-peer-view-unregister-lifecycle-callbacks.patch).
# An app with one JUCE plugin library can keep compiling the patched Java sources instead. See
# "More Than One JUCE Plugin Library in an App" in docs/JUCE_GUI_SUPPORT.md.
#
# Usage: update-juce-java-bytecode.sh JUCE_DIR
#
# It needs javac, python3, and an Android SDK (ANDROID_SDK_ROOT or ANDROID_HOME) with build-tools
# and a platform installed.

set -e

if [ $# -ne 1 ] ; then
    echo "Usage: $0 JUCE_DIR" >&2
    exit 1
fi

JUCE_DIR=$(cd "$1" && pwd)

# "Java source:C++ source:array name", relative to $JUCE_DIR/modules.
ENTRIES="
juce_gui_basics/native/java/app/com/rmsl/juce/ComponentPeerView.java:juce_gui_basics/native/juce_Windowing_android.cpp:javaComponentPeerView
"

# The same as what JUCE uses for these classes.
MIN_SDK=16

SDK_DIR=${ANDROID_SDK_ROOT:-$ANDROID_HOME}
if [ -z "$SDK_DIR" ] ; then
    if [ -d ~/Library/Android/sdk ] ; then
        SDK_DIR=~/Library/Android/sdk
    else
        SDK_DIR=~/Android/Sdk
    fi
fi
BUILD_TOOLS_DIR=$(ls -d "$SDK_DIR"/build-tools/* | sort -V | tail -n 1)
# Skip codename platforms (e.g. android-UpsideDownCake), which sort -V would pick over the numbered ones.
ANDROID_JAR=$(ls "$SDK_DIR"/platforms/android-[0-9]*/android.jar | sort -V | tail -n 1)
if [ ! -x "$BUILD_TOOLS_DIR/d8" ] || [ ! -f "$ANDROID_JAR" ] ; then
    echo "Android SDK build-tools or platform was not found in $SDK_DIR" >&2
    exit 1
fi

WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

for entry in $ENTRIES ; do
    JAVA_SOURCE=$JUCE_DIR/modules/$(echo $entry | cut -d: -f1)
    CPP_SOURCE=$JUCE_DIR/modules/$(echo $entry | cut -d: -f2)
    ARRAY_NAME=$(echo $entry | cut -d: -f3)
    echo "Updating $ARRAY_NAME in $CPP_SOURCE from $JAVA_SOURCE"

    rm -rf "$WORK_DIR/classes" "$WORK_DIR/dex"
    mkdir -p "$WORK_DIR/classes" "$WORK_DIR/dex"
    javac -Xlint:-options --release 8 -classpath "$ANDROID_JAR" -d "$WORK_DIR/classes" "$JAVA_SOURCE"
    "$BUILD_TOOLS_DIR/d8" --release --min-api $MIN_SDK --lib "$ANDROID_JAR" --output "$WORK_DIR/dex" \
        $(find "$WORK_DIR/classes" -name '*.class')
    # -n omits the file name and timestamp, so that the output is reproducible.
    gzip -n -9 -c "$WORK_DIR/dex/classes.dex" > "$WORK_DIR/classes.dex.gz"

    python3 - "$CPP_SOURCE" "$ARRAY_NAME" "$WORK_DIR/classes.dex.gz" <<'EOF'
import re, sys
cpp_path, array_name, gz_path = sys.argv[1:]
data = open(gz_path, 'rb').read()
src = open(cpp_path, 'rb').read().decode('utf-8')
newline = '\r\n' if '\r\n' in src else '\n'
lines = ['  ' + ', '.join('0x%02x' % b for b in data[i:i + 12]) + ',' for i in range(0, len(data), 12)]
pattern = re.compile(r'(\b' + re.escape(array_name) + r'\[\]\s*=?\s*\{)(.*?)(\};)', re.S)
if not pattern.search(src):
    sys.exit('%s was not found in %s' % (array_name, cpp_path))
src = pattern.sub(lambda m: m.group(1) + newline + newline.join(lines) + newline + m.group(3), src, count=1)
open(cpp_path, 'wb').write(src.encode('utf-8'))
EOF
done
