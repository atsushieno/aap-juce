# Projucer support in aap-juce


## Porting a plugin or a host to AAP

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

Once you are done with the modified .jucer file, you still need `aap_metadata.xml` for your project. IF everything works fine, it can be achieved by `make update-aap-metadata` or `make update-aap-metadata-cmake` to generate it (explained later too), but it requires independent desktop build and may often fail, depending on your project. **It is much easier if you manually fill the metadata**. If you still decided to run the generator, then copy it somewhere with some identical name, typically under `apps`.
`make copy-and-patch-app-sources` will copy it if the file is appropriately specified in `Makefile` and it indeed exists.

After these tasks, you are ready to run `make copy-and-patch-app-sources` for your own port.

If you are not making changes to .jucer file, you can keep working with the generated Android Studio project and diagnose any issues.
If you run Projucer to resave the project, the project is overwritten, which invalidates project fixup for AAP (we rewrite AndroidManifest.xml, build.gradle, and so on), so you should avoid that.
Instead, run `make projuce-app` to resave projects and perform required overwrites.

Lastly, for hosts, `AndroidAudioPluginFormat` has to be added to the app's `AudioPluginFormatManager` instances in the application code (example [1](https://github.com/atsushieno/aap-juce-plugin-host/blob/295aa69b48a25f798520d9a365b611a081a0a725/apps/juceaaphost.patch#L62)/[2](https://github.com/atsushieno/aap-juce-plugin-host/blob/295aa69b48a25f798520d9a365b611a081a0a725/apps/juceaaphost.patch#L102)).


## Under the hood

### Makefile Tasks for Projucer-based projects

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

### Rewriting .jucer file for AAP

Here are the porting steps that we had. Note that this applies only to `atsushieno/aap-juce-*` projects that make use of the helper files in this repository:

- Run Projucer and open the project `.jucer` file.
- open project settings and ensure to build at least one plugin format (`Standalone` works)
- Ensure that `JUCEPROJECT` has `name` attribute value only with `_0-9A-Za-z` characters. That should be handled by Projucer but its hands are still too short.
  - For example, we had to rename `Andes-1` to `Andes_1` and `OB-Xd` to `OB_Xd`.
- Go to Modules and add module `aap_audio_processors` (via path, typically)
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
- Add `<MODULEPATH id="aap_audio_pprocessors" path="./aap-modules"/>` for each `<MODULEPATHS>` for each `<EXPORTFORMAT>`. (You can do this in Projucer too, but copypasting on a text editor is 10x easier.)
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
