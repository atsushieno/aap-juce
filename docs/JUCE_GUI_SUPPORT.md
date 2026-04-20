# Bringing JUCE GUI Support to an AAP Plugin

This is a Codex-generated documentation while @atsushieno was working on procedualizing migration steps to JUCE plugin UI.

----

This guide describes the moving parts needed to expose a JUCE plugin editor as
an AAP native GUI through `JuceAudioPluginViewFactory`.

The goal is to make existing AAP JUCE plugin projects use the plugin's real
JUCE editor instead of a Compose/Web fallback UI. The host creates a remote
Android `View`; the plugin process creates the JUCE editor and attaches its
Android peer into that hosted view.

## Requirements

- `aap-core` and `aap-juce` versions that include the GUI extension and
  `org.androidaudioplugin.juce.JuceAudioPluginViewFactory`.
- A plugin that can create a JUCE `AudioProcessorEditor`.
- Android API level 29 or newer for hosts that use `SurfaceControlViewHost`.
- A build that applies the required `aap-juce` JUCE patches before compiling.

## Android Manifest

Use the Compose AAP activity as the launcher, not the JUCE standalone activity.
The JUCE activity may still be declared because JUCE internals expect the class,
but it should not be the main entry point.

The plugin app must also expose `AudioPluginViewService` and initialize JUCE
through AndroidX Startup:

```xml
<application
    android:label="@string/app_name"
    android:icon="@drawable/ic_launcher"
    android:hardwareAccelerated="false">

  <activity
      android:name="org.androidaudioplugin.ui.compose.app.PluginManagerActivity"
      android:hardwareAccelerated="true"
      android:exported="true">
    <intent-filter>
      <action android:name="android.intent.action.MAIN" />
      <category android:name="android.intent.category.LAUNCHER" />
    </intent-filter>
  </activity>

  <activity
      android:name="com.rmsl.juce.JuceActivity"
      android:label="@string/app_name"
      android:configChanges="keyboardHidden|orientation|screenSize"
      android:screenOrientation="userLandscape"
      android:launchMode="singleTask"
      android:hardwareAccelerated="true"
      android:exported="true" />

  <service
      android:name="org.androidaudioplugin.AudioPluginService"
      android:label="@string/app_name"
      android:foregroundServiceType="mediaPlayback"
      android:exported="true">
    <intent-filter>
      <action android:name="org.androidaudioplugin.AudioPluginService.V3" />
    </intent-filter>
    <meta-data
        android:name="org.androidaudioplugin.AudioPluginService.V3#Plugins"
        android:resource="@xml/aap_metadata" />
  </service>

  <service
      android:name="org.androidaudioplugin.AudioPluginViewService"
      android:exported="true" />

  <provider
      android:name="androidx.startup.InitializationProvider"
      android:authorities="${applicationId}.androidx-startup"
      android:exported="false"
      tools:replace="android:authorities"
      tools:node="merge">
    <meta-data
        android:name="org.androidaudioplugin.juce.JuceAppInitializer"
        android:value="androidx.startup" />
  </provider>
</application>
```

The `JuceAppInitializer` call is important. It runs
`com.rmsl.juce.Java.initialiseJUCE(context.getApplicationContext())` before
the plugin editor is attached to the hosted view.

We also have to make sure that `app/build.gradle` contains the reference to `libs.aap.ui.compose.app`:

```
    dependencies {
        ...
        implementation libs.aap.ui.compose.app
    }
```

## Gradle Java Sources

The Android app must compile JUCE Java sources from the same patched JUCE tree
that `JUCE_DIR` points at. Applying the C++/JUCE patches from the `Makefile` is
not enough if Gradle still compiles stale Java sources from a generated
`third_party/JUCE` copy or another unpatched tree.

The app must also compile the Java side of `aap-juce`, which provides
`JuceAudioPluginViewFactory` and the hosted JUCE editor wrapper.

In `app/build.gradle`, add or replace the relevant `sourceSets` entry:

```gradle
android {
    sourceSets {
        main.java.srcDirs +=
            ["../external/YourPluginSource/external/JUCE/modules/juce_gui_basics/native/java/app",
             "../external/YourPluginSource/external/JUCE/modules/juce_gui_basics/native/javaopt/app",
             "../external/aap-juce/java"]
    }
}
```

Use paths relative to the `app/build.gradle` file. Some JUCE/Projucer layouts
also need `juce_core/native/javacore/...` source directories; if the generated
project already referenced JUCE Java source directories, keep the same set of
modules but point them at the patched JUCE tree.

Do not keep two different JUCE Java trees in the same app source set. If the
app compiles stale `com.rmsl.juce.*` classes, the metadata and native factory
can look correct while the native UI still fails to show up or uses old input
routing behavior.

## AAP Metadata

In `app/src/main/res/xml/aap_metadata.xml`, declare the GUI extension and set
the plugin's GUI view factory to `JuceAudioPluginViewFactory`:

```xml
<plugins xmlns="urn:org.androidaudioplugin.core">
  <plugin
      name="YourPlugin"
      category="Instrument"
      author="Your Name"
      developer="Your Name"
      unique-id="juceaap:...."
      library="libYourPlugin_Standalone.so"
      entrypoint="GetJuceAAPFactoryStandalone"
      gui:ui-view-factory="org.androidaudioplugin.juce.JuceAudioPluginViewFactory"
      xmlns:gui="urn://androidaudioplugin.org/extensions/gui">
    <extensions>
      <extension uri="urn://androidaudioplugin.org/extensions/plugin-info/v3" />
      <extension uri="urn://androidaudioplugin.org/extensions/parameters/v3" />
      <extension uri="urn://androidaudioplugin.org/extensions/presets/v3" />
      <extension uri="urn://androidaudioplugin.org/extensions/state/v3" />
      <extension uri="urn://androidaudioplugin.org/extensions/gui/v3" />
    </extensions>
  </plugin>
</plugins>
```

Keep `library` and `entrypoint` consistent with the actual shared library and
factory symbol in the APK. A wrong value here can look like a GUI failure even
when the native editor code is correct.

## Makefile Wiring

The top-level `Makefile` should point to the app source tree, the app's JUCE
tree, the app support patch, and the JUCE patches that are needed by the
current `aap-juce` version.

Example:

```make
PWD=$(shell pwd)
AAP_JUCE_DIR=$(PWD)/external/aap-juce

APP_NAME=YourPlugin
APP_BUILD_DIR=$(PWD)
APP_SRC_DIR=$(PWD)/external/YourPluginSource
JUCE_DIR=$(APP_SRC_DIR)/external/JUCE

APP_SHARED_CODE_LIBS="YourPlugin_artefacts/libYourPlugin_SharedCode.a"

PATCH_FILE=$(PWD)/aap-juce-support.patch
PATCH_DEPTH=1

JUCE_PATCHES= \
        $(shell pwd)/external/aap-juce/juce-patches/7.0.12/disable-cgwindowlistcreateimage.patch \
        $(shell pwd)/external/aap-juce/juce-patches/7.0.6/support-plugin-ui.patch \
        $(shell pwd)/external/aap-juce/juce-patches/7.0.11/juce-component-peer-view-touch.patch

include $(AAP_JUCE_DIR)/Makefile.cmake-common
```

If one of the stock JUCE patches does not apply to the app's JUCE revision,
create a replacement patch under `external/aap-juce/juce-patches/...` and
reference that patch from this top-level `Makefile`.

## App Source Patch

Most ports need a project-local patch, commonly named
`aap-juce-support.patch`. This patch should contain only changes that are
specific to adapting the upstream plugin project to AAP.

Common changes include:

- Adding `aap_audio_processors` to the app's CMake build.
- Linking `androidaudioplugin::androidaudioplugin`.
- Avoiding `aap_audio_plugin_client` in plugin APK targets. That module is
  host-side code, not plugin-side editor support.
- Adding Android include/library paths needed by the plugin build.
- Adding a dummy `JuceHeader.h` when the upstream source expects Projucer-style
  generated headers but the Android/CMake build does not create one.
- Replacing blocking JUCE modal APIs with async APIs.
- Preventing popup menus and dialogs from using an Android desktop peer outside
  the plugin editor's hosted view.

For CMake-based plugin ports, the AAP-specific block should normally look like
this:

```cmake
if(ANDROID)
find_package(androidaudioplugin REQUIRED CONFIG)

juce_add_modules(${AAP_JUCE_DIR}/aap-modules/aap_audio_processors)

target_link_libraries(${APP_NAME}
  PRIVATE
  aap_audio_processors
  androidaudioplugin::androidaudioplugin
  )
endif()
```

The `androidaudioplugin::androidaudioplugin` link is not optional once the
current AAP/JUCE GUI wrapper is compiled into the plugin. Without it, link
errors may appear as missing AAP host symbols such as:

```text
ld.lld: error: undefined symbol: aap::PluginHost::getInstanceById(int)
```

If you see that symbol in a plugin port, first check that the plugin target
links `androidaudioplugin::androidaudioplugin`, and also check that you did not
accidentally add `aap_audio_plugin_client`. `aap_audio_plugin_client` is for
JUCE-based AAP hosts that instantiate remote plugins; plugin packages exposing
their own JUCE editor should use `aap_audio_processors`.

For projects that need a dummy `JuceHeader.h`, keep it minimal:

```cpp
// It is a dummy header file.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
using namespace juce;
```

## Preferred GUI Size

The native GUI size should come from the plugin editor when possible.
`JuceAudioPluginViewFactory.getPreferredSize()` calls into the native wrapper,
which asks the JUCE editor for its preferred size. Hosts can clamp that size to
their display area, safe insets, title bars, and scroll container.

Do not hard-code sizing for one plugin. If the plugin editor has a meaningful
width and height, expose that value. If it does not, fix the plugin/editor side
or add generic fallback logic in the AAP/JUCE layer.

## Popup Menus and Dialogs

JUCE popup components can break when they call `addToDesktop()` in an AAP
plugin process. The plugin UI is not a normal Android standalone app window;
it is a remote view hosted by another process.

Prefer anchoring popup menus to the plugin editor component:

```cpp
PopupMenu::Options Custom_Look_And_Feel::getOptionsForComboBoxPopupMenu(
    ComboBox &box, Label &label)
{
    auto options = Base::getOptionsForComboBoxPopupMenu(box, label);

    if (auto *parent = box.getTopLevelComponent())
        options = options.withParentComponent(parent);

    return options;
}
```

For explicit menus, prefer `showMenuAsync()` and anchor to a target component
or parent component:

```cpp
menu.showMenuAsync(
    PopupMenu::Options().withTargetComponent(button),
    [this](int selection) {
        // handle selection
    });
```

Avoid synchronous modal calls such as `showMenu()`,
`AlertWindow::showMessageBox()`, and blocking `FileChooser` APIs in plugin UI
paths. Use async equivalents so the Android/UI message loop is not blocked.

## Maintaining `aap-juce-support.patch`

The safest workflow is:

1. Start from a clean app source tree.
2. Apply the current patch:

   ```sh
   git -C external/YourPluginSource apply --ignore-space-change ../../aap-juce-support.patch
   ```

3. Edit source files directly.
4. Regenerate the patch from real source diffs.

For projects that contain nested submodules, do not use raw
`git diff --submodule=diff` output as-is if it includes lines like:

```text
Submodule external/SomeSubmodule contains modified content
```

That line is not a valid patch hunk. If it lands in `aap-juce-support.patch`,
CI may fail to create files such as `JuceHeader.h`, then fail later during C++
compilation.

Generate nested submodule diffs with the path prefix expected from the app
source root. For example:

```sh
{
  git -C external/YourPluginSource diff -- CMakeLists.txt
  git -C external/YourPluginSource/external/NestedSubmodule \
      diff --src-prefix=a/external/NestedSubmodule/ \
           --dst-prefix=b/external/NestedSubmodule/
  git -C external/YourPluginSource/external/NestedSubmodule \
      diff --no-index \
           --src-prefix=a/external/NestedSubmodule/ \
           --dst-prefix=b/external/NestedSubmodule/ \
           /dev/null sources/JuceHeader.h || true
} > aap-juce-support.patch
```

Then validate both directions:

```sh
git -C external/YourPluginSource apply --check --ignore-space-change \
    ../../aap-juce-support.patch

git -C external/YourPluginSource apply --check --reverse --ignore-space-change \
    ../../aap-juce-support.patch
```

## Verification Checklist

- `git apply --check` succeeds on a clean app source tree.
- `git apply --check --reverse` succeeds on a patched tree.
- `make build` succeeds locally.
- `app/build.gradle` source sets point at the patched JUCE Java sources and
  `external/aap-juce/java`, not stale generated JUCE Java sources.
- The plugin appears in AAP hosts.
- Opening, closing, and reopening the native UI works.
- The editor reports a sensible preferred size.
- Controls react at the touched coordinates.
- Combo boxes, preset selectors, popup menus, and about dialogs render inside
  the hosted plugin UI.
- The UI works in at least these host styles when available:
  - Compose host such as AAPHostSample.
  - SDL/ImGui host such as UAPMD.
  - JUCE host using `juce_audio_plugin_client`.

