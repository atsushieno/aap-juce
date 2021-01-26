#!/bin/bash

CURDIR="$( cd `dirname $0` >/dev/null 2>&1 && pwd )"
if [ `uname` == 'Darwin' ] ; then
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

$PROJUCER --resave `basename $1` || exit 1

# If a top-level `local.properties` exists, then copy it into the generated Android project.
if [ -f ../../local.properties ] ; then
	cp ../../local.properties Builds/Android/ || exit 1
else
	echo "projuce-app.sh: building '`basename $1`' without local.properties."
fi

# Fixup Android project
cp $CURDIR/sample-project.settings.gradle Builds/Android/settings.gradle
cp $CURDIR/sample-project.gradle.properties Builds/Android/gradle.properties
cp $CURDIR/sample-project.gradle-wrapper.properties Builds/Android/gradle/wrapper/gradle-wrapper.properties
# Projucer is too inflexible to generate required content for top-level file.
cp $CURDIR/sample-project.build.gradle Builds/Android/build.gradle

# copy aap_metadata.xml once Builds/Android is created.
# Projucer behavior is awkward. It generates "debug" and "release" directories, and any other common resources are ignored.
mkdir -p Builds/Android/app/src/debug/res/xml && cp $AAP_METADATA_XML_SOURCE Builds/Android/app/src/debug/res/xml/aap_metadata.xml || exit 1
mkdir -p Builds/Android/app/src/release/res/xml && cp $AAP_METADATA_XML_SOURCE Builds/Android/app/src/release/res/xml/aap_metadata.xml || exit 1

APPNAMELOWER=`echo $APPNAME | tr [:upper:] [:lower:]`

# Projucer is too inflexible to generate required content.
## AndroidManifest.xml (only for plugins)
if [ -f Builds/Android/app/src/debug/res/xml/aap_metadata.xml ] ; then
sed -e "s/@@@ PACKAGE_NAME @@@/org.androidaudioplugin.juceports.$APPNAMELOWER/" $CURDIR/template.AndroidManifest.xml > Builds/Android/app/src/main/AndroidManifest.xml || exit 1
fi

echo "-------- Post-projucer file list for $APPNAME: --------"
find .
