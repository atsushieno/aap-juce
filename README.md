# AAP-JUCE: JUCE audio plugin and hosting support for AAP (Audio Plugins For Android)

This repo is the place where we have the common JUCE integration support modules for [aap-core](https://github.com/atsushieno/aap-core) (AAP), for both plugins and hosts.

The entire AAP framework is on early development phase and not ready for any serious consumption yet.
Everything is subject to change. Contributions are welcome but please bear in mind, documentation is poor and source code is ugly yet. We have [GitHub discussions for the project](https://github.com/atsushieno/aap-juce/discussions/landing) enabled so please feel free to shoot your questions, if any.

## Existing ports

This repository used to contain a handful of sample projects, but to avoid bloat core library repository, they are split from here and have their own repositories. Now they are listed at [AAP Wiki](https://github.com/atsushieno/aap-core/wiki/List-of-AAP-plugins-and-hosts).

aap-juce-plugin-host can enumerate the installed AAP plugins on the system (not limited to aap-juce ones), and instantiate each plugin.

At this state, this repository itself is almost only about a set of build scripts that lets you port your (or others') JUCE audio plugins and hosts to AAP world. And probably more importantly, this README.

## Why JUCE for AAP?

JUCE is a popular cross-platform, cross-plugin-format audio development framework.
JUCE itself does not support AAP, but it can be extended by additional modules.
JUCE also supports Android (you can even run UI), which makes things closer to actual app production.
Still, JUCE is not designed to be extensible *enough*, additional code to support AAP is needed in each app.

While JUCE itself is useful to develop frameworks like AAP, it is designed to be independent of any other audio development toolkits and frameworks. We stick to minimum dependencies, at least on the public API surface.

Their API is stable-ish, while AAP API is not. So if anyone wants to get audio plugins portable without fear of API breakage, JUCE can be your good friend.

Note that JUCE plugins are usually designed for desktop and not meant to be usable on mobiles. In particular -

- their UIs are usually useless on mobile.
- those plugins that lets user pick up local files would not fit well with Android paradigm.
- Some plugins woudl expose performance issues too.

## How to try it out?

You would have to build and install each host and/or plugin from source so far, or look for the APKs from the build artifacts zip archives from successful GitHub Actions workflow runs. They could be installed via [AAP APK Installer](https://github.com/atsushieno/aap-ci-package-installer).

You need a host app and a plugin to try at least one plugin via one host. This repository does not contain any application within itself.

The host can be either `aaphostsample` in aap-core repo, or `AudioPluginHost` in [aap-juce-plugin-host](https://github.com/atsushieno/aap-juce-plugin-host) repo (which is JUCE AudioPluginHost with AAP support).

The plugin can be either `aapbarebonepluginsample` in aap-core repo (more stable), or any plugin on the [AAP Wiki](https://github.com/atsushieno/aap-core/wiki/List-of-AAP-plugins-and-hosts).

For those `aap-juce-*` repositories, `make` will build them.

For JUCE apps that use Projucer, JUCE Android apps are generated and built under `Builds/Android/app/build/outputs/` in each app directory.
Though we typically use Android Studio and open `Builds/Android` and then run or debug there, once top-level `make` ran successfully.

For those projects that use CMake, it is `app/build/outputs`.


## Build Instruction

There is a GitHub Actions workflow setup, which is an official build instructions. It is almost one `make` call if all conditions are met.

You need Android SDK. If you install it via Android Studio it is usually placed under `~/Android/Sdk` on Linux, and `~/Library/Android/sdk` on MacOS.
You also need Android NDK, most likely at least r23 (our latest development version uses newer ones).

For those project that use Projucer: depending on the NDK setup you might also have to rewrite `Makefile` and `Builds/Android/local.properties` to point to the right NDK location. Then run `cd Builds/Android && ./gradlew build` instead of `./projuce-app.sh`.
It would be much easier to place Android SDK and NDK to the standard location though. Symbolic links would suffice.

CMake based projects would similarly need to tweak `Makefile`, but other than that it is a typical Android Studio (Android Gradle) project.

## Porting a plugin or a host to AAP

To port existing plugins, or even with a new project, you will either follow the CMake way, or the Projucer way.
You would normally have no choice, the original project would be either of those already, but CMake is much easier and much more intuitive to deal with. With Projucer, you will have to generate project every time you make changes to the project.
In either approach, you end up with an Android Studio (Gradle) project that you can open on Android Studio (or stick to Gradle to build and adb to install, like we do on CI).

There are many aap-juce based apps that could be used as reference/template projects. See the list of plugins on the AAP Wiki.

### Making application itself build for Android

If you are trying to port an existing project to Android, the first thing to do is to make it build for Android, regardless of AAP.
If it is a Projucer project, open Projucer, add Android Studio exporter, resave, open on Android Studio and try to build.
If it is a CMake project, check the next  section ("Make AAP-specific changes: CMake").

JUCE API is in general less featureful on Android than desktop. For example, you have no access to synchronous dialog boxes.
So you will have to rewrite code to follow asynchronous way or disable code so far. Once it's done, let's mark it as **the** porting target application.
 (They were deprecated in recent JUCE versions in 2021, so newer projects would suffer less.)

### Make AAP-specific changes: CMake

For a project that build with CMake, it is fairly easier to port to AAP.

There are existing sample projects listed earlier on this documentation, and to port an existing plugin project (or even if you are going to create a new JUCE plugin project), it is easier to create new project based off of those existing ones, making changes to your plugin specifics.

The best way to find out what you should make changes can be found e.g. by:

```
diff -u aap-juce-witte-eq/ aap-juce-chow-phaser/
diff -u aap-juce-witte-eq/app aap-juce-chow-phaser/app
diff -ur aap-juce-witte-eq/app/src aap-juce-chow-phaser/app/src -x cpp
```

There are some important bits:

- JUCE needs some changes. [JUCE-support-Android-CMake.patch](JUCE-support-Android-CMake.patch) is applied if you build those ports with `make`.
- The plugin's top level `CMakeLists.txt` : we need `Standalone` plugin build (as no other formats are supported, and on Android it is built as a shared library).
- `app/CMakeLists.txt` has plugin-specific information, as well as references to JUCE subdirectories.

A typical porting trouble we encounter is that even if the activity launches, it shows an empty screen.
It is because the application somehow fails to bootstrap (call to `com.rmsl.juce.Java.initialiseJUCE()`).

Another typical problem is that it fails to load the plugin shared library. It is due to missing shared library within the apk, inconsistent `aap_metadata.xml` description (either `library` or `entrypoint` attribute), or missing entrypoint function symbol in the shared library (which must not be visibility=hidden).

### Make AAP-specific changes: Projucer

With the current Projucer based project structure that makes use of `Makefile.common` in this repository, such as `atsushieno/aap-juce-*` projects, it once copies the application source tree into another directory, then apply patches to make it build for Android.

Then we run Projucer to generate Android app sources, and we overwrite some source files that Projucer generated with our own.
Once it's done, you get a workable Android Studio (Gradle) project, just like Projucer does for you.

In short, `make` does all these tasks above for you:

- copy and patch app sources
- run projucer, and overwrite some generated files
- gradlew build

Now, we have to dive into our sausage internals more in depth...

The copied project has overwritten .jucer project (or sometimes different .jucer file than the original aside) that you can keep working with Projucer and Android Studio without creating patched source tree over and over again.
But any changes made to the source code has to be backported to the original source tree.
And if you *indeed* need to perform this process (you usually don't), run `make copy-and-patch-app-sources` which will recreate the application source tree.

In any case, before performing this task, you need this "modified" version of the `.jucer` file. This work is too verbose to explain here. I wrote the details later in this README ("Rewriting .jucer file for AAP").

Once you are done with the modified .jucer file, you still need `aap_metadata.xml` for your project, so run `make update-aap-metadata` or `make update-aap-metadata-cmake` to generate it (explained later too).
Copy it somewhere with some identical name, typically under `apps`.
`make copy-and-patch-app-sources` will copy it if the file is appropriately specified in `Makefile` and it indeed exists.

After these tasks, you are ready to run `make copy-and-patch-app-sources` for your own port.

If you are not making changes to .jucer file, you can keep working with the generated Android Studio project and diagnose any issues.
If you run Projucer to resave the project, the project is overwritten, which invalidates project fixup for AAP (we rewrite AndroidManifest.xml, build.gradle, and so on), so you should avoid that.
Instead, run `make projuce-app` to resave projects and perform required overwrites.

Lastly, for hosts, `AndroidAudioPluginFormat` has to be added to the app's `AudioPluginFormatManager` instances in the application code (example [1](https://github.com/atsushieno/aap-juce-plugin-host/blob/295aa69b48a25f798520d9a365b611a081a0a725/apps/juceaaphost.patch#L62)/[2](https://github.com/atsushieno/aap-juce-plugin-host/blob/295aa69b48a25f798520d9a365b611a081a0a725/apps/juceaaphost.patch#L102)).


### Generating and updating aap_metadata.xml

This only applies to plugins.

There is some non-trivial task: `aap_metadata.xml` has to be created for the plugin. Sometimes it can be hundreds of lines of markup, so you would like to automatically generate from existing code.

There is `update-aap-metadata` target in `Makefile.common` or `update-aap-metadata-cmake` target in `Makefile.cmake-common`, but in case you would like to run it manually...

To import JUCE audio plugins into AAP world, we have to create plugin
descriptor (`aap_metadata.xml`). We have a supplemental tool source to
help generating it automatically from JUCE plugin binary (shared code).


### Dealing with different JUCE versions

JUCE sometimes involves incompatible changes between versions, and sometimes they involve those project file generators. We have some Makefile variables that changes the behavior. It is worth inspecting `Makefile.common` for Projucer-based project and `Makefile.cmake-common` for CMake-based projects (they are often added).

## GitHub Actions common scripts

This repository contains some GitHub Actions workflow description files under `.github` directory. One is for Projucer-based projects, and the other is for CMake. They are being [reused](https://docs.github.com/en/actions/using-workflows/reusing-workflows) in `aap-juce-*` repositories (as they have been almost identical across projects).

As long as other project follows the same structure, you can reuse it too. It is safe to use the versioned workflow by commit rev. as we will be making breaking changes.

## Under the hood

JUCE itself already supports JUCE apps running on Android and there is still no need to make any changes to the upstream JUCE, unless you want to build CMake based project (see [this post by @atsushieno](https://atsushieno.github.io/2021/01/16/juce-cmake-android-now-works.html) for details).

On the other hand, we make a lot of changes to whatever Projucer generates.
Projucer is not capable of supporting arbitrary plugin format and it's quite counter-intuitive to try to use various option fields on Projucer.
Thus we simply replace various parts of the generated Android Gradle project.
It is mostly taken care by `projuce-app.sh`.

CMake is preferred, but it is not officially supported by JUCE so far either.
However the impact of the changes is small so that they can be manually fixed.

We plan to juce_emscripten for future integration with wasm UI builds.

### Makefile Tasks

There are couple of tasks that the build scripts process:

- Create a patched copy of the app sources and overwrite .jucer file. Unless you have an updated patch or changing the entire patching step (by hacking build system), it has to be done only once.
  - Some projects don't actually "overwrite" the .jucer file, but they copy .jucer anyways. There will be unused original .jucer file and then app's .jucer file that is actually in use.
- Generate desktop and Android projects using Projucer, and overwrite some build scripts such as top-level `build.gradle`.
  - It does not involve updating `aap_metadata.xml` anymore. It has to be manually done with desktop version of the app.

There are related build scripts and Makefile targets:

- `copy-and-patch-app-sources` : since we patch the sources but do not want to leave the app source tree dirty, we first copy the app sources, and then apply the patch to them (if exists). This has to be done once before running Projucer.
- `projuce-app` : this executes `Projucer --resave` and then replaces some of the files that Projucer generated with whtaeever we actually need.
- `update-aap-metadata` : it is NOT part of ordinary build step. Whenever app porting maintainer updates the sources, they should run this process to automatically update `aap_metadata.xml`.
  - It internally builds Linux or Mac desktop version of the imported application, and then builds `aap-metadata-importer` tool, linking the "shared code" of the application, and at last runs it to generate `aap_metadata.xml`.
  - `aap_metadata.xml` is generated at the imported application top directory, so porting maintainer is supposed to copy it to the app source directory that would also contain `override.*.jucer` and the source patch (if any).
  - `build-desktop.sh` is used to accomplish this task.

`make projuce-app` will rewrite some files that Projucer --resave has generated, namely:

- build.gradle (top level)
- settings.gradle
- gradle.properties
- gradle/wrapper/gradle-wrapper.properties
- app/src/main/AndroidManifest.xml

At this state, the build script is not flexible enough to make them further customizible.

### difference between normal JUCE Android app and JUCE-AAP Android app

Projucer can generate Android apps, and we basically make use of it.
Although its feature is quite insufficient, we don't expect it to generate the entire set of the required files with sufficient information. Projucer is basically not well-suited tool to do that job.
We (at least on our ports) copy our own support files into the apps instead, namely the top-level `build.gradle`, `gradle-properties` and `settings.gradle` etc.
It is mostly about AAP dependencies.

We also don't expect that the original `.jucer` files can be simply patched by simple diff tool, so we have an entire `.jucer` file to override for each sample project.
They resolve various relative paths to AAP includes and libs.
Both Projucer and Android Gradle Plugin lack sufficient support to decently resolve them.

For CMake support, things are much simpler (as JUCE does not support CMake on Android yet...) and we simply use our own app template with slight changes and reference to `CMakeLists.txt` from the app that is being ported.

### Rewriting .jucer file for AAP

Here are the porting steps that we had. Note that this applies only to `atsushieno/aap-juce-*` projects that make use of the helper files in this repository:

- Run Projucer and open the project `.jucer` file.
- open project settings and ensure to build at least one plugin format (`Standalone` works)
- Ensure that `JUCEPROJECT` has `name` attribute value only with `_0-9A-Za-z` characters. That should be handled by Projucer but its hands are still too short.
  - For example, we had to rename `Andes-1` to `Andes_1` and `OB-Xd` to `OB_Xd`.
- Go to Modules and add module `juceaap_audio_plugin_client` (via path, typically)
- Go to Android exporter section and make following changes (Gradle/AGP versions may vary):
  - Module Dependencies: add list below
  - minSdkVersion 29
  - targetSdkVersion 31 (can be any later as newer versions show up)
  - Custom Manifest XML content: listed below

Changes below are also needed. You can edit with text editors, which would be easier:

- On every `<MODULEPATH>` element, specify `path="./juce-modules"` everywhere.
  - `make copy-and-patch-app-sources` adds symbolic link `juce-modules` to the JUCE `modules` directory. If it does not exist, then this change does not work.
  - If you don't set `path` to point to the exact location e.g. to the one in the submodule in this repo, then it may result in system global path to JUCE (or fails if it is not installed) and any unexpected build breakage could happen. The build log would tell if it went as such.
  - If you are copy-pasting `<MODULES>` element, make sure that you do not accidentally remove necessary ones. @atsushieno had spent a lot of time on finding that he was missing `<MODULE>` for `juce_audio_plugin_client` when copying from `AudioPluginHost` to `Andes_1`...
- Add `<MODULEPATH id="juceaap_audio_processors" path="./aap-modules"/>` for each `<MODULEPATHS>` for each `<EXPORTFORMAT>`. (You can do this in Projucer too, but copypasting on a text editor is 10x easier.)
  - Similar to JUCE modules, the make target adds a symbolic link `aap-modules` to `aap-modules` directory in the aap-juce checkout.
- In `<LINUX_MAKE>` and `<ANDROIDSTUDIO>` element, replace `<CONFIGURATION>` elements with the contents explained below for each.

Below are `<CONFIGURATION>` rewrites.
Note that `make copy-and-patch-app-sources` adds a symbolic link `aap-core` to AAP submodule, so without it those configuration sections won't work.
Also this ignores any possible existing header and library paths specified, so if there are any, do not forget to add them to those paths as well.

For `<LINUX_MAKE>` for desktop (`Builds/LinuxMakefile`, not to be confused with the one for `EMSCRIPTEN`, if exists), below:

```
        <CONFIGURATION name="Debug" isDebug="1"
            headerPath="../../aap-core/include"
            libraryPath="/usr/X11R6/lib/;../../aap-core/build/native/androidaudioplugin"
            />
        <CONFIGURATION name="Release" isDebug="0" optimisation="2"
            headerPath="../../aap-core/include"
            libraryPath="/usr/X11R6/lib/;../../aap-core/build/native/androidaudioplugin"
            />
```

For `<ANDROIDSTUDIO>`:

```
        <CONFIGURATION name="Debug" isDebug="1" 
                       headerPath="../../aap-core/include"
                       libraryPath="../../aap-core/androidaudioplugin/build/intermediates/cmake/debug/obj/${ANDROID_ABI}"
                       optimisation="1" linkTimeOptimisation="0"
                       recommendedWarnings="LLVM"/>
        <CONFIGURATION name="Release" isDebug="0" optimisation="3" linkTimeOptimisation="1"
                       headerPath="../../aap-core/include"
                       libraryPath="../../aap-core/androidaudioplugin/build/intermediates/cmake/release/obj/${ANDROID_ABI}"
                       recommendedWarnings="LLVM"/>
```

Once you made all those changes to .jucer, copy the resulting .jucer as `override.${APP_NAME}.jucer` and put it under `apps` directory.
Then run `make copy-and-patch-app-sources` to create the actual application in a copy. It is created under `apps/${APP_NAME}`

### juce_emscripten for UI (future)

**UPDATE:** as of current version juce_emscripten is disabled in favor of JUCE 6.0 migration. We might bring back wasm UI support later.

We use [juce_emscripten](https://github.com/Dreamtonics/juce_emscripten/), which is a fork of JUCE that extends its support to WebAssembly world using [emscripten](emscripten.org/).

At this state, we can use the official JUCE distribution, without forking and making any changes for AAP itself. But when we would like to build a plugin UI that can launch from within the host (DAW) application, the WebAssembly bundle is going to make it possible. juce_emscripten is needed for that purpose.

(Note that external UI integration is not implemented in the framework yet.)

Our repository makes use of [our own fork that supports JUCE 5.4.7 and Web MIDI API](https://github.com/atsushieno/JUCE/tree/juce_emscripten/) (which may not be required once Dreamtonics is back on that effort).

UPDATE: after some investigation, it turned out that current Chrome (and inherently Android WebView)  does not support [Actomics](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Atomics) which is essential to juce_emscripten internals, therefore it will not work on Android so far.
We keep using juce_emscripten so far though; it may become usable at some stage.


## Using AddressSanitizer on aap-juce apps

It is tricky to set up ASAN for aap-juce apps, especially for those projects that use Projucer (as it regenerates the projects every time you re-save .jucer).

The easiest way would be to copy `setup-asan-for-debugging.sh` to `(app)/Builds/Android/` directory, modify the script to have `ALL_APPS` variable to point to `app` directory, and run it there. For CMake-based projects, it can be just the top directory (and point to the source as well).

Then you have to make some changes to the build scripts (`build.gradle(.kts)` and `CMakeLists.txt`) to take those options. You can track how `enable_asan` variable in `aap-core` repository is used.

There is one more change needed: Projucer generates CMake arguments to contain `-DANDROID_STL_c++_static`. It has to be changed to `-DANDROID_STL=c++_shared`.

Note that if you are on Projucer based project, Projucer will clean up all your efforts every time it resaves the project.

## Hacking JUCEAAP modules

The easiest way to hack AAP JUCE integration itself would be still via soem ported app project on Android Studio.

Although on the other hand, JUCE integration on desktop is significantly easy as JUCE is primarily developed for desktop, if code to hack is not Android specific.
JUCE exporter for CLion may be useful for debugging (especially that Android Studio native builds are also for CMake either way). On CLion (verified with 2019.3) setting the project root with `Tools` -> `CMake` -> `Change Project Root` command would make it possible to diagnose issues with breakpoints on the sources from AAP itself (also LV2 dependencies and prebuilt LV2 plugins e.g. mda-lv2, but it is out of scope of this repo).

## Measuring performance

`juceaap_AAPWrappers.h` has a simple performance measurement aid which can be enabled with [`JUCEAAP_LOG_PERF` variable](https://github.com/atsushieno/aap-juce/blob/cc649d9/modules/juceaap_audio_plugin_client/juceaap_AAPWrappers.h#L18)

It prints plugin implementation processing time as well as JUCE-AAP wrapper processing time, so it would tell you which part takes time.

Here is an example output (measured with Magical8bitPlug2, simple 3 notes chord, 1024 frames, android-30 x86 emulator on KVM on Ubuntu 20.04 desktop). The last line is an additional logging at aaphostsample.

```
2020-07-11 01:44:22.330 26807-26827/com.ymck.magical8bitplug2juce I/AAPHostNative: AAP JUCE Bridge juceaap:73796e6a - Synth TAT: 1501484
2020-07-11 01:44:22.330 26807-26827/com.ymck.magical8bitplug2juce I/AAPHostNative: AAP JUCE Bridge juceaap:73796e6a - Process TAT: time diff 1525561
2020-07-11 01:44:22.330 26700-26829/org.androidaudioplugin.aaphostsample D/AAPHost Perf: instance process time: 1975494.0
```

## Code origin and license

This repository itself is licensed under the GPLv3 license.
