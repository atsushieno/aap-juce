#!/bin/bash

# Options:
## - APPNAME
## - LIBRARY (so filename that host attempts to dlopen())
## - ENTRYPOINT (entrypoint function that host attempts to dlsym())
## - LIBFILES (lib files to link, unquoted, separate by spaces)
## - EXTRA_LDFLAGS (passed to gcc e.g. -lGL)
## - EXTRA_OUTDIR (copy destination Android app topdir e.g. `Builds/Android`)
## - BUILDS_DIR (usually `Builds')
## - PLAT_BUILD_DIR (usually `MacOSX` or `LinuxMakefile`)

CURDIR="$( cd `dirname $0` >/dev/null 2>&1 && pwd )"

if [ -z "$BUILDS_DIR" ] ; then
BUILDS_DIR=Builds
fi
if [ -z "$PLAT_BUILD_DIR" ] ; then
if [ "`uname`" == 'Darwin' ] ; then
PLAT_BUILD_DIR=MacOSX
else
PLAT_BUILD_DIR=LinuxMakefile
fi
fi
if [ "`uname`" == 'Darwin' ] ; then
LIBDEFAULTS=$BUILDS_DIR/$PLAT_BUILD_DIR/build/Debug/lib$APPNAME.a
PLAT_LDFLAGS="-lobjc -framework Foundation -framework AudioToolbox -framework QuartzCore -framework Carbon -framework Cocoa -framework CoreAudio -framework IOKit -framework CoreAudioKit -framework Accelerate -framework CoreMIDI -framework Metal"
if [ -z "$PLAT_COMPILER" ] ; then
PLAT_COMPILER="xcrun clang++"
fi
else
LIBDEFAULTS=$BUILDS_DIR/$PLAT_BUILD_DIR/build/$APPNAME.a
PLAT_LDFLAGS=`pkg-config --libs alsa x11 xinerama xext freetype2 libcurl webkit2gtk-4.0`
if [ -z "$PLAT_COMPILER" ] ; then
PLAT_COMPILER=gcc
fi
fi
if [ -z "$LIBFILES" ] ; then
LIBFILES=$LIBDEFAULTS
fi

echo "Static libraries for `uname` are $LIBFILES"

IFS=':' read -ra arr <<< "$LIBFILES"
Q_LIBFILES=()
for i in "${arr[@]}"; do
Q_LIBFILES+=("$i")
done

rm -f `pwd`/aap_metadata.xml ;
echo "building aap-metadata-generator tool..." ;
$PLAT_COMPILER -g $CURDIR/tools/aap-metadata-generator.cpp \
	${Q_LIBFILES[@]} \
	$EXTRA_LDFLAGS \
	$PLAT_LDFLAGS \
        -lstdc++ -ldl -lm -lpthread \
        -o $CURDIR/tools/aap-metadata-generator  || exit 1;
$CURDIR/tools/aap-metadata-generator `pwd`/aap_metadata.xml $LIBRARY $ENTRYPOINT;

if [ -d "$EXTRA_OUTDIR" ] ; then
if [ -d "$EXTRA_OUTDIR/app/src/debug" ] ; then
mkdir -p $EXTRA_OUTDIR/app/src/debug/res/xml/ ;
cp `pwd`/aap_metadata.xml $EXTRA_OUTDIR/app/src/debug/res/xml/ ;
mkdir -p $EXTRA_OUTDIR/app/src/release/res/xml/ ;
cp `pwd`/aap_metadata.xml $EXTRA_OUTDIR/app/src/release/res/xml/
else
mkdir -p $EXTRA_OUTDIR/app/src/main/res/xml/ ;
cp `pwd`/aap_metadata.xml $EXTRA_OUTDIR/app/src/main/res/xml/ ;
fi
fi

echo "done."
