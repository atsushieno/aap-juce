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

Use a checked-in Android CMake wrapper rather than a symbolic link to the
upstream application. For example, in `app/src/main/cpp/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.22)
project(DexedAAP LANGUAGES C CXX)

get_filename_component(port_dir "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(AAP_JUCE_DIR "${port_dir}/external/aap-juce")

add_subdirectory("${AAP_JUCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/aap-juce"
                 EXCLUDE_FROM_ALL)

aap_juce_configure(
    APP_SOURCE_DIR  "${port_dir}/external/dexed"
    APP_PATCHES     "${port_dir}/aap-juce-support.patch"
    JUCE_SOURCE_DIR "${port_dir}/external/dexed/libs/JUCE"
    AAP_DIR         "${port_dir}/external/aap-core"
)

# Set upstream-specific options/path overrides here, then load the application.
# Upstreams that assume CMAKE_SOURCE_DIR is their own root need an adaptation
# in their application patch when used through a wrapper.
add_subdirectory("${port_dir}/external/dexed" upstream)

# This replaces repeated AAP module/linkage setup when that setup is removed
# from the application's patch. Already-registered AAP modules are reused.
aap_juce_plugin(TARGET Dexed)
```

Run preparation before JUCE and upstream CMake files are evaluated, and run
the target helper after `juce_add_plugin()` creates the target. Host projects
use `MODE HOST` in `aap_juce_configure()` and `aap_juce_host(TARGET ...)`.
Paths passed to preparation are relative to the calling CMake source directory;
absolute paths are recommended. There is no required `PORT_ROOT` argument.

Application patches contain application adaptations; JUCE patches are selected
centrally. An application patch must not also patch files inside the separately
prepared JUCE tree. App and JUCE preparation have separate journals.

`aap_juce_configure()` exports `AAP_JUCE_DIR`, `AAP_JUCE_SOURCE_DIR`,
`AAP_JUCE_VERSION`, and, when supplied, `AAP_DIR` into the caller's scope.
The plugin target helper supplies the appropriate old/new AudioPlayHead API
definition, AAP module include paths, and Android linkage/initialization
definitions. Project-specific flags such as `AAP_JUCE_HAS_HEADLESS_PROCESSOR`
remain the application's responsibility.

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

Exceptions are available through:

- `EXTRA_JUCE_PATCHES`: additional patches applied after the predefined set.
- `DISABLE_JUCE_FIXES`: named fixes to omit. The catalog's IDs are `jni`,
  `detach`, `cgwindow`, `opengl`, `peer`, `plugin-ui`, `thread`, `peer-touch`,
  `peer-lifecycle`, `popup`, `lightweight`, `saf`, `bytecode`, `factory`, and
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

The fingerprint includes the source revision, ordered patch paths/contents,
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

On the first migration, use unpatched source checkouts. Existing legacy stamps
cannot establish which patches were applied or recover their original files,
so the new interface does not blindly adopt a tree previously patched by Make.
After successful preparation it creates the existing `.stamp-app` and
`.stamp-juce` markers, allowing legacy Make targets to skip applying the patches
again. Existing files are not moved, renamed, or replaced with new symlinks.

## Fetched JUCE and Android build scheduling

For FetchContent/CPM ports, populate JUCE without adding its CMake project,
then call `aap_juce_configure(JUCE_SOURCE_DIR "${JUCE_SOURCE_DIR}" ...)` before
adding JUCE. Remove the old `PATCH_COMMAND` when adopting this preparation
layer; both mechanisms must not patch the same tree. A shared populated source
can be reused across ABI builds, with the source-local lock protecting patching.
Fetching itself must also be serialized by its owner before publishing a
shared source tree; this helper does not lock a concurrent download or checkout.

The helpers can also be loaded with `include(cmake/AAPJuce.cmake)` from a host
`cmake -P` preparation script. Android builds still need Gradle dependencies
connecting preparation to Java, resources, and lint where these consume its
outputs. A native build target runs too late for configuration-time patching.

This interface does not publish aap-core to Maven, generate Gradle projects,
copy application assets, or collect APK/AAB artifacts. Existing Make tasks
remain available for those operations during migration. Resolve aap-core's
Android/Prefab dependencies before entering native CMake.

## Checks

Run `python3 -m unittest discover -s tests -v` to check concurrent preparation,
patch updates/removal, failure handling, preservation of local edits, upstream
revision changes, and the CMake target helpers. These checks use temporary source
trees and do not modify any checked-out port.
