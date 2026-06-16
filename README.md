# AAP-JUCE: JUCE audio plugin and hosting support for AAP (Audio Plugins For Android)

AAP-JUCE is a part of [AAP (Audio Plugins For Android) project](https://github.com/atsushieno/aap-core) that integrates JUCE apps (plugins and hosts) to Android.

This repo is the place where we have the common JUCE integration support modules, as well as various patches for handful of different JUCE versions.

CAUTION: do not expect stability too much. The entire AAP framework is still under active development and will keep bringing in breaking API/ABI changes.
Also, since JUCE itself does not support AAP or anything like AAP that works as an Android Service, there are handful of local changes that try to make it work within the AAP basis.

We partially care about backward compatibility, but everything is still subject to change. Contributions are welcome but please bear in mind, documentation is poor and source code is ugly. We have GitHub discussions for AAP enabled at [aap-core](https://github.com/atsushieno/aap-core/discussions/landing), not in aap-juce repo, so please feel free to shoot your questions there.

## Existing ports

We have lots of JUCE plugins ported to AAP already. They are listed at [AAP Wiki](https://github.com/atsushieno/aap-core/wiki/List-of-AAP-plugins-and-hosts).

aap-juce-simple-host can enumerate the installed AAP plugins on the system (not limited to aap-juce ones), and instantiate each plugin.

At this state, this repository itself is almost about a set of build scripts that lets you port your (or others') JUCE audio plugins and hosts to AAP world, as well as a bunch of JUCE patches to make it realize. And probably more importantly, this README.

## Why JUCE for AAP?

JUCE is a popular cross-platform, multi-plugin-format audio development framework.
JUCE itself does not support AAP, but it can be extended by additional modules.
JUCE also supports Android (you can even run UI), which makes things closer to actual app production.
Still, JUCE is not designed to be extensible *enough*, additional code to support AAP is needed in each app. And when it comes to GUI support, many apps are not quite ready for Android.

While JUCE itself is useful to develop plugin formats like AAP, it is designed to be independent of any other audio development toolkits and frameworks. We stick to minimum dependencies, at least on the public API surface.

JUCE API is stable-ish, while AAP API is not. So if anyone wants to build AAP plugins portable without fear of API breakage, JUCE can be your good friend.

Note that JUCE plugins are usually designed for desktop and not meant to be usable on mobiles. In particular -

- their UIs usually don't build, and/or are usually useless on mobile.
- those plugins that lets user pick up local files would not fit well with Android paradigm.
- they frequently expose performance issues too.

## How to try it out?

To avoid duplicates I removed description here. Check out [aap-core](https://github.com/atsushieno/aap-core) README instead.

The host can be either `aaphostsample` in aap-core repo, a project [aap-juce-simple-host](https://github.com/atsushieno/aap-juce-simple-host) that is somewhat tailored for AAP and mobile UI, `AudioPluginHost` in [aap-juce-plugin-host-cmake](https://github.com/atsushieno/aap-juce-plugin-host) repo (which is JUCE AudioPluginHost with AAP support), or [UAPMD](https://github.com/atsushieno/uapmd). Since JUCE hosts themselves are not really reliable as JUCE is in general not well designed and tailored for Android, @atsushieno recommends UAPMD as the most reliable native host application as of 2026.


## App Build Instruction

For those `aap-juce-*` repositories, `make` will build them. (`Makefile` does not sound cool, but it is at least distinct from `CMakeLists.txt` for Android app itself...)

### CI builds

aap-juce itself is a set of JUCE modules that itself does not "build". But still, there are [reusable GitHub Actions workflows](https://docs.github.com/en/actions/using-workflows/reusing-workflows) under `.github` directory: one for CMake based projects and another for Projucer based projects. They are kind of normative build instructions (in that it is offered by ourselves).

The actual build is one `make` call if all conditions are met. In other words, every aap-juce-* project that uses the workflow is adjusted to build with that.

As long as other project follows the same structure, you can reuse it too. It is safe to use the versioned workflow by commit rev. as we will be making breaking changes.

### Building locally

As a prerequisite, you need Android SDK. If you install it via Android Studio it is usually placed under `~/Android/Sdk` on Linux, and `~/Library/Android/sdk` on MacOS.
You also need Android NDK, most likely at least r23 (our latest development version uses newer ones).

For those project that use Projucer: depending on the NDK setup you might also have to rewrite `Makefile` and `Builds/Android/local.properties` to point to the right NDK location. Then run `cd Builds/Android && ./gradlew build` instead of `./projuce-app.sh`.
It would be much easier to place Android SDK and NDK to the standard location though. Symbolic links would suffice.

CMake based projects would similarly need to tweak `Makefile`, but other than that it is a typical Android Studio (Android Gradle) project.

### Build outputs

For JUCE apps that still use Projucer (we often scrap them and create `CMakeLists.txt` by ourselves in our ports), JUCE Android apps are generated and built under `Builds/Android/app/build/outputs/` in each app directory.
Though we typically use Android Studio and open `Builds/Android` and then run or debug there, once top-level `make` ran successfully.

For those projects that use CMake, the build outputs are at `app/build/outputs`.

## Additional notes on Projucer

aap-juce basically recommends CMake for porting JUCE plugins to Android, but it is still technically possible to use Projucer-based projects for porting.

Since Projucer support needs quite a lot of more documentation, we have consolidated Projucer-related topics to [docs/PROJUCER.md](docs/PROJUCER.md) to keep this document cleaner.


## Porting a plugin or a host to AAP

See the dedicated [PORTING doc](docs/PORTING.md) or if it still helps somewhat outdated step-by-step [PORTING GUIDE doc](docs/PLUGIN_PORTING_GUIDE.md).

In 2026 or later, coding AI agents would help you prform the actual porting work for you.


## Under the hood

JUCE itself already supports JUCE apps running on Android and there is still no need to make any changes to the upstream JUCE, unless you want to build CMake based project (see [this post by @atsushieno](https://atsushieno.github.io/2021/01/16/juce-cmake-android-now-works.html) for details).

On the other hand, we make a lot of changes to whatever Projucer generates.
Projucer is not capable of supporting arbitrary plugin format and it's quite counter-intuitive to try to use various option fields on Projucer.
Thus we simply replace various parts of the generated Android Gradle project.
It is mostly taken care by `projuce-app.sh`.

CMake is preferred, but it is not officially supported by JUCE so far either.
However the impact of the changes is small so that they can be manually fixed.

### difference between normal JUCE Android app and JUCE-AAP Android app

We have a lot of works for Projucer-based projects here, but things are much simpler for CMake support. We simply use our own app template with slight changes and reference to `CMakeLists.txt` from the app that is being ported.

### Integrating `juce::AudioProcessorEditor` (GUI)

aap-core since 0.7.7 provides preliminary native plugin-process GUI functionality, but since JUCE GUI does not work as a standalone `View`, unlike Jetpack Compose, or Flutter, it is impossible to reuse existing desktop UI without tweaking JUCE to some extent. JUCE plugins even invalidates whatever `Activity` we specify at `AndroidManifest.xml` and launches its own `JuceActivity` instead. It is a brutal behavior, and we need a handful of patches to fix this.

For further JUCE GUI integration, see [docs/JUCE_GUI_SUPPORT.md](docs/JUCE_GUI_SUPPORT.md).


## Profiling

Both `aap_audio_plugin_client` and `aap_audio_processors` implement profiling support using ATrace API, just like aap-core and aap-lv2 do. The profiling is done at both including and excluding aap-juce specific parts.

For more details on AAP tracing, read the [aap-core documentation](https://github.com/atsushieno/aap-core/blob/e9a28aa7f382a0c30b8b378b6809d2effa25e002/docs/DEVELOPERS.md#profiling-audio-processing) (it is a permalink; there may be updated docs).

## Code origin and license

This repository itself is licensed under the AGPLv3 license (in sync with JUCE itself).
