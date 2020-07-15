#!/bin/bash

APP_NAME=$1	# used to determine .jucer file
		#  e.g. AudioPluginHost
SRC_PATH=$2	# source directory to copy
		#  e.g. external/JUCE/extras/AudioPluginHost
DST_PATH=$3	# destination directory
		#  e.g. samples/AudioPluginHost
PATCH_FILE=$4	# patch file path to **relative to** DST_PATH
		#  e.g. ../juceaaphost.patch
PATCH_DEPTH=$5	# patch depth, used as a -p argument to patch(exe)
		#  e.g. 6
OVERRIDE_JUCER=$6	# e.g. samples/override.AudioPluginHost.jucer

INITIAL_DIR=`pwd`

if ! [ $OVERRIDE_JUCER ]; then
	echo "USAGE: create-patched-juce_app.sh [APP_NAME] [SRC_PATH] [DST_PATH] [PATCH_FILE] [PATCH_DEPTH] [OVERRIDE_JUCER]"
	exit 1 ;
fi

# The backup stuff below is disabled
#if [ -d $DST_PATH-AutoBackup ]; then
#	echo "make sure that you don't retain backup as '$DST_PATH-AutoBackup'. It is a backup to ensure that you have a clean '$DST_PATH' directory. If you really need a backup then give a different name." && exit 2 ;
#fi
#if [ -d $DST_PATH ]; then
#	mv $DST_PATH $DST_PATH-AutoBackup ;
#fi

mkdir -p $DST_PATH
cp -R $SRC_PATH/* $DST_PATH/
rm -rf $DST_PATH/.git

cd $DST_PATH
if [ -f $PATCH_FILE ]; then
	patch -i $PATCH_FILE -p$PATCH_DEPTH ;
else
	echo "Patch file $PATCH_FILE does not exist. Skipping." ;
fi
cd $INITIAL_DIR

if [ -f $OVERRIDE_JUCER ]; then
	cp $OVERRIDE_JUCER $DST_PATH/$APP_NAME.jucer ;
else
	echo "override JUCER file $OVERRIDE_JUCER does not exist. Skipping." ;
fi

touch $DST_PATH/.stamp

