# AAP-JUCE: JUCE audio plugin and hosting support for AAP (Audio Plugins For Android)

This repo is the place where we have the common JUCE integration support modules for [aap-core](https://github.com/atsushieno/aap-core) (AAP), for both plugins and hosts.

The entire AAP framework is on early development phase and not ready for any serious consumption yet.
Everything is subject to change. Contributions are welcome but please bear in mind, documentation is poor and source code is ugly yet. We have GitHub discussions for AAP enabled at [aap-core](https://github.com/atsushieno/aap-core/discussions/landing) so please feel free to shoot your questions, if any.

## Existing ports

This repository used to contain a handful of sample projects, but to avoid bloat core library repository, they are split from here and have their own repositories. Now they are listed at [AAP Wiki](https://github.com/atsushieno/aap-core/wiki/List-of-AAP-plugins-and-hosts).

aap-juce-plugin-host can enumerate the installed AAP plugins on the system (not limited to aap-juce ones), and instantiate each plugin.

At this state, this repository itself is almost only about a set of build scripts that lets you port your (or others') JUCE audio plugins and hosts to AAP world. And probably more importantly, this README.

## Why JUCE for AAP?

JUCE is a popular cross-platform, multi-plugin-format audio development framework.
JUCE itself does not support AAP, but it can be extended by additional modules.
JUCE also supports Android (you can even run UI), which makes things closer to actual app production.
Still, JUCE is not designed to be extensible *enough*, additional code to support AAP is needed in each app.

While JUCE itself is useful to develop plugin formats like AAP, it is designed to be independent of any other audio development toolkits and frameworks. We stick to minimum dependencies, at least on the public API surface.

JUCE API is stable-ish, while AAP API is not. So if anyone wants to get audio plugins portable without fear of API breakage, JUCE can be your good friend.

Note that JUCE plugins are usually designed for desktop and not meant to be usable on mobiles. In particular -

- their UIs are usually useless on mobile.
- those plugins that lets user pick up local files would not fit well with Android paradigm.
- Some plugins would expose performance issues too.

## How to try it out?

You would have to build and install each host and/or plugin from source so far, or look for the APKs from the build artifacts zip archives from successful GitHub Actions workflow runs. They could be installed via [AAP APK Installer](https://github.com/atsushieno/aap-ci-package-installer). Note that not all plugins are updated and there can be incompatible AAP protocol versions of the plugins, or outdated to not work properly on newer Android platforms.

You need a host app and a plugin to try at least one plugin via one host. This repository does not contain any application within itself.

The host can be either `aaphostsample` in aap-core repo, a project [aap-juce-simple-host](https://github.com/atsushieno/aap-juce-simple-host) that is somewhat tailored for AAP and mobile UI, or `AudioPluginHost` in [aap-juce-plugin-host-cmake](https://github.com/atsushieno/aap-juce-plugin-host) repo (which is JUCE AudioPluginHost with AAP support).

The plugin can be either `aappluginsample` in aap-core repo (more stable), or any plugin on the [AAP Wiki](https://github.com/atsushieno/aap-core/wiki/List-of-AAP-plugins-and-hosts).

For those `aap-juce-*` repositories, `make` will build them. (`Makefile` does not sound cool, but it is at least distinct from `CMakeLists.txt` for Android app itself...)

For JUCE apps that use Projucer, JUCE Android apps are generated and built under `Builds/Android/app/build/outputs/` in each app directory.
Though we typically use Android Studio and open `Builds/Android` and then run or debug there, once top-level `make` ran successfully.

For those projects that use CMake, it is `app/build/outputs`.


## App Build Instruction

### CI builds

aap-juce itself is a set of JUCE modules that itself does not "build". But still, there are [reusable GitHub Actions workflows](https://docs.github.com/en/actions/using-workflows/reusing-workflows) under `.github` directory: one for CMake based projects and another for Projucer based projects. They are kind of normative build instructions (in that it is offered by ourselves).

The actual build is one `make` call if all conditions are met. In other words, every project that uses the workflow is adjusted to build with that.

As long as other project follows the same structure, you can reuse it too. It is safe to use the versioned workflow by commit rev. as we will be making breaking changes.

### Building locally

As a prerequisite, you need Android SDK. If you install it via Android Studio it is usually placed under `~/Android/Sdk` on Linux, and `~/Library/Android/sdk` on MacOS.
You also need Android NDK, most likely at least r23 (our latest development version uses newer ones).

For those project that use Projucer: depending on the NDK setup you might also have to rewrite `Makefile` and `Builds/Android/local.properties` to point to the right NDK location. Then run `cd Builds/Android && ./gradlew build` instead of `./projuce-app.sh`.
It would be much easier to place Android SDK and NDK to the standard location though. Symbolic links would suffice.

CMake based projects would similarly need to tweak `Makefile`, but other than that it is a typical Android Studio (Android Gradle) project.

## Additional notes on Projucer

aap-juce basically recommends CMake for porting JUCE plugins to Android, but it is still technically possible to use Projucer-based projects for porting.

Since Projucer support needs quite a lot of more documentation, we have consolidated Projucer-related topics to [docs/PROJUCER.md](docs/PROJUCER.md) to keep this document cleaner.

## Porting a plugin or a host to AAP

To port existing plugins, or even with a new project, you will either follow the CMake way, or the Projucer way.
You would normally have no choice, the original project would be either of those already, but CMake is much easier and much more intuitive to deal with. With Projucer, you will have to generate project every time you make changes to the project.
In either approach, you end up with an Android Studio (Gradle) project that you can open on Android Studio (or stick to Gradle to build and adb to install, like we do on CI).

There are many aap-juce based apps that could be used as reference/template projects. See the list of plugins on the AAP Wiki.

### Patching

In most aap-juce-* apps, I created `aap-juce-support.patch` that (for each app) contains a set of changes as in patch files. In `Makefile`, there is `PATCH_FILE` variable that indicates one single patch file. It usually points to that `aap-juce-support.patch`.

IF the build system is Projucer, there is an additional variable called `PATCH_DEPTH` which is used with `patch` tool as: `patch -i [aap-juce-support.patch] -p [PATCH_DEPTH]`. For CMake projects -p 1 should suffice so I used `git apply` which does not take patch depth instead.

### Making application itself build for Android

If you are trying to port an existing project to Android, the first thing to do is to make it build for Android, regardless of AAP.
If it is a Projucer project, open Projucer, add Android Studio exporter, resave, open on Android Studio and try to build.
If it is a CMake project, check the next  section ("Make AAP-specific changes: CMake").

JUCE API is in general less featureful on Android than desktop. For example, you have no access to synchronous dialog boxes.
So you will have to rewrite code to follow asynchronous way or disable code so far. Once it's done, let's mark it as **the** porting target application.
 (They were deprecated in recent JUCE versions in 2021, so newer projects would suffer less.)

### Make AAP-specific changes: CMake

For a project that build with CMake, it is fairly easier to port to AAP.

There are existing sample projects listed earlier on our [AAP wiki page](https://github.com/atsushieno/aap-core/wiki/List-of-AAP-plugins-and-hosts), and to port an existing plugin project (or even if you are going to create a new JUCE plugin project), it is easier to create new project based off of those existing ones, making changes to your plugin specifics. For details, see [docs/PLUGIN_PORTING_GUIDE.md](docs/PLUGIN_PORTING_GUIDE.md).

There are some important bits:

- JUCE needs some changes. [JUCE-support-Android-CMake.patch](JUCE-support-Android-CMake.patch) is applied if you build those ports with `make`.
- The plugin's top level `CMakeLists.txt` : we need `Standalone` plugin build (as no other formats are supported, and on Android it is built as a shared library). We also (typically) patch this file to add build setup for AAP specifics.
- `app/src/main/cpp`: is typically a symbolic link to the JUCE plugin app source (e.g. `cpp` -> `../../../external/dexed`).

A typical porting trouble we encounter is that even if the activity launches, it shows an empty screen.
It is because the application somehow fails to bootstrap (call to `com.rmsl.juce.Java.initialiseJUCE()`).

Another typical problem is that it fails to load the plugin shared library. It is due to missing shared library within the apk, inconsistent `aap_metadata.xml` description (either `library` or `entrypoint` attribute), or missing entrypoint function symbol in the shared library (which must not be `visibility=hidden`).


### Creating aap_metadata.xml

Starting aap-core 0.7.7 along with its parameters extension v2.1, it supports dynamic parameter population by code, and aap-juce fully makes use of it. This feature makes `aap_metadata.xml` simply editable by human beings i.e. you can just copy existing `aap_metadata.xml` from other project and replace "names" in it.

It used to be "generated automatically". The next section explained that, but it is totally passable anymore.

### Generating and updating aap_metadata.xml

NOTE: this only applies to aap-juce 0.4.8 or earlier. The generator is not part of the build anymore. You can however still try to build and use it by making changes to aap-juce codebase. The build tasks in [the old `Makefile.common`](https://github.com/atsushieno/aap-juce/blob/135477ef53b1e6585463eebb92d5e06db62674e9/Makefile.common#L191) and [`generate-metadata.sh`](https://github.com/atsushieno/aap-juce/blob/135477ef53b1e6585463eebb92d5e06db62674e9/generate-metadata.sh#L1) would be helpful.

This only applies to plugins (hosts do not need it).

`aap_metadata.xml` has to be created for the plugin.

Starting aap-juce 0.4.7, we do not really have to "generate" it automatically - we could just copy some `aap_metadata.xml` and modify it to match your project (especially, names and the native library filename). Therefore, we deprecate the following paragraphs.

<del>
There is some non-trivial task: `aap_metadata.xml` has to be created for the plugin. Sometimes it can be hundreds of lines of markup, so you would like to automatically generate from existing code.
</del>

<del>
There is `update-aap-metadata` target in `Makefile.common` or `update-aap-metadata-cmake` target in `Makefile.cmake-common`, but in case you would like to run it manually...
</del>

<del>
To import JUCE audio plugins into AAP world, we have to create plugin
descriptor (`aap_metadata.xml`). We have a supplemental tool source to
help generating it automatically from JUCE plugin binary (shared code).
</del>

### Dealing with different JUCE versions

JUCE sometimes involves incompatible changes between versions, and sometimes they involve those project file generators. We have some Makefile variables that changes the behavior. It is worth inspecting `Makefile.common` for Projucer-based project and `Makefile.cmake-common` for CMake-based projects (they are often added).


## Under the hood

JUCE itself already supports JUCE apps running on Android and there is still no need to make any changes to the upstream JUCE, unless you want to build CMake based project (see [this post by @atsushieno](https://atsushieno.github.io/2021/01/16/juce-cmake-android-now-works.html) for details).

On the other hand, we make a lot of changes to whatever Projucer generates.
Projucer is not capable of supporting arbitrary plugin format and it's quite counter-intuitive to try to use various option fields on Projucer.
Thus we simply replace various parts of the generated Android Gradle project.
It is mostly taken care by `projuce-app.sh`.

CMake is preferred, but it is not officially supported by JUCE so far either.
However the impact of the changes is small so that they can be manually fixed.

We plan to juce_emscripten for future integration with wasm UI builds.

### difference between normal JUCE Android app and JUCE-AAP Android app

We have a lot of works for Projucer-based projects here, but things are much simpler for CMake support. We simply use our own app template with slight changes and reference to `CMakeLists.txt` from the app that is being ported.

### GUI limitation

aap-core since 0.7.7 provides preliminary native plugin-process GUI functionality, but since JUCE GUI does not work as a standalone `View`, unlike Jetpack Compose,or Flutter, it is impossible to reuse existing desktop UI at the moment. JUCE plugins even invalidates whatever `Activity` we specify at `AndroidManifest.xml` and launches its own `JuceActivity` instead. It is a brutal behavior, but we cannot fix without a various patching effort.

At the moment, you are encouraged to build a dedicated mobile UI for JUCE based plugins, without `juce_gui_basics`. Or push JUCE team to improve their GUI support to make it just work as `View`.


## Using AddressSanitizer on aap-juce apps

It is tricky to set up ASAN for aap-juce apps, especially for those projects that use Projucer (as it regenerates the projects every time you re-save .jucer).

The easiest way would be to copy `setup-asan-for-debugging.sh` to `(app)/Builds/Android/` directory, modify the script to have `ALL_APPS` variable to point to `app` directory, and run it there. For CMake-based projects, it can be just the top directory (and point to the source as well).

Then you have to make some changes to the build scripts (`build.gradle(.kts)` and `CMakeLists.txt`) to take those options. You can track how `enable_asan` variable in `aap-core` repository is used.

There is one more change needed: Projucer generates CMake arguments to contain `-DANDROID_STL_c++_static`. It has to be changed to `-DANDROID_STL=c++_shared`.

Note that if you are on Projucer based project, Projucer will clean up all your efforts every time it resaves the project.

## Profiling

Both `aap_audio_plugin_client` and `aap_audio_processors` implement profiling support using ATrace API, just like aap-core and aap-lv2 do. The profiling is done at both including and excluding aap-juce specific parts.

For more details on AAP tracing, read the [aap-core documentation](https://github.com/atsushieno/aap-core/blob/e9a28aa7f382a0c30b8b378b6809d2effa25e002/docs/DEVELOPERS.md#profiling-audio-processing) (it is a permalink; there may be updated docs).

## Code origin and license

This repository itself is licensed under the GPLv3 license.
