#!/bin/sh

CURDIR="$( cd `dirname $0` >/dev/null 2>&1 && pwd )"

if ! [ $SKIP_METADATA_GENERATOR ] ; then
	rm -f `pwd`/aap_metadata.xml ;
	echo "building aap-metadata-generator tool..." ;
	gcc -g ../../tools/aap-metadata-generator.cpp \
        	Builds/LinuxMakefile/build/$APPNAME.a \
	        -lstdc++ -ldl -lm -lpthread \
	        `pkg-config --libs alsa x11 xinerama xext freetype2 libcurl webkit2gtk-4.0` \
	        -o aap-metadata-generator  || exit 1;
	./aap-metadata-generator `pwd`/aap_metadata.xml ;
fi

cp $CURDIR/samples/sample-project.settings.gradle Builds/Android/settings.gradle

# There is no way to generate this in Projucer.
cp $CURDIR/samples/sample-project.gradle.properties Builds/Android/gradle.properties

# Projucer is too inflexible to generate required content.
cp $CURDIR/samples/sample-project.build.gradle Builds/Android/build.gradle

mkdir -p Builds/Android/app/src/debug/res/xml/
mkdir -p Builds/Android/app/src/release/res/xml/

if ! [ $SKIP_METADATA_GENERATOR ] ; then
	cp `pwd`/aap_metadata.xml Builds/Android/app/src/debug/res/xml/ ;
	cp `pwd`/aap_metadata.xml Builds/Android/app/src/release/res/xml/ ;
fi

