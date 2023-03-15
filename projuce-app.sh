#!/bin/bash

CURDIR="$( cd `dirname $0` >/dev/null 2>&1 && pwd )"
if [ "`uname`" == 'Darwin' ] ; then
READLINK=greadlink # brew install coreutils
else
READLINK=readlink
fi
if [ -z "$MACAPPNAME" ] ; then
MACAPPNAME=$APPNAME
fi

if ! [ -f $1 ] ; then
    echo "usage: APPNAME=xyz PROJUCER=/path/to/JUCE/Projucer projuce-app.sh [.jucer file]"
    echo "Missing .jucer file (or it does not exist)."
    exit 2
fi

echo "projuce-app.sh: APPNAME: $APPNAME / MAPAPPNAME: $MACAPPNAME"

SRCFILE=`$READLINK -f $1` >/dev/null
SRCDIR=`dirname $SRCFILE` >/dev/null

echo "Entering $SRCDIR ..."
cd $SRCDIR

$PROJUCER --resave "`basename $1`" || exit 1

# We can skip this for update-aap-metadata (which rarely happens in the build structures though, mostly for vital)
if [ -d Builds/Android ] ; then

# If a top-level `local.properties` exists, then copy it into the generated Android project.
if [ -f ../../local.properties ] ; then
	cp ../../local.properties Builds/Android/ || exit 1
else
	echo "projuce-app.sh: building '`basename $1`' without local.properties."
fi

# Fixup Android project
echo "rootProject.name='$APPNAME'" > Builds/Android/settings.gradle
echo "include ':app'" >> Builds/Android/settings.gradle
cp $CURDIR/sample-project.gradle.properties Builds/Android/gradle.properties
cp $CURDIR/sample-project.libs.versions.toml Builds/Android/gradle/libs.versions.toml
cp $CURDIR/sample-project.gradle-wrapper.properties Builds/Android/gradle/wrapper/gradle-wrapper.properties
cp $CURDIR/sample-project.proguard-rules.pro Builds/Android/app/proguard-rules.pro
# Projucer is too inflexible to generate required content for top-level file.
cp $CURDIR/sample-project.build.gradle Builds/Android/build.gradle
# app/build.gradle needs further tweaks.
sed -i "" -e "s/defaultConfig {/defaultConfig {\n        proguardFiles \"proguard-rules.pro\"/" -- Builds/Android/app/build.gradle
sed -i "" -e "s/ANDROID_ARM_MODE/INVALIDATED_ANDROID_ARM_MODE/" -- Builds/Android/app/build.gradle
sed -i "" -e "s/c++_static/c++_shared/" -- Builds/Android/app/build.gradle
sed -i "" -e "s/repositories {/buildFeatures { prefab true }\n    repositories {/" -- Builds/Android/app/build.gradle

# app/CMakeLists.txt needs further tweaks
echo "find_package(androidaudioplugin REQUIRED CONFIG)" >> Builds/Android/app/CMakeLists.txt
echo "target_link_libraries(\${BINARY_NAME} androidaudioplugin::androidaudioplugin)" >> Builds/Android/app/CMakeLists.txt
echo "# We NEVER ALLOW JUCE audio plugin standalone apps to automatically open MIDI devices." >> Builds/Android/app/CMakeLists.txt
echo "# It will open virtual MidiDeviceServices, including AAP MidiDeviceServices!!" >> Builds/Android/app/CMakeLists.txt 
echo "add_compile_definitions(JUCE_DONT_AUTO_OPEN_MIDI_DEVICES_ON_MOBILE=1)" >> Builds/Android/app/CMakeLists.txt

# copy aap_metadata.xml once Builds/Android is created.
# Projucer behavior is awkward. It generates "debug" and "release" directories, and any other common resources are ignored.
if [ $AAP_METADATA_XML_SOURCE ] ; then
echo "SPECIFIED METADATA XML is: $AAP_METADATA_XML_SOURCE"
mkdir -p Builds/Android/app/src/debug/res/xml ;
cp $AAP_METADATA_XML_SOURCE Builds/Android/app/src/debug/res/xml/aap_metadata.xml || exit 1 ;
mkdir -p Builds/Android/app/src/release/res/xml ;
cp $AAP_METADATA_XML_SOURCE Builds/Android/app/src/release/res/xml/aap_metadata.xml || exit 1 ;
fi

if [ -f Builds/Android/app/src/debug/res/xml/aap_metadata.xml ] ; then
if [ ! -z "$ENABLE_MIDI_DEVICE_SERVICE" ] ; then
MANIFEST_TEMPLATE=$CURDIR/template.AndroidManifest-midi-enabled.xml
cp $CURDIR/template.midi_device_info.xml midi_device_info.xml
sed -i "" -e "s/@@@APPNAME@@@/$APPNAME/g" -- midi_device_info.xml || exit 1
cp midi_device_info.xml Builds/Android/app/src/debug/res/xml
cp midi_device_info.xml Builds/Android/app/src/release/res/xml
else
MANIFEST_TEMPLATE=$CURDIR/template.AndroidManifest.xml
fi
else
MANIFEST_TEMPLATE=$CURDIR/template.AndroidManifest-host.xml

# copy additional host sources
mkdir -p Builds/Android/app/src/main/java/org/androidaudioplugin/juce/
cp $CURDIR/java/org/androidaudioplugin/juce/JuceAppInitializer.java Builds/Android/app/src/main/java/org/androidaudioplugin/juce/
fi

APPNAMELOWER=`echo $APPNAME | tr [:upper:] [:lower:] | tr - _`

# Projucer is too inflexible to generate required content.
echo "Manifest template is $MANIFEST_TEMPLATE"
cp $MANIFEST_TEMPLATE Builds/Android/app/src/main/AndroidManifest.xml
sed -i "" -e "s/@@@ PACKAGE_NAME @@@/org.androidaudioplugin.ports.juce.$APPNAMELOWER/" -- Builds/Android/app/src/main/AndroidManifest.xml || exit 1

echo "-------- Post-projucer file list for $APPNAME: --------"
find .


fi # -d Builds/Android
