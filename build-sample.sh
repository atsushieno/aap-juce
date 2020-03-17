#!/bin/sh

CURDIR="$( cd `dirname $0` >/dev/null 2>&1 && pwd )"
ANDROID_SDK_OVERRIDE=$HOME/Android/Sdk

SRCDIR=`dirname $1` >/dev/null

echo "usage: APPNAME=xyz PROJUCER=/path/to/JUCE/Projucer build-sample.sh [.jucer file]"

echo "Entering $CURDIR ..."
cd $SRCDIR

$PROJUCER --resave $APPNAME.jucer || exit 1
# sed -e "s/#define JUCE_PROJUCER_VERSION/\\/\\/\$1/" JuceLibraryCode/AppConfig.h> JuceLibraryCode/tmpcfg.txt || eixt 2
# mv JuceLibraryCode/tmpcfg.txt JuceLibraryCode/AppConfig.h || exit 3

make -C Builds/LinuxMakefile || exit 4

APPNAME=$APPNAME $CURDIR/fixup-project.sh || exit 5

# There is no way to generate those files in Projucer.
cp $CURDIR/samples/sample-project.gradle.properties Builds/Android/gradle.properties

# Projucer is too inflexible to generate required content.
cp $CURDIR/samples/sample-project.build.gradle Builds/Android/build.gradle

sed -e "s/project (\"$APPNAME\" C CXX)/project (\"$APPNAME\" C CXX)\n\nlink_directories (\n  \$\{CMAKE_CURRENT_SOURCE_DIR\}\/..\/..\/..\/..\/..\/..\/external\/android-audio-plugin-framework\/build\/native\/androidaudioplugin \n  \$\{CMAKE_CURRENT_SOURCE_DIR\}\/..\/..\/..\/..\/..\/..\/external\/android-audio-plugin-framework\/build\/native\/androidaudioplugin-lv2) \n\n/" Builds/CLion/CMakeLists.txt  > Builds/CLion/CMakeLists.txt.patched
mv Builds/CLion/CMakeLists.txt.patched Builds/CLion/CMakeLists.txt

# This does not work on bitrise (Ubuntu 16.04) because libfreetype6-dev is somehow too old and lacks some features that is uncaught. Disabled.
# cd Builds/LinuxMakefile && make && cd ../..

echo "ndk.dir=$ANDROID_SDK_OVERRIDE/ndk/21.0.6113669\nsdk.dir=$ANDROID_SDK_OVERRIDE" > Builds/Android/local.properties

cd Builds/Android && ./gradlew build && cd ../.. || exit 1

echo "exiting $CURDIR..."
cd ..

