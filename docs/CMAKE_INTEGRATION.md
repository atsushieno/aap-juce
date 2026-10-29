# CMake integration (AI slop)

The repository root can be loaded with `add_subdirectory()`. This is an
additive interface for CMake ports: existing Makefiles, scripts, patches,
templates, Java files, and JUCE module paths remain available at their existing
locations. Projucer ports, including Vital and Helio, continue to use the old
workflow.

The helpers require CMake 3.22 and Git. They use no shell scripts and do not
build or execute Android binaries on the host. Adding the directory only loads
functions; it does not fetch dependencies or patch sources.

## Checked-out sources

Put the integration directly in the upstream application's `CMakeLists.txt`
and include it in the existing `aap-juce-support.patch`. Gradle points at that
entry point; no per-port configuration file or wrapper is required.

The beginning of a patched Dexed `CMakeLists.txt` looks like:

```cmake
cmake_minimum_required(VERSION 3.22)
get_filename_component(app_source "${CMAKE_CURRENT_LIST_DIR}" REALPATH)
add_subdirectory("${app_source}/../aap-juce" "${CMAKE_CURRENT_BINARY_DIR}/aap-juce" EXCLUDE_FROM_ALL)
aap_juce_configure(JUCE_SOURCE_DIR libs/JUCE ICON assets/ui/dexedIcon.png ADOPT_EXISTING_PATCHES)

project(Dexed VERSION 1.0.1)
# Existing upstream JUCE loading and plugin target creation follows here.
# After the Dexed target exists:
aap_juce_plugin(TARGET Dexed)
```

The configuration call must precede `project()`, compiler discovery, and JUCE
loading. The shared helper detects Android and handles the host preparation
mode internally. During host preparation it stages sources and resources, then
returns from the calling `CMakeLists.txt` before configuring native targets.
Desktop builds receive the AAP paths and JUCE version without Android patching
or resource staging. Port developers do not need execution-mode guards around
the helper calls. Relative paths use the application source directory in both
modes. The port locates aap-juce once in `add_subdirectory()`; that call exports
`AAP_JUCE_DIR` from the loaded directory. Resolving the application directory
first makes the port's relative import work through the Android Studio symlink
and when configuring the upstream checkout directly. Other layouts can simply
name their dependency in `add_subdirectory()`. The helper supplies the sibling
aap-core path and stages a launcher icon when
`ICON` is supplied. Keep Gradle's `src/main/cpp/CMakeLists.txt` reference and its
source-directory symlink: Android Studio's CMake path-resolution limits require
this layout.

Run preparation before JUCE and upstream CMake files are evaluated, and run
the target helper after `juce_add_plugin()` creates the target. Host projects
use `MODE HOST` in `aap_juce_configure()` and `aap_juce_host(TARGET ...)`.
Paths passed to preparation are relative to the calling CMake source directory;
absolute paths are recommended. There is no required `PORT_ROOT` argument.

Application patches contain application adaptations; JUCE patches are selected
centrally. An application patch must not also patch files inside the separately
prepared JUCE tree. App and JUCE preparation have separate journals.

`add_subdirectory()` exports `AAP_JUCE_DIR` immediately.
`aap_juce_configure()` exports `AAP_JUCE_SOURCE_DIR`, `AAP_JUCE_VERSION`, and
`AAP_DIR` into the caller's scope.
The target helpers replace the repeated AAP target blocks. They register and
link the AAP module, provide its include paths, and select compatibility
flags from the loaded JUCE's own header. This covers AudioPlayHead,
AudioProcessorListener change details, plugin IDs, hosted parameters, and the
editor-creation method renamed in JUCE 8.0.13 (after the headless module split
in 8.0.11). Those flags belong to
this library rather than each port. The modules declare their JUCE dependencies
and C++17 requirement in their existing module headers.

On Android both helpers link the Prefab AAP library and the log, Android, GLESv2,
and EGL libraries, and provide the JUCE activity, push-notification, MIDI,
and stabilized-callback definitions. Desktop targets receive the module and
compatibility definitions without Android libraries or activity definitions.
All definitions and include paths are attached to targets, so unrelated targets
and directory-wide optimization settings are left alone.

Use the existing JUCE target name; the helpers do not create or rename targets.
Several targets can share one call, for example:

```cmake
aap_juce_plugin(TARGETS MyInstrument MyEffect)
aap_juce_host(TARGETS magda_daw magda_daw_app)
```

The helper can also run after `juce_generate_juce_header()`: JUCE evaluates its
module definitions when generating the header. Separate early
`juce_add_modules()` calls are unnecessary. Keep app-specific source files and
options (such as VMPC's custom factory, RtMidi JNI shim, or disabling Oboe) in
the app patch. The helper does not change plugin formats or compiler optimization.

## JUCE patch catalog

The version is detected from JUCE's `juce_StandardHeader.h`, without configuring
or compiling JUCE. Optional `JUCE_VERSION` verifies an expected version.

Predefined sets currently cover:

| JUCE | Embedded Java option |
| --- | --- |
| 6.0.5 | Unavailable |
| 7.0.9, 7.0.10, 7.0.11, 7.0.12 | Available |
| 8.0.9, 8.0.10, 8.0.12 | Unavailable |
| 8.0.15, 9.0.0 | Available |

The ordered sets have been checked for patch applicability against clean JUCE
source snapshots at those releases. This does not replace building and testing
each Android port. Patch-directory names indicate where a patch originated,
not the entire range of JUCE versions it supports.

By default the catalog patches Java sources. `EMBEDDED_JAVA` additionally
patches JUCE's embedded bytecode, for ports that load those classes from native
libraries. It fails explicitly on versions without a compatible bytecode patch.
Plugin/host mode also selects the appropriate initialization and entrypoint
patches. JUCE 8/9 plugin sets use the weak factory fallback alongside the
application-entrypoint visibility patch, avoiding duplicate factory definitions.
All predefined plugin and host sets align JUCE's current-thread-holder spinlock
storage, preventing unaligned atomic accesses during native thread startup.

Exceptions are available through:

- `EXTRA_JUCE_PATCHES`: additional patches applied after the predefined set.
- `DISABLE_JUCE_FIXES`: named fixes to omit. The catalog's IDs are `jni`,
  `detach`, `cgwindow`, `opengl`, `peer`, `desktop`, `activity`, `plugin-ui`, `thread`, `thread-lock`, `peer-touch`,
  `peer-lifecycle`, `popup`, `popup-touch`, `lightweight`, `saf`, `bytecode`, `factory`, and
  `entrypoint`. Only IDs active for the selected version/mode are accepted.
- `JUCE_PATCHES`: a complete explicit replacement, including entrypoint patches.
  Use `JUCE_PATCHES NONE` to disable JUCE patching. This override cannot be
  combined with catalog options. Unknown JUCE versions require this override
  until a compatible predefined set is added.

`aap_juce_get_juce_patches(patches VERSION 8.0.12 MODE PLUGIN)` exposes the
resolved ordered list independently for tools that need to inspect it.

## Shared preparation and existing paths

Sources are patched in place so existing Gradle Java source sets and native
paths continue to point at the same files. Each actual source directory holds
a `.aap-juce/` lock and journal; aliases to the same directory resolve to the
same lock. ABI/variant binary directories never own preparation state.
Exclude `.aap-juce/` from version control in the upstream checkouts.

The fingerprint includes the source revision, ordered patch contents,
and preparation implementation. Prepared file contents are verified too.
Matching calls return without reapplying patches. Changed patch sets are staged
against saved originals; all patches must succeed before real files are updated.
Removing a previously selected patch restores its original files. Unrelated
source files are not copied or replaced.

Edits to prepared files are preserved by stopping with a diagnostic rather than
overwriting them. Clean tracked files from a new upstream Git revision become
the new baseline. Restoring the previous original file also permits preparation
to run again. Source preparation persists across native `clean` builds; retain
the journal while the working tree contains its patches.

On the first migration, use unpatched source checkouts or opt into
`ADOPT_EXISTING_PATCHES`. This option reconstructs originals in scratch space by
reversing patches that Git proves are already applied, then validates the entire
ordered forward series before publishing it. Unrelated local edits are retained;
incompatible or partly applied patches fail without overwriting sources. Existing
legacy stamps alone are never treated as proof that a patch was applied.
After successful preparation it creates the existing `.stamp-app` and
`.stamp-juce` markers, allowing legacy Make targets to skip applying the patches
again. Existing checked-out JUCE directories are retained.

## Fetched JUCE and Android build scheduling

Fetched ports pass their JUCE Git recipe to the same configuration call before
`project()`. For example, after importing aap-juce:

```cmake
aap_juce_configure(JUCE_SOURCE_DIR modules/juce SOURCE_CACHE "${CPM_SOURCE_CACHE}"
    JUCE_CPM_ARGS NAME JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE
                  GIT_TAG 7.0.12 CUSTOM_CACHE_KEY aap-7.0.12-lifecycle)
project(MyPlugin)
add_subdirectory(modules/juce "${CMAKE_CURRENT_BINARY_DIR}/JUCE")
```

The version in `GIT_TAG` selects the catalog, then the populated header verifies
it. A commit-only tag needs `JUCE_VERSION`. `SOURCE_CACHE` is optional; its
default is the application's `.aap-juce/dependencies`. Gradle forwards an existing
`-DCPM_SOURCE_CACHE=...` argument to host preparation so both phases use the same
cache. Android toolchain arguments are kept out of the host configure.

Preparation uses CPM's `DOWNLOAD_ONLY` mode, without discovering compilers or
configuring JUCE. Cache entries include the Git recipe and ordered patch contents;
identical profiles share a checkout, while changed profiles get separate checkouts.
A cache lock covers the first clone, patching, and publication of the stable
source link. An interrupted partial clone in that managed cache can be retried.
The source link is published after patch validation succeeds. Android native
configurations verify the prepared checkout and link without fetching or patching.
Java and native builds therefore consume the same completed source tree.

Remove the upstream JUCE `PATCH_COMMAND` when adopting this interface. Other
native dependencies can still use CPM or FetchContent after `project()`.
Additional dependencies containing Java sources can be prepared with
`aap_juce_fetch_dependency(SOURCE_DIR ... SOURCE_CACHE ... NAME ...
GIT_REPOSITORY ... GIT_TAG ...)` before `aap_juce_configure()` returns from the
host entry point. Existing in-tree checkouts of these unpatched dependencies are
preserved. This does not prepare unrelated native dependencies.

The shared `cmake/PreparePort.cmake` first applies the application's existing
`aap-juce-support.patch`, then evaluates the patched entry point in a normal host CMake project
with `LANGUAGES NONE` and `AAP_JUCE_PREPARE_ONLY=TRUE`. This bootstraps an unpatched checkout without trying to patch a build
script while CMake is already configuring it. Pass `APP_CMAKE` and `APP_PATCH`
when invoking it directly.

The separate `PORT_CONFIG` interface remains available for existing callers,
but these migrated ports use their patched CMake entry points directly.

Apply `gradle/aap-juce.gradle` at the end of the Android module's build script.
It registers one `prepareAapJuce` task and wires it before native configuration,
native builds, Java/Kotlin compilation, resources/assets, and lint. The task uses
the catalog-selected Android SDK CMake installation, falling back to CMake on
PATH. `-PaapJuceCmake=/path/to/cmake` overrides the executable, and
`-PaapJucePortConfig=path/to/config.cmake` selects another configuration file.
The adapter finds its preparation script relative to its own applied file;
it does not pass an aap-juce source path to CMake. The task always
checks the shared journal, so source resets and changed patches are detected
even when Gradle would otherwise consider native outputs up to date. Native
clean tasks do not depend on preparation.

This interface does not publish aap-core to Maven, generate Gradle projects,
bundle application assets, or collect APK/AAB artifacts. Existing Make tasks
remain available for those operations during migration. Resolve aap-core's
Android/Prefab dependencies before entering native CMake.

## Checks

Run `python3 -m unittest discover -s tests -v` to check concurrent preparation,
patch updates/removal, failure handling, preservation of local edits, upstream
revision changes, and the CMake target helpers. These checks use temporary source
trees and do not modify any checked-out port. To also run the CPM population
checks, set `CPM_TEST_FILE` to a local CPM 0.40.2 implementation and run the same
command. Those checks cover simultaneous cold-cache builds, offline warm reuse,
profile changes, native verification, partial-clone recovery, and additional Java
sources.
