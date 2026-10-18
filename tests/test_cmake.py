"""Portable preparation checks; run with python3 -m unittest discover -s tests."""

import concurrent.futures
import pathlib
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class PreparationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="aap juce ")
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.source = self.root / "upstream source"
        self.source.mkdir()
        (self.source / "value.txt").write_text("original\n")
        self.git("init", "-q")
        self.git("add", ".")
        self.git("-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                 "commit", "-qm", "fixture")

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.source), *args],
                              capture_output=True, text=True, check=True)

    def patch(self, name="change.patch", before="original", after="patched"):
        patch = self.root / name
        patch.write_text(
            "diff --git a/value.txt b/value.txt\n"
            "--- a/value.txt\n+++ b/value.txt\n@@ -1 +1 @@\n"
            f"-{before}\n+{after}\n")
        return patch

    def script(self, body):
        script = self.root / "prepare.cmake"
        script.write_text(
            'cmake_minimum_required(VERSION 3.22)\n'
            f'include("{ROOT}/cmake/AAPJuce.cmake")\n' + body)
        return script

    def run_script(self, script):
        return subprocess.run(["cmake", "-P", str(script)],
                              capture_output=True, text=True)

    def prepare(self, *patches):
        args = " ".join(f'"{patch}"' for patch in patches)
        if patches:
            args = "PATCHES " + args
        return self.run_script(self.script(
            f'_aap_juce_prepare_tree("{self.source}" {args})\n'))

    def assert_success(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_independent_builds_share_one_preparation(self):
        patch = self.patch()
        script = self.script(f'_aap_juce_prepare_tree("{self.source}" PATCHES "{patch}")\n')
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            results = list(pool.map(lambda _: self.run_script(script), range(8)))
        for result in results:
            self.assert_success(result)
        self.assertEqual(sum("Preparing AAP sources:" in r.stdout for r in results), 1)
        self.assertEqual((self.source / "value.txt").read_text(), "patched\n")

    def test_patch_content_changes_reprepare_from_original(self):
        patch = self.patch()
        self.assert_success(self.prepare(patch))
        self.patch(after="updated")
        self.assert_success(self.prepare(patch))
        self.assertEqual((self.source / "value.txt").read_text(), "updated\n")

    def test_ordered_overlapping_patches_and_removal(self):
        first = self.patch()
        second = self.patch("second.patch", "patched", "twice")
        self.assert_success(self.prepare(first, second))
        self.assertEqual((self.source / "value.txt").read_text(), "twice\n")
        self.assert_success(self.prepare(first))
        self.assertEqual((self.source / "value.txt").read_text(), "patched\n")
        self.assert_success(self.prepare())
        self.assertEqual((self.source / "value.txt").read_text(), "original\n")

    def test_later_patch_failure_does_not_modify_sources(self):
        first = self.patch()
        bad = self.patch("bad.patch", "wrong baseline", "bad")
        result = self.prepare(first, bad)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Source files are unchanged", result.stderr)
        self.assertEqual((self.source / "value.txt").read_text(), "original\n")
        self.assertFalse((self.source / ".aap-juce/current/fingerprint").exists())

    def test_failed_update_preserves_previous_preparation(self):
        first = self.patch()
        self.assert_success(self.prepare(first))
        self.patch(before="wrong", after="updated")
        self.assertNotEqual(self.prepare(first).returncode, 0)
        self.assertEqual((self.source / "value.txt").read_text(), "patched\n")
        self.patch()
        result = self.prepare(first)
        self.assert_success(result)
        self.assertNotIn("Preparing AAP sources:", result.stdout)

    def test_local_edits_are_preserved(self):
        patch = self.patch()
        self.assert_success(self.prepare(patch))
        (self.source / "value.txt").write_text("developer edit\n")
        result = self.prepare(patch)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("changed outside preparation", result.stderr)
        self.assertEqual((self.source / "value.txt").read_text(), "developer edit\n")

    def test_adopts_partial_legacy_patch_set_with_local_edits(self):
        first = self.patch()
        second = self.patch("second.patch", "patched", "twice")
        context = "context 1\ncontext 2\ncontext 3\n"
        for patch in (first, second):
            patch.write_text(patch.read_text().replace('@@ -1 +1 @@', '@@ -1,4 +1,4 @@')
                             + ''.join(' ' + line + '\n' for line in context.splitlines()))
        (self.source / "value.txt").write_text("patched\n" + context + "unrelated developer edit\n")
        result = self.run_script(self.script(
            f'_aap_juce_prepare_tree("{self.source}" ADOPT_EXISTING_PATCHES '
            f'PATCHES "{first}" "{second}")\n'))
        self.assert_success(result)
        self.assertEqual((self.source / "value.txt").read_text(),
                         "twice\n" + context + "unrelated developer edit\n")
        self.assert_success(self.prepare())
        self.assertEqual((self.source / "value.txt").read_text(),
                         "original\n" + context + "unrelated developer edit\n")

    def test_adoption_rejects_incompatible_local_edit(self):
        patch = self.patch()
        (self.source / "value.txt").write_text("different developer edit\n")
        result = self.run_script(self.script(
            f'_aap_juce_prepare_tree("{self.source}" ADOPT_EXISTING_PATCHES '
            f'PATCHES "{patch}")\n'))
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((self.source / "value.txt").read_text(), "different developer edit\n")

    def test_adoption_mode_also_accepts_clean_dependent_patches(self):
        first = self.patch()
        second = self.patch("second.patch", "patched", "twice")
        result = self.run_script(self.script(
            f'_aap_juce_prepare_tree("{self.source}" ADOPT_EXISTING_PATCHES '
            f'PATCHES "{first}" "{second}")\n'))
        self.assert_success(result)
        self.assertEqual((self.source / "value.txt").read_text(), "twice\n")

    def test_restored_output_is_prepared_again(self):
        patch = self.patch()
        self.assert_success(self.prepare(patch))
        (self.source / "value.txt").write_text("original\n")
        self.assert_success(self.prepare(patch))
        self.assertEqual((self.source / "value.txt").read_text(), "patched\n")

    def test_file_creation_deletion_and_rename(self):
        patch = self.root / "files.patch"
        patch.write_text(
            "diff --git a/value.txt b/renamed.txt\n"
            "similarity index 100%\nrename from value.txt\nrename to renamed.txt\n"
            "diff --git a/added.txt b/added.txt\nnew file mode 100644\n"
            "--- /dev/null\n+++ b/added.txt\n@@ -0,0 +1 @@\n+added\n")
        self.assert_success(self.prepare(patch))
        self.assertFalse((self.source / "value.txt").exists())
        self.assertEqual((self.source / "renamed.txt").read_text(), "original\n")
        self.assertEqual((self.source / "added.txt").read_text(), "added\n")
        self.assert_success(self.prepare())
        self.assertTrue((self.source / "value.txt").exists())
        self.assertFalse((self.source / "renamed.txt").exists())
        self.assertFalse((self.source / "added.txt").exists())

    def test_source_symlink_escape_is_rejected(self):
        outside = self.root / "outside"
        outside.mkdir()
        (outside / "value.txt").write_text("original\n")
        try:
            (self.source / "link").symlink_to(outside, target_is_directory=True)
        except OSError:
            self.skipTest("Creating symlinks is unavailable on this host")
        patch = self.patch()
        patch.write_text(patch.read_text().replace("a/value.txt", "a/link/value.txt")
                         .replace("b/value.txt", "b/link/value.txt"))
        result = self.prepare(patch)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((outside / "value.txt").read_text(), "original\n")

    def test_configure_override_and_legacy_stamp(self):
        header = self.source / "modules/juce_core/system/juce_StandardHeader.h"
        header.parent.mkdir(parents=True)
        header.write_text("#define JUCE_MAJOR_VERSION 99\n"
                          "#define JUCE_MINOR_VERSION 0\n#define JUCE_BUILDNUMBER 1\n")
        patch = self.patch()
        result = self.run_script(self.script(
            f'aap_juce_configure(JUCE_SOURCE_DIR "{self.source}" '
            f'JUCE_VERSION 99.0.1 JUCE_PATCHES "{patch}")\n'))
        self.assert_success(result)
        self.assertTrue((self.source / ".stamp-juce").exists())

    def test_version_mismatch_fails_before_mutation(self):
        header = self.source / "modules/juce_core/system/juce_StandardHeader.h"
        header.parent.mkdir(parents=True)
        header.write_text("#define JUCE_MAJOR_VERSION 7\n"
                          "#define JUCE_MINOR_VERSION 0\n#define JUCE_BUILDNUMBER 11\n")
        patch = self.patch()
        result = self.run_script(self.script(
            f'aap_juce_configure(JUCE_SOURCE_DIR "{self.source}" '
            f'JUCE_VERSION 8.0.12 JUCE_PATCHES "{patch}")\n'))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Expected JUCE 8.0.12, found 7.0.11", result.stderr)
        self.assertEqual((self.source / "value.txt").read_text(), "original\n")

    def test_configure_selects_catalog_with_exceptions(self):
        header = self.source / "modules/juce_core/system/juce_StandardHeader.h"
        header.parent.mkdir(parents=True)
        header.write_text("#define JUCE_MAJOR_VERSION 7\n"
                          "#define JUCE_MINOR_VERSION 0\n#define JUCE_BUILDNUMBER 11\n")
        patch = self.patch()
        result = self.run_script(self.script(
            f'aap_juce_configure(JUCE_SOURCE_DIR "{self.source}" '
            'DISABLE_JUCE_FIXES jni detach cgwindow plugin-ui peer-touch '
            'peer-lifecycle popup popup-touch entrypoint '
            f'EXTRA_JUCE_PATCHES "{patch}")\n'))
        self.assert_success(result)
        self.assertEqual((self.source / "value.txt").read_text(), "patched\n")

    def test_clean_new_upstream_revision_is_a_new_baseline(self):
        patch = self.patch()
        self.assert_success(self.prepare(patch))
        (self.source / "value.txt").write_text("new upstream\n")
        self.git("add", "value.txt")
        self.git("-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                 "commit", "-qm", "upstream update")
        self.patch(before="new upstream", after="new patched")
        self.assert_success(self.prepare(patch))
        self.assertEqual((self.source / "value.txt").read_text(), "new patched\n")

    def test_add_subdirectory_and_target_helpers(self):
        project = self.root / "wrapper"
        project.mkdir()
        core = self.root / "core/include"
        core.mkdir(parents=True)
        (project / "check.c").write_text(
            "#if JUCEAAP_HAVE_AUDIO_PLAYHEAD_NEW_POSITION_INFO != 1\n"
            '#error "Missing playhead definition"\n#endif\nint fixture(void) { return 0; }\n')
        (project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.22)\nproject(fixture LANGUAGES C)\n'
            f'add_subdirectory("{ROOT}" aap-juce EXCLUDE_FROM_ALL)\n'
            'function(juce_add_module source)\n'
            ' get_filename_component(module "${source}" NAME)\n'
            ' add_library("${module}" INTERFACE)\nendfunction()\n'
            'set(AAP_JUCE_VERSION 8.0.12)\n'
            f'set(AAP_DIR "{core.parent}")\n'
            'add_library(plugin STATIC check.c)\naap_juce_plugin(TARGET plugin)\n'
            'add_library(host STATIC check.c)\n'
            'aap_juce_plugin(TARGET host)\naap_juce_host(TARGET host)\n')
        build = self.root / "build"
        self.assert_success(subprocess.run(
            ["cmake", "-S", str(project), "-B", str(build)], capture_output=True, text=True))
        self.assert_success(subprocess.run(
            ["cmake", "--build", str(build)], capture_output=True, text=True))

    def test_target_compatibility_uses_loaded_juce_and_stays_target_scoped(self):
        # Exact releases around each API transition, checked against JUCE's headers.
        versions = {
            "6.0.5": (0, 1, 0, 0, 0),
            "6.1.0": (0, 0, 1, 0, 0),
            "6.1.2": (0, 0, 1, 0, 0),
            "6.1.3": (0, 0, 1, 1, 0),
            "7.0.0": (1, 0, 1, 1, 0),
            "8.0.10": (1, 0, 1, 1, 0),
            "8.0.11": (1, 0, 1, 1, 0),
            "8.0.12": (1, 0, 1, 1, 0),
            "8.0.13": (1, 0, 1, 1, 1),
            "8.0.15": (1, 0, 1, 1, 1),
            "9.0.0": (1, 0, 1, 1, 1),
        }
        names = ("JUCEAAP_HAVE_AUDIO_PLAYHEAD_NEW_POSITION_INFO",
                 "JUCEAAP_AUDIO_PROCESSOR_CHANGE_DETAILS_UNAVAILABLE",
                 "JUCEAAP_USE_UNIQUE_ID", "JUCEAAP_HOSTED_PARAMETER",
                 "AAP_JUCE_HAS_HEADLESS_PROCESSOR")
        core = self.root / "core/include"
        core.mkdir(parents=True)
        (core / "aap-target-check.h").write_text("#define AAP_INCLUDE_PATH_CHECK 1\n")
        for version, expected in versions.items():
            with self.subTest(version=version):
                project = self.root / version
                project.mkdir()
                juce = project / "JUCE"
                header = juce / "modules/juce_core/system/juce_StandardHeader.h"
                header.parent.mkdir(parents=True)
                header.write_text("".join(f"#define JUCE_{key} {value}\n" for key, value in
                    zip(("MAJOR_VERSION", "MINOR_VERSION", "BUILDNUMBER"), version.split("."))))
                (project / "check.c").write_text('#include "aap-target-check.h"\n' + "".join(
                    f'#if {name} != {value}\n#error "Incorrect {name}"\n#endif\n'
                    for name, value in zip(names, expected)) +
                    '#if !JUCEAAP_ENABLED || !AAP_INCLUDE_PATH_CHECK\n#error "Missing AAP setup"\n#endif\n'
                    '#if defined(JUCE_ANDROID) || defined(JUCE_PUSH_NOTIFICATIONS)\n'
                    '#error "Android flags leaked into desktop"\n#endif\nint check(void) { return 0; }\n')
                (project / "unrelated.c").write_text(
                    '#ifdef JUCEAAP_ENABLED\n#error "Definitions leaked to unrelated target"\n#endif\n'
                    'int unrelated(void) { return 0; }\n')
                (project / "CMakeLists.txt").write_text(
                    'cmake_minimum_required(VERSION 3.22)\nproject(targets LANGUAGES C)\n'
                    f'add_subdirectory("{ROOT}" aap-juce EXCLUDE_FROM_ALL)\n'
                    'add_library(juce_core INTERFACE)\nadd_library(juce::juce_core ALIAS juce_core)\n'
                    f'set_target_properties(juce_core PROPERTIES INTERFACE_JUCE_MODULE_PATH "{juce}/modules")\n'
                    'function(juce_add_module source)\n'
                    ' get_filename_component(module "${source}" NAME)\n'
                    ' add_library("${module}" INTERFACE)\n'
                    'endfunction()\n'
                    # The real module path, not a caller's stale variable, decides the version.
                    'set(AAP_JUCE_VERSION 99.0.0)\n'
                    'function(create_plugins)\n'
                    ' add_library(plugin_a STATIC check.c)\nadd_library(plugin_b STATIC check.c)\n'
                    f' aap_juce_plugin(TARGETS plugin_a plugin_b AAP_DIR "{core.parent}")\n'
                    'endfunction()\ncreate_plugins()\n'
                    'add_library(host STATIC check.c)\n'
                    f'aap_juce_host(TARGET host AAP_DIR "{core.parent}")\n'
                    'add_library(consumer STATIC check.c)\ntarget_link_libraries(consumer PRIVATE plugin_a)\n'
                    'add_library(unrelated STATIC unrelated.c)\n'
                    'get_directory_property(directory_defs COMPILE_DEFINITIONS)\n'
                    'if(directory_defs)\n message(FATAL_ERROR "Directory definitions were modified")\nendif()\n')
                build = project / "build"
                self.assert_success(subprocess.run(
                    ["cmake", "-S", str(project), "-B", str(build)], capture_output=True, text=True))
                self.assert_success(subprocess.run(
                    ["cmake", "--build", str(build)], capture_output=True, text=True))

    def test_android_target_helpers_supply_initialization_and_system_linkage(self):
        project = self.root / "android-targets"
        project.mkdir()
        core = self.root / "core/include"
        core.mkdir(parents=True)
        (project / "check.c").write_text(
            '#if !JUCEAAP_ENABLED || !JUCE_ANDROID || '
            '!JUCE_DONT_AUTO_OPEN_MIDI_DEVICES_ON_MOBILE || !JUCE_USE_ANDROID_OBOE_STABILIZED_CALLBACK\n'
            '#error "Missing Android initialization definitions"\n#endif\n'
            '#if !AAP_JUCE_HAS_HEADLESS_PROCESSOR || !JUCEAAP_HOSTED_PARAMETER || !JUCEAAP_USE_UNIQUE_ID\n'
            '#error "Missing JUCE compatibility definitions"\n#endif\n'
            '#if EXPECT_EMBEDDED\n'
            '#if defined(JUCE_PUSH_NOTIFICATIONS) || defined(JUCE_PUSH_NOTIFICATIONS_ACTIVITY)\n'
            '#error "Embedded Java must not require JuceActivity"\n#endif\n'
            '#else\n#if !JUCE_PUSH_NOTIFICATIONS || !defined(JUCE_PUSH_NOTIFICATIONS_ACTIVITY)\n'
            '#error "Missing activity initialization definitions"\n#endif\n'
            'const char* activity = JUCE_PUSH_NOTIFICATIONS_ACTIVITY;\n#endif\n'
            'int fixture(void) { return 0; }\n')
        juce = project / "juce/modules/juce_core/system"
        juce.mkdir(parents=True)
        (juce / "juce_StandardHeader.h").write_text(
            '#define JUCE_MAJOR_VERSION 8\n#define JUCE_MINOR_VERSION 0\n#define JUCE_BUILDNUMBER 15\n')
        libraries = []
        setup = ""
        for library in ("log", "android", "GLESv2", "EGL"):
            path = project / f"lib{library}.a"
            path.touch()
            libraries.append(str(path))
            setup += f'set(aap_{library} "{path}" CACHE FILEPATH "" FORCE)\n'
        (project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.22)\nproject(android_targets LANGUAGES C)\n'
            f'add_subdirectory("{ROOT}" aap-juce EXCLUDE_FROM_ALL)\n'
            'function(juce_add_module source)\n'
            ' get_filename_component(module "${source}" NAME)\n'
            ' add_library("${module}" INTERFACE)\nendfunction()\n'
            'add_library(androidaudioplugin::androidaudioplugin INTERFACE IMPORTED)\n'
            # Configure before enabling the mocked Android toolchain, avoiding source preparation.
            'if(EXPECT_EMBEDDED)\nset(java_option EMBEDDED_JAVA)\nendif()\n'
            f'aap_juce_configure(JUCE_SOURCE_DIR juce AAP_DIR "{core.parent}" ${{java_option}})\n'
            'set(ANDROID TRUE)\nadd_compile_definitions(EXPECT_EMBEDDED=${EXPECT_EMBEDDED})\n' + setup +
            'function(add_plugins)\n'
            'add_library(plugin STATIC check.c)\nadd_library(host STATIC check.c)\n'
            f'aap_juce_plugin(TARGET plugin AAP_DIR "{core.parent}")\n'
            f'aap_juce_host(TARGET host AAP_DIR "{core.parent}")\n'
            'endfunction()\nadd_plugins()\n'
            'foreach(target plugin host)\n'
            ' get_target_property(links ${target} LINK_LIBRARIES)\n'
            ' foreach(required androidaudioplugin::androidaudioplugin ' +
            " ".join(f'"{x}"' for x in libraries) + ')\n'
            '  if(NOT required IN_LIST links)\n message(FATAL_ERROR "Missing Android library: ${required}")\n'
            '  endif()\n endforeach()\nendforeach()\n')
        for embedded in (0, 1):
            with self.subTest(embedded_java=embedded):
                build = project / f"build-{embedded}"
                self.assert_success(subprocess.run(
                    ["cmake", "-S", str(project), "-B", str(build),
                     f"-DEXPECT_EMBEDDED={embedded}"], capture_output=True, text=True))
                self.assert_success(subprocess.run(
                    ["cmake", "--build", str(build)], capture_output=True, text=True))

    def test_add_subdirectory_exports_relocated_source_directory(self):
        relocated = self.root / "dependencies/integration with arbitrary name"
        relocated.mkdir(parents=True)
        shutil.copyfile(ROOT / "CMakeLists.txt", relocated / "CMakeLists.txt")
        shutil.copytree(ROOT / "cmake", relocated / "cmake")
        project = self.root / "consumer"
        project.mkdir()
        (project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.22)\nproject(fixture LANGUAGES C)\n'
            f'add_subdirectory("{relocated}" integration EXCLUDE_FROM_ALL)\n'
            f'if(NOT AAP_JUCE_DIR STREQUAL "{relocated}")\n'
            ' message(FATAL_ERROR "add_subdirectory did not export its own directory")\n'
            'endif()\n'
            'function(juce_add_module source)\n'
            f' if(NOT source MATCHES "^{relocated}/")\n'
            '  message(FATAL_ERROR "Module path did not follow the loaded directory")\n'
            ' endif()\n'
            ' get_filename_component(module "${source}" NAME)\n'
            ' add_library("${module}" INTERFACE)\nendfunction()\n'
            'file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/core/include")\n'
            'set(AAP_DIR "${CMAKE_CURRENT_BINARY_DIR}/core")\n'
            'set(AAP_JUCE_VERSION 8.0.12)\n'
            'file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/check.c" "int fixture(void) { return 0; }\\n")\n'
            'add_library(plugin STATIC "${CMAKE_CURRENT_BINARY_DIR}/check.c")\n'
            'aap_juce_host(TARGET plugin)\n')
        self.assert_success(subprocess.run(
            ["cmake", "-S", str(project), "-B", str(self.root / "relocated-build")],
            capture_output=True, text=True))

    def test_desktop_configure_exports_paths_without_android_preparation(self):
        header = self.source / "modules/juce_core/system/juce_StandardHeader.h"
        header.parent.mkdir(parents=True)
        header.write_text("#define JUCE_MAJOR_VERSION 99\n"
                          "#define JUCE_MINOR_VERSION 0\n#define JUCE_BUILDNUMBER 1\n")
        project = self.root / "desktop"
        project.mkdir()
        (project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.22)\n'
            f'add_subdirectory("{ROOT}" aap-juce EXCLUDE_FROM_ALL)\n'
            f'aap_juce_configure(JUCE_SOURCE_DIR "{self.source}" ICON missing.png)\n'
            'project(desktop LANGUAGES NONE)\n'
            'if(NOT AAP_JUCE_VERSION STREQUAL "99.0.1" OR NOT AAP_DIR OR NOT AAP_JUCE_DIR)\n'
            ' message(FATAL_ERROR "Missing integration values")\nendif()\n')
        self.assert_success(subprocess.run(
            ["cmake", "-S", str(project), "-B", str(self.root / "desktop-build")],
            capture_output=True, text=True))
        self.assertFalse((self.source / ".aap-juce").exists())
        self.assertFalse((self.source / ".stamp-juce").exists())

    def test_android_detection_before_project(self):
        header = self.source / "modules/juce_core/system/juce_StandardHeader.h"
        header.parent.mkdir(parents=True)
        header.write_text("#define JUCE_MAJOR_VERSION 99\n"
                          "#define JUCE_MINOR_VERSION 0\n#define JUCE_BUILDNUMBER 1\n")
        patch = self.patch()
        project = self.root / "android"
        project.mkdir()
        (project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.22)\n'
            f'add_subdirectory("{ROOT}" aap-juce EXCLUDE_FROM_ALL)\n'
            'set(CMAKE_SYSTEM_NAME Android)\n'
            f'aap_juce_configure(JUCE_SOURCE_DIR "{self.source}" JUCE_PATCHES "{patch}")\n'
            'set(CMAKE_SYSTEM_NAME "${CMAKE_HOST_SYSTEM_NAME}")\n'
            'project(fixture LANGUAGES NONE)\n')
        self.assert_success(self.prepare(patch))
        self.assert_success(subprocess.run(
            ["cmake", "-S", str(project), "-B", str(self.root / "android-build")],
            capture_output=True, text=True))
        self.assertEqual((self.source / "value.txt").read_text(), "patched\n")

    def test_catalog_choices_and_unknown_versions(self):
        for version in ("6.0.5", "7.0.9", "7.0.10", "7.0.11", "7.0.12",
                        "8.0.9", "8.0.10", "8.0.12", "8.0.15", "9.0.0"):
            for mode in ("PLUGIN", "HOST"):
                result = self.run_script(self.script(
                    f'aap_juce_get_juce_patches(p VERSION {version} MODE {mode})\n'
                    'foreach(patch IN LISTS p)\n'
                    ' if(NOT EXISTS "${patch}")\n'
                    '  message(FATAL_ERROR "Missing patch: ${patch}")\n'
                    ' endif()\nendforeach()\n'))
                self.assert_success(result)
        result = self.run_script(self.script('aap_juce_get_juce_patches(p VERSION 99.0.1)\n'))
        self.assertNotEqual(result.returncode, 0)

    def test_preparation_bootstraps_the_patched_cmake_entrypoint(self):
        juce = self.root / "JUCE"
        header = juce / "modules/juce_core/system/juce_StandardHeader.h"
        header.parent.mkdir(parents=True)
        header.write_text("#define JUCE_MAJOR_VERSION 99\n"
                          "#define JUCE_MINOR_VERSION 0\n#define JUCE_BUILDNUMBER 1\n")
        (juce / "value.txt").write_text("original\n")
        juce_patch = self.patch()
        entry = self.source / "CMakeLists.txt"
        entry.write_text('project(fixture LANGUAGES NONE)\n'
                         'message(FATAL_ERROR "Preparation must stop before project")\n')
        patch = self.root / "entry.patch"
        patch.write_text(
            'diff --git a/CMakeLists.txt b/CMakeLists.txt\n'
            '--- a/CMakeLists.txt\n+++ b/CMakeLists.txt\n@@ -1,2 +1,5 @@\n'
            f'+add_subdirectory("{ROOT}" "${{CMAKE_CURRENT_BINARY_DIR}}/aap-juce")\n'
            '+file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/selected-cache.txt" "${CPM_SOURCE_CACHE}")\n'
            f'+aap_juce_configure(JUCE_SOURCE_DIR "{juce}" JUCE_PATCHES "{juce_patch}")\n'
            ' project(fixture LANGUAGES NONE)\n'
            ' message(FATAL_ERROR "Preparation must stop before project")\n')
        result = subprocess.run([
            'cmake', '-DCPM_SOURCE_CACHE=chosen-cache', f'-DAPP_CMAKE={entry}', f'-DAPP_PATCH={patch}',
            '-P', str(ROOT / 'cmake/PreparePort.cmake')], capture_output=True, text=True)
        self.assert_success(result)
        self.assertEqual((self.source / 'selected-cache.txt').read_text(), 'chosen-cache')
        self.assertEqual((juce / 'value.txt').read_text(), 'patched\n')
        self.assertTrue((self.source / '.stamp-app').exists())
        result = subprocess.run([
            'cmake', f'-DAPP_CMAKE={entry}', f'-DAPP_PATCH={patch}',
            '-P', str(ROOT / 'cmake/PreparePort.cmake')], capture_output=True, text=True)
        self.assert_success(result)
        self.assertNotIn('Preparing AAP sources:', result.stdout)
        self.assertEqual((self.source / 'selected-cache.txt').read_text(), '')
        # Switching from a development checkout to the pinned submodule must
        # not reuse a CMake cache tied to a different host project's source.
        alternate = self.root / 'alternate-helper'
        alternate.mkdir()
        for filename in ('PreparePort.cmake', 'AAPJuce.cmake', 'PrepareSources.cmake', 'JucePatchSets.cmake', 'PopulateSources.cmake'):
            shutil.copyfile(ROOT / 'cmake' / filename, alternate / filename)
        shutil.copytree(ROOT / 'cmake/prepare', alternate / 'prepare')
        result = subprocess.run([
            'cmake', f'-DAPP_CMAKE={entry}', f'-DAPP_PATCH={patch}',
            '-P', str(alternate / 'PreparePort.cmake')], capture_output=True, text=True)
        self.assert_success(result)


if __name__ == "__main__":
    unittest.main()
