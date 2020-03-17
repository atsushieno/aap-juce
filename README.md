# AAP JUCE audio processor and client modules

This is a place where we have JUCE modules for AAP, both plugins and hosts.

- Host sample
  - AudioPluginHost
- Plugin sample
  - [andes](https://github.com/artfwo/andes/)
  - [SARAH](https://github.com/getdunne/SARAH/)


## Build Instruction

You most likely need a Linux desktop. It may build on virtual machines, but you might want get a real Linux desktop because JUCE depends on X11 (even without running GUI).
It should build on any kind of Linux desktop but it may fail. So far only Ubuntu 19.10 is the verified desktop.

You need Android SDK. If you install it via Android Studio it is usually placed under `~/Android/Sdk`.
You also need Android NDK 21.0. It would be installed under `~/Android/Sdk/ndk/21.*`.

Once they are all set, simply run `make`.


Depending on the NDK setup you might also have to rewrite `Makefile` and `Builds/Android/local.properties` to point to the right NDK location. Then run `cd Builds/Android && ./gradlew build` instead of `./build-sample.sh`.
It would be much easier to place Android SDK and NDK to the standard location though. Symbolic links would suffice.


## Under the hood

JUCE itself already supports JUCE apps running on Android and there is still
no need to make any changes to the upstream JUCE.

We use juce_emscripten for future plan to integrate with wasm builds.
juce_emscripten is based on 5.4.5-ish so far.

Projucer is not capable of supporting arbitrary plugin format and it's quite incompete.
Thus we make additional changes to the generated Android Gradle project.
It is mostly taken care by `build-sample.sh` and `fixup-project.sh`.

## Generating aap_metadata.xml

It is already done as part of `fixup-project.sh` but in case you would like
to run it manually...

To import JUCE audio plugins into AAP world, we have to create plugin
descriptor (`aap_metadata.xml`). We have a supplemental tool source to
help generating it automatically from JUCE plugin binary (shared code).

The command line below builds `aap-metadata-generator` under
`Builds/LinuxMakefile/build` and generates `aap_metadata.xml` at the plugin
top level directory:

```
APP=__yourapp__ gcc (__thisdir__)/tools/aap-metadata-generator.cpp \
	Builds/LinuxMakefile/build/intermediate/Debug/*.o \
	-lstdc++ -ldl -lm -lpthread -lGL \
	-L../../external/android-audio-plugin-framework/build/native/androidaudioplugin -landroidaudioplugin 
	`pkg-config --libs alsa x11 xinerama xext freetype2 libcurl webkit2gtk-4.0` \
	-o aap-metadata-generator \
	&&./aap-metadata-generator aap_metadata.xml
```

## Porting other JUCE-based audio plugins

Here are the porting steps that we had. Note that this applies only to samples built under this `samples` directory:

- Run Projucer and open the project `.jucer` file.
- open project settings and ensure to build at least one plugin format (`Standalone` works)
- Ensure that `JUCEPROJECT` has `name` attribute value only with `_0-9A-Za-z` characters. That should be handled by Projucer but its hands are still too short.
  - For example, we had to rename `Andes-1` to `Andes_1`.
- Go to Modules and add module `juceaap_audio_plugin_client` (via path, typically)
- Go to Android exporter section and make following changes:
  - Module Dependencies: add list below
  - minSdkVersion 29
  - targetSdkVersion 29
  - Custom Manifest XML content: listed below
  - Gradle Version 6.2
  - Android Plug-in Version 4.0.0-alpha09

For module dependenciesm add below:

```
implementation project(':androidaudioplugin')
implementation "org.jetbrains.kotlin:kotlin-stdlib-jdk7:1.3.61"
```

For custom Manifest XML content, replace `!!!APPNAME!!!` part with your own (this is verified with JUCE 5.4.7 and may change in any later versions):

```
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="org.androidaudioplugin.aappluginsample" android:versionCode="1" android:versionName="0.1">
  <uses-permission android:name="android.permission.FOREGROUND_SERVICE"/>
  <application android:allowBackup="true" android:label="@string/app_name"
               android:supportsRtl="true" android:name="com.roli.juce.JuceApp" android:hardwareAccelerated="false">
    <service android:name="org.androidaudioplugin.AudioPluginService" android:label="!!!APPNAME!!!Plugin">
      <intent-filter>
        <action android:name="org.androidaudioplugin.AudioPluginService"/>
      </intent-filter>
      <meta-data android:name="org.androidaudioplugin.AudioPluginService#Plugins" android:resource="@xml/aap_metadata"/>
    </service>
  </application>
</manifest>
```

Below can be edited with text editor:

- On the top-level `<JUCERPROJECT>` element add `juceFolder="../../external/juce_emscripten"`
- On every `<MODULEPATH>` element, specify `path="../../external/juce_emscripten/modules"` everywhere. Yes, the path on `<JUCERPROJECT>` does not apply here, Projucer is incapable of taking it into consideration.
- Add `<MODULEPATH id="juceaap_audio_plugin_processors" path="../../modules"/>` for each `<MODULEPATHS>` for each `<EXPORTFORMAT>`. (You can do this in Projucer too, but copypasting on a text editor is 10x easier.)
- In `<LINUX_MAKE>` and `<ANDROIDSTUDIO>` element, replace `<CONFIGURATION>` elements with the contents explained below for each.

For `<LINUX_MAKE>` for desktop (`Builds/LinuxMakefile`, not to be confused with the one for `EMSCRIPTEN`), below, replacing `!!!APPNAME!!!` part with the actual name:

```
        <CONFIGURATION name="Debug" isDebug="1" targetName="!!!APPNAME!!!"
            headerPath="../../../../external/android-audio-plugin-framework/native/plugin-api/include;../../../../external/android-audio-plugin-framework/native/androidaudioplugin/core/include;${CMAKE_CURRENT_SOURCE_DIR}/../../../../external/android-audio-plugin-framework/native/plugin-api/include;${CMAKE_CURRENT_SOURCE_DIR}/../../../../external/android-audio-plugin-framework/native/androidaudioplugin/core/include"
            libraryPath="/usr/X11R6/lib/;../../../../external/android-audio-plugin-framework/build/native/androidaudioplugin;${CMAKE_CURRENT_SOURCE_DIR}/../../../../external/android-audio-plugin-framework/build/native/androidaudioplugin"/>
        <CONFIGURATION name="Release" isDebug="0" optimisation="2" targetName="!!!APPNAME!!!"
            headerPath="../../../../external/android-audio-plugin-framework/native/plugin-api/include;../../../../external/android-audio-plugin-framework/native/androidaudioplugin/core/include;${CMAKE_CURRENT_SOURCE_DIR}/../../../../external/android-audio-plugin-framework/native/plugin-api/include;${CMAKE_CURRENT_SOURCE_DIR}/../../../../external/android-audio-plugin-framework/native/androidaudioplugin/core/include"
            libraryPath="/usr/X11R6/lib/;../../../../external/android-audio-plugin-framework/build/native/androidaudioplugin;${CMAKE_CURRENT_SOURCE_DIR}/../../../../external/android-audio-plugin-framework/build/native/androidaudioplugin"/>
```

For `<ANDROIDSTUDIO>`:

```
        <CONFIGURATION name="Debug" isDebug="1" 
                       headerPath="${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../external/android-audio-plugin-framework/androidaudioplugin/core/include;${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../../external/android-audio-plugin-framework/plugin-api/include;${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../../external/android-audio-plugin-framework/dependencies/tinyxml2"
                       libraryPath="${CMAKE_CURRENT_SOURCE_DIR}/../../../../../external/android-audio-plugin-framework/java/androidaudioplugin/build/intermediates/cmake/debug/obj/${ANDROID_ABI}"
                       optimisation="1" linkTimeOptimisation="0"
                       recommendedWarnings="LLVM"/>
        <CONFIGURATION name="Release" isDebug="0" optimisation="3" linkTimeOptimisation="1"
                       headerPath="${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../external/android-audio-plugin-framework/androidaudioplugin/core/include;${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../../external/android-audio-plugin-framework/plugin-api/include;${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../../external/android-audio-plugin-framework/dependencies/tinyxml2"
                       libraryPath="${CMAKE_CURRENT_SOURCE_DIR}/../../../../../external/android-audio-plugin-framework/java/androidaudioplugin/build/intermediates/cmake/release/obj/${ANDROID_ABI}"
                       recommendedWarnings="LLVM"/>

```

Note that those relative paths are valid only if those source directories exist under `samples` directory. Projucer is of a lot of hacks and there is no valid way to specify required paths.
If you don't want to be messed a lot, just specify absolute paths that won't be viable outside your machine (we don't accept such PRs), but it would work for you.

Lastly, copy `sample-project-build.gradle` as the project top-level `build.gradle` in `Builds/Android`. Projucer lacks options to specify required content so we have to come up with manually generated ones. Fortunately there is nothing specific to the project, usually.


## Code origin and license

The sample app code under `samples/AudioPluginHost/Source` directory is slightly modified version of
JUCE [AudioPluginHost](https://github.com/WeAreROLI/JUCE/tree/master/extras/AudioPluginHost)
which is distributed under the GPLv3 license.

The sample app code under `samples/SARAH` directory is slightly modified version of [SARAH](https://github.com/getdunne/SARAH) which is distributed under the MIT license (apart from JUCE under GPLv3).

The sample app code under `samples/andes` directory is slightly modified version of [andes](https://github.com/artfwo/andes) which is distributed under the GPLv3 license.
