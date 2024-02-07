# AAP-JUCE Porting Step By Step Guide (2023.4 edition)

This is an attempt to explain how I (@atsushieno) creates JUCE application ports, step by step. In this example, I import files from the [aap-juce-peak-eater](https://github.com/atsushieno/aap-juce-peak-eater/) checkout, right next to it.

## import as an Android app project

Begin with these lines on your terminal:

```
mkdir aap-juce-xenos
cd aap-juce-xenos

# Set up a basic aap-juce port repository structure.
git init
mkdir external
cd external
git submodule add https://github.com/atsushieno/aap-core.git
git submodule add https://github.com/atsushieno/aap-juce.git
git submodule add https://github.com/raphaelradna/xenos.git
git submodule update --init --recursive
cd ..

# Copy common basic files as an Android project.
cp -R ../aap-juce-peak-eater/LICENSE .
cp -R ../aap-juce-peak-eater/gradle* .
cp -R ../aap-juce-peak-eater/gradle.properties .
cp -R ../aap-juce-peak-eater/settings.gradle .
# we will make changes to it later...
cp -R ../aap-juce-peak-eater/Makefile .

# Create and/or copy app files...
touch aap-juce-support.patch # empty file so far

mkdir app
cp -R ../aap-juce-peak-eater/app/src app/
cd app/src/main
rm cpp # it is a symlink
ln -s ../../../external/xenos cpp
cd ../../..

cp -R ../aap-juce-peak-eater/app/build.gradle app/

# replacing PeakEater with Xenos, can just use edit on vi etc. (-i '' on mac)
sed -i -e s/PeakEater/xenos/ settings.gradle    # lowercase, same as submodule path
sed -i -e s/PeakEater/xenos/ Makefile    # lowercase, same as submodule path
# this needs an additional change
sed -i -e s/APPNAME=xenos/APPNAME=Xenos/ Makefile    # UPPERCASE, *should be* same as app name in CMake
sed -i -e s/PeakEater/Xenos/ app/src/main/res/values/string.xml    # UPPERCASE, same as above
sed -i -e s/PeakEater/Xenos/ app/src/main/java/com/rmsl/juce/Java.kt    # UPPERCASE, same as app name in CMakeLists.txt

# similarly, replace peak_eater with xenos, lowercase (required as the Android package name):
sed -i -e s/peak_eater/xenos/ app/build.gradle
```

### adjust path to JUCE

Every JUCE plugin app specifies JUCE in its own path, mostly submodule. But sometimes they assume you have one.
In this case, Xenos does not have it as a submodule while it expects that `JUCE` exists as a direct subdirectory. So, add an extra checkout there:

```
cd external/xenos
git clone https://github.com/juce-framework/JUCE.git
```

`Makefile` specifies the path to JUCE (`JUCE_DIR=...`). Make such a change in that file too.

There is another part that requires rewrite for the JUCE path, in `app/build.gradle`:

```
sourceSets {  
  main.java.srcDirs +=  
  ["../external/PeakEater/Dependencies/JUCE/modules/juce_gui_basics/native/javaopt/app",  
         "../external/aap-juce/java"]  
  main.res.srcDirs +=  
  []  
}
```

This `PeakEater/Dependencies/` needs to be replaced by `xenos/` . (Note that next time you further want to rewrite for your another porting, the actual path may be even different)

### make changes for MidiDeviceService

In addition, not relevant to JUCE path, there should be two additional changes to the existing files to add MIDI device service support. It is required when copying from an effect plugin (which cannot be a MidiDeviceService):

- in `app/build.gradle`, add `implementation libs.aap.midi.device.service` to the `dependencies` section
- in `app/src/main/AndroidManifest.xml`, add the following lines along side another `<service>` element:

```
<service android:name="org.androidaudioplugin.midideviceservice.StandaloneAudioPluginMidiDeviceService"  
  android:permission="android.permission.BIND_MIDI_DEVICE_SERVICE"  
  android:exported="true">  
 <intent-filter>  
 <action android:name="android.media.midi.MidiDeviceService" />  
 </intent-filter>  
 <meta-data android:name="android.media.midi.MidiDeviceService"  
  android:resource="@xml/midi_device_info" />  
</service>
```

### AAP additions to app CMakeLists.txt

Now open `external/xenos/CMakeLists.txt` in your text editor and add the following lines at the end. Depending on the JUCE plugin app you are trying to import, `${AAP_NAME}` may not be used and hardcoded (for example, PeakEater was) :

```
# begin AAP specifics -->  
  
include_directories(  
  "${AAP_DIR}/include"  
)  
  
add_definitions([[-DJUCE_PUSH_NOTIFICATIONS_ACTIVITY="com/rmsl/juce/JuceActivity"]])  
target_compile_definitions(${APP_NAME} PUBLIC  
  JUCEAAP_HAVE_AUDIO_PLAYHEAD_NEW_POSITION_INFO=1  
 JUCE_PUSH_NOTIFICATIONS=1  )  
  
message("AAP_DIR: ${AAP_DIR}")  
message("AAP_JUCE_DIR: ${AAP_JUCE_DIR}")  
juce_add_modules(${AAP_JUCE_DIR}/aap-modules/aap_audio_processors)  
  
if (ANDROID)  
  
  # dependencies  
  find_library(log "log")  
  find_library(android "android")  
  find_library(glesv2 "GLESv2")  
  find_library(egl "EGL")  
  set(cpufeatures_lib "cpufeatures")  
  set(oboe_lib "oboe")  
  
  target_include_directories(${APP_NAME} PRIVATE  
  "${ANDROID_NDK}/sources/android/cpufeatures"  
            "${OBOE_DIR}/include"  
            )  
  
  add_compile_definitions([[JUCE_DONT_AUTO_OPEN_MIDI_DEVICES_ON_MOBILE=1]])  
endif (ANDROID)  
  
target_link_libraries(${APP_NAME}  
  PRIVATE  
  aap_audio_processors
  )  
# <-- end Android specifics  
# <-- end AAP specifics
```

### Create aap_metadata.xml for the new port

An AAP plugin needs its own metadata description file `aap_metadata.xml`. It needs to be in an XML file that can be consumed by Android Service framework (to query metadata without instantiating the Service itself, we are forced to use XML). Copy `aap_metadata.xml` into the new port, at `app/src/main/res/xml` directory, and make necessary changes to the file so that it indicates the correct names for the porting target (otherwise the new port will be advertising wrong plugin name, developer name, copyright, etc...).

### Make sure that the plugin app code builds for Android

Now go back to the terminal and continue:

```
# usually $HOME/Android/Sdk ($HOME/Library/Android/sdk on mac)
echo "sdk.dir=/your/path/to/android/sdk/" > local.properties
make
```

Once you reached this `make` then your Android Studio project is ready, with `aap-core` built and installed into your Maven local repository (under `~/.m2/repository/org/androidaudioplugin`), so from here you can continue with Android Studio too.

By the way, `make` would complete successfully and there will be `apk`:

```
$ find **/*.apk
app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
app/build/outputs/apk/debug/app-debug.apk
```

BUT, it does not work yet. Because the required plugin code is not actually built and packaged into the `*.apk` ! :

```
$ unzip -l app/build/outputs/apk/debug/app-debug.apk | grep .so$ | grep -i xenos # it will show nothing
```

It is because, the target app (Xenos this time) does not include `Standalone` plugin app. To make it build, make changes to `external/xenos/CMakeLists.txt` to add it to `BUILD_TYPES`:

```
# Set what formats we want to target
list(APPEND BUILD_TYPES VST3 Standalone)
```

Now, let's try to build the app again. You can run either `make` or `./gradlew build`, or just F9 on Android Studio.

Sadly, Xenos is not ready for Android yet, and it results in:

```
/Users/atsushi/sources/AAP/aap-juce-xenos/app/src/main/cpp/Source/PluginEditor.cpp:292:17: error: no member named 'browseForFileToOpen' in 'juce::FileChooser'
```

`juce::FileChooser::browseForFileToOpen()` is a synchronous dialog that JUCE on Android does not support (JUCE is not shorthanded; the concept of synchronous dialogs do not match how Android Views work). We have to fix this code. We can rewrite this code to run file dialog asynchronously, but this time it's more important to get this working sooner (it is not AAP-specific topic either).

So far, just comment out that code (it only disables Scala `*.scl` file support).

### Finishing it up

Now it should really build. Try `make` or `./gradlew build` or F9 on Android Studio again. This will finally get you to the app launching:

<img alt="Xenos on Android" src="https://github.com/atsushieno/aap-juce-xenos/blob/main/docs/images/xenos-android.png?raw=true" width="650px" />

<img alt="Xenos on aaphostsample" src="https://github.com/atsushieno/aap-juce-xenos/blob/main/docs/images/xenos-aaphostsample.png?raw=true" height="600px" />


Once you are done to this point, you should make this project rebuildable from scratch. To make it so, there is one more thing you need to make to the code. Your changes to the target app is not going to be committed to the repo. You wouldn't like to maintain your fork either. You would most likely like to keep a separate tiny patch to the project. It is what `aap-juce-support.patch` is supposed to hold (remember you created an empty file earlier?). 

```
cd external/xenos
git diff > ../../aap-juce-support.patch 
cd ../..
```

Now, add all those source files to the newly created repo:

```
git add .gitignore LICENSE Makefile aap-juce-support.patch app/ build.gradle gradle* settings.gradle
```

You might want to create `README.md` and add to them as well.

### Bonus: GitHub Actions build setup

Those official aap-juce ports contain automated build setup for GitHub Actions. Usually it could be just copied from other project.

```
cp -R ../aap-juce-peak-eater/.github .
```

But since we have an additional manual setup i.e. adding JUCE checkout under xenos source tree, it also needs to be automated.

In `Makefile`, there can be additional target `PRE_BUILD_TASKS` that is automatically run. We can set it up in `Makefile` after `include $(AAP_JUCE_DIR)/Makefile.cmake-common`, like:

```
PRE_BUILD_TASKS=checkout-juce

checkout-juce: $(APP_SRC_DIR)/JUCE/.stamp

$(APP_SRC_DIR)/JUCE/.stamp:
	cd $(APP_SRC_DIR) && git clone https://github.com/juce-framework/JUCE.git
	touch $(APP_SRC_DIR)/JUCE/.stamp
```

