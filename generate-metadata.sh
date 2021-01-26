#!/bin/bash

# Options:
## - APPNAME

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

rm -f `pwd`/aap_metadata.xml ;
echo "building aap-metadata-generator tool..." ;
$PLAT_COMPILER -g $CURDIR/tools/aap-metadata-generator.cpp \
	$LIBNAME \
	$PLAT_LDFLAGS \
        -lstdc++ -ldl -lm -lpthread \
        -o $CURDIR/tools/aap-metadata-generator  || exit 1;
$CURDIR/tools/aap-metadata-generator `pwd`/aap_metadata.xml ;

mkdir -p Builds/Android/app/src/main/res/xml/

cp `pwd`/aap_metadata.xml Builds/Android/app/src/main/res/xml/

