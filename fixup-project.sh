#!/bin/bash

CURDIR="$( cd `dirname $0` >/dev/null 2>&1 && pwd )"

if [ `uname` == 'Darwin' ] ; then
LIBNAME=Builds/MacOSX/build/Debug/lib$APPNAME.a
PLAT_LDFLAGS="-lobjc -framework Foundation -framework AudioToolbox -framework QuartzCore -framework Carbon -framework Cocoa -framework CoreAudio -framework IOKit -framework CoreAudioKit -framework Accelerate -framework CoreMIDI"
PLAT_COMPILER="xcrun clang++"
else
LIBNAME=Builds/LinuxMakefile/build/$APPNAME.a
PLAT_LDFLAGS=`pkg-config --libs alsa x11 xinerama xext freetype2 libcurl webkit2gtk-4.0`
PLAT_COMPILER=gcc
fi

echo "Static library for `uname` is $LIBNAME"

if ! [ $SKIP_METADATA_GENERATOR ] ; then
	rm -f `pwd`/aap_metadata.xml ;
	echo "building aap-metadata-generator tool..." ;
	$PLAT_COMPILER -g ../../aap-juce/tools/aap-metadata-generator.cpp \
		$LIBNAME \
		$PLAT_LDFLAGS \
	        -lstdc++ -ldl -lm -lpthread \
	        -o aap-metadata-generator  || exit 1;
	./aap-metadata-generator `pwd`/aap_metadata.xml ;
fi

cp $CURDIR/sample-project.settings.gradle Builds/Android/settings.gradle

# There is no way to generate this in Projucer.
cp $CURDIR/sample-project.gradle.properties Builds/Android/gradle.properties

# Only Android Studio 2020.3.1 Canary comes with working debugger ATM.
cp $CURDIR/sample-project.gradle-wrapper.properties Builds/Android/gradle/wrapper/gradle-wrapper.properties

# Projucer is too inflexible to generate required content.
cp $CURDIR/sample-project.build.gradle Builds/Android/build.gradle

mkdir -p Builds/Android/app/src/debug/res/xml/
mkdir -p Builds/Android/app/src/release/res/xml/

if ! [ $SKIP_METADATA_GENERATOR ] ; then
	cp `pwd`/aap_metadata.xml Builds/Android/app/src/debug/res/xml/ ;
	cp `pwd`/aap_metadata.xml Builds/Android/app/src/release/res/xml/ ;
fi

