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
    echo "usage: APPNAME=xyz PROJUCER=/path/to/JUCE/Projucer build-desktop.sh [.jucer file]"
    echo "Missing .jucer file (or it does not exist)."
    exit 2
fi

echo "build-desktop.sh: APPNAME: $APPNAME / MAPAPPNAME: $MACAPPNAME"

SRCFILE=`$READLINK -f $1` >/dev/null
SRCDIR=`dirname $SRCFILE` >/dev/null

echo "Entering $SRCDIR ..."
cd $SRCDIR

$PROJUCER --resave `basename $1` || exit 1

if [ `uname` == 'Darwin' ] ; then
if [ $APPNAME == 'AudioPluginHost' ] ; then
	pushd . && cd Builds/MacOSX && xcodebuild -project "$APPNAME.xcodeproj" && popd || exit 4
else
	pushd . && cd Builds/MacOSX && xcodebuild -project "$MACAPPNAME.xcodeproj" -target "$MACAPPNAME - Shared Code" && popd || exit 4
fi
else
	make -C Builds/LinuxMakefile || exit 4
fi

echo "exiting $CURDIR..."
cd ..

