#!/bin/bash

# Options:
## - APPNAME
## - LIBRARY (so filename that host attempts to dlopen())
## - ENTRYPOINT (entrypoint function that hots attempts to dlsym())
## - LIBFILES (lib files to link, unquoted, separate by spaces)
## - EXTRA_OUTDIR (copy destination Android app topdir e.g. `Builds/Android`)

CURDIR="$( cd `dirname $0` >/dev/null 2>&1 && pwd )"

if [ `uname` == 'Darwin' ] ; then
LIBDEFAULTS=Builds/MacOSX/build/Debug/lib$APPNAME.a
PLAT_LDFLAGS="-lobjc -framework Foundation -framework AudioToolbox -framework QuartzCore -framework Carbon -framework Cocoa -framework CoreAudio -framework IOKit -framework CoreAudioKit -framework Accelerate -framework CoreMIDI"
PLAT_COMPILER="xcrun clang++"
else
LIBDEFAULTS=Builds/LinuxMakefile/build/$APPNAME.a
PLAT_LDFLAGS=`pkg-config --libs alsa x11 xinerama xext freetype2 libcurl webkit2gtk-4.0`
PLAT_COMPILER=gcc
fi
if [ ! $LIBFILES ] ; then
LIBFILES=$LIBDEFAULTS
fi

echo "Static libraries for `uname` are $LIBFILES"

rm -f `pwd`/aap_metadata.xml ;
echo "building aap-metadata-generator tool..." ;
$PLAT_COMPILER -g $CURDIR/tools/aap-metadata-generator.cpp \
	$LIBFILES \
	$PLAT_LDFLAGS \
        -lstdc++ -ldl -lm -lpthread \
        -o $CURDIR/tools/aap-metadata-generator  || exit 1;
$CURDIR/tools/aap-metadata-generator `pwd`/aap_metadata.xml $LIBRARY $ENTRYPOINT;

if [ $EXTRA_OUTDIR ] ; then
mkdir -p $EXTRA_OUTDIR/app/src/main/res/xml/ ;
cp `pwd`/aap_metadata.xml $EXTRA_OUTDIR/app/src/main/res/xml/ ;
fi

