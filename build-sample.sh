#!/bin/bash

CURDIR="$( cd `dirname $0` >/dev/null 2>&1 && pwd )"
ANDROID_SDK_OVERRIDE=$HOME/Android/Sdk
NDK_VERSION=21.2.6472646
MINIMIZE_INTERMEDIATES=0
if [ '$GRADLE_TASK' == '' ] ; then
GRADLE_TASK=build
fi
if [ `uname` == 'Darwin' ] ; then
READLINK=greadlink # brew install coreutils
else
READLINK=readlink
fi
if [ '$MACAPPNAME' == '' ] ; then
MACAPPNAME=$APPNAME
fi



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

echo "build-sample.sh: APPNAME: $APPNAME / MAPAPPNAME: $MACAPPNAME / GRADLE_TASK: $GRADLE_TASK"

SRCFILE=`$READLINK -f $1` >/dev/null
SRCDIR=`dirname $SRCFILE` >/dev/null

echo "Entering $SRCDIR ..."
cd $SRCDIR

$PROJUCER --resave `basename $1` || exit 1
# sed -e "s/#define JUCE_PROJUCER_VERSION/\\/\\/\$1/" JuceLibraryCode/AppConfig.h> JuceLibraryCode/tmpcfg.txt || eixt 2
# mv JuceLibraryCode/tmpcfg.txt JuceLibraryCode/AppConfig.h || exit 3

# If a top-level `local.properties` exists, then copy it into the generated Android project.
if [ -f ../../local.properties ] ; then
	cp ../../local.properties Builds/Android/ || exit 1
else
	echo "build-sample.sh: building '`basename $1`' without local.properties."
fi

if [ `uname` == 'Darwin' ] ; then
if [ $APPNAME == 'AudioPluginHost' ] ; then
	pushd . && cd Builds/MacOSX && xcodebuild -project "$APPNAME.xcodeproj" && popd || exit 4
else
	pushd . && cd Builds/MacOSX && xcodebuild -project "$MACAPPNAME.xcodeproj" -target "$APPNAME - Shared Code" && popd || exit 4
fi
else
	make -C Builds/LinuxMakefile || exit 4
fi

APPNAME=$APPNAME $CURDIR/fixup-project.sh || exit 5
APPNAMELOWER=`echo $APPNAME | tr [:upper:] [:lower:]`

# There is no way to generate those files in Projucer.
cp $CURDIR/sample-project.gradle.properties Builds/Android/gradle.properties

# Projucer is too inflexible to generate required content.
## build.gradle
cp $CURDIR/sample-project.build.gradle Builds/Android/build.gradle
## AndroidManifest.xml (only for plugins)
if [ -f Builds/Android/app/src/debug/res/xml/aap_metadata.xml ] ; then
sed -e "s/@@@ PACKAGE_NAME @@@/org.androidaudioplugin.juceports.$APPNAMELOWER/" $CURDIR/template.AndroidManifest.xml > Builds/Android/app/src/main/AndroidManifest.xml || exit 1
fi

if [ -d Builds/CLion ] ; then
sed -e "s/project (\"$APPNAME\" C CXX)/project (\"$APPNAME\" C CXX)\n\nlink_directories (\n  \$\{CMAKE_CURRENT_SOURCE_DIR\}\/..\/..\/..\/..\/..\/..\/external\/android-audio-plugin-framework\/build\/native\/androidaudioplugin \n  \$\{CMAKE_CURRENT_SOURCE_DIR\}\/..\/..\/..\/..\/..\/..\/external\/android-audio-plugin-framework\/build\/native\/androidaudioplugin-lv2) \n\n/" Builds/CLion/CMakeLists.txt  > Builds/CLion/CMakeLists.txt.patched || exit 1
mv Builds/CLion/CMakeLists.txt.patched Builds/CLion/CMakeLists.txt
fi

echo "sdk.dir=$ANDROID_SDK_ROOT" > $SRCDIR/Builds/Android/local.properties

cd Builds/Android && ./gradlew $GRADLE_TASK && cd ../.. || exit 1

if [ $MINIMIZE_INTERMEDIATES ] ; then
    rm -rf Builds/Android/app/build/intermediates/ ;
    rm -rf Builds/Android/app/.cxx ;
    rm -rf Builds/LinuxMakefile/build/intermediates/
fi

echo "exiting $CURDIR..."
cd ..

