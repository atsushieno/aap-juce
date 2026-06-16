# PORTING details

To port existing plugins, or even with a new project, you will either follow the CMake way, or the Projucer way.
You would normally have no choice, the original project would be either of those already, but CMake is much easier and much more intuitive to deal with. With Projucer, you will have to generate project every time you make changes to the project.
In either approach, you end up with an Android Studio (Gradle) project that you can open on Android Studio (or stick to Gradle to build and adb to install, like we do on CI).
Note that those Projucer-based projects won't expose usefull project structure so that those C++ sources won't show up. Only valid CMake setup provides them.

There are many aap-juce based apps that could be used as reference/template projects. See the list of plugins on the AAP Wiki.

### Patching

In most aap-juce-* apps, we created `aap-juce-support.patch` that (for each app) contains a set of changes as in patch files. In `Makefile`, there is `PATCH_FILE` variable that indicates one single patch file. It usually points to that `aap-juce-support.patch`.

IF the build system is Projucer, there is an additional variable called `PATCH_DEPTH` which is used with `patch` tool as: `patch -i [aap-juce-support.patch] -p [PATCH_DEPTH]`. For CMake projects -p 1 should suffice so we used `git apply` which does not take patch depth instead.

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

