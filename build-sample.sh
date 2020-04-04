#!/bin/sh

CURDIR="$( cd `dirname $0` >/dev/null 2>&1 && pwd )"
ANDROID_SDK_OVERRIDE=$HOME/Android/Sdk
NDK_VERSION=21.0.6113669
MINIMIZE_INTERMEDIATES=0

if [ -d $ANDROID_SDK_OVERRIDE ] ; then
    echo "ANDROID_SDK_OVERRIDE: $ANDROID_SDK_OVERRIDE"
    ANDROID_SDK_ROOT=$ANDROID_SDK_OVERRIDE
else
    ANDROID_SDK_ROOT=$ANDROID_SDK_ROOT
fi

if ! [ -d  $ANDROID_SDK_ROOT ] ; then
    echo "ANDROID_SDK_ROOT is not defined."
    exit 1
fi

if ! [ -f $1 ] ; then
    echo "usage: APPNAME=xyz PROJUCER=/path/to/JUCE/Projucer build-sample.sh [.jucer file]"
    echo "Missing .jucer file (or it does not exist)."
    exit 2
fi

SRCFILE=`readlink -f $1` >/dev/null
SRCDIR=`dirname $SRCFILE` >/dev/null

echo "Entering $SRCDIR ..."
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

# Some CI servers have only "ndk-bundle" ...
if [ -d $ANDROID_SDK_ROOT/ndk-bundle ] ; then
    echo "ndk.dir=$ANDROID_SDK_ROOT/ndk-bundle\nsdk.dir=$ANDROID_SDK_ROOT" > $SRCDIR/Builds/Android/local.properties
else
    echo "ndk.dir=$ANDROID_SDK_ROOT/ndk/$NDK_VERSION\nsdk.dir=$ANDROID_SDK_ROOT" > $SRCDIR/Builds/Android/local.properties
fi

echo "ANDROID_SDK_ROOT: $ANDROID_SDK_ROOT"
ls $ANDROID_SDK_ROOT

cd Builds/Android && ./gradlew build && cd ../.. || exit 1

if [ $MINIMIZE_INTERMEDIATES ] ; then
    rm -rf Builds/Android/app/build/intermediates/
    rm -rf Builds/Android/app/.cxx
    rm -rf Builds/LinuxMakefile/build/intermediates/
fi

echo "exiting $CURDIR..."
cd ..

