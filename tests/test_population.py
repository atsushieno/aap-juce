"""CPM integration checks. Set CPM_TEST_FILE to a CPM 0.40.2 implementation."""

import concurrent.futures
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.environ.get('CPM_TEST_FILE'), 'set CPM_TEST_FILE for local CPM integration tests')
class PopulationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='aap population ')
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name).resolve()
        self.repo = self.root / 'repository'
        self.repo.mkdir()
        (self.repo / 'CMakeLists.txt').write_text(
            'message(FATAL_ERROR "Host population must not configure dependencies")\n')
        (self.repo / 'value.txt').write_text('original\n')
        self.git('init', '-q')
        self.git('add', '.')
        self.git('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                 'commit', '-qm', 'fixture')
        self.git('tag', '1.0.0')
        self.cache = self.root / 'cache'
        (self.cache / 'cpm').mkdir(parents=True)
        shutil.copyfile(os.environ['CPM_TEST_FILE'], self.cache / 'cpm/CPM_0.40.2.cmake')
        self.patch = self.root / 'change.patch'
        self.patch.write_text('diff --git a/value.txt b/value.txt\n'
                              '--- a/value.txt\n+++ b/value.txt\n@@ -1 +1 @@\n'
                              '-original\n+patched\n')

    def git(self, *args):
        subprocess.run(['git', '-C', str(self.repo), *args],
                       check=True, capture_output=True)

    def project(self, name, patch=None, tag='1.0.0'):
        project = self.root / name
        project.mkdir(exist_ok=True)
        (project / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.22)\nproject(fixture LANGUAGES NONE)\n'
            f'include("{ROOT}/cmake/AAPJuce.cmake")\n'
            f'_aap_juce_populate(source SOURCE_DIR modules/dependency SOURCE_CACHE "{self.cache}" '
            f'NAME Fixture GIT_REPOSITORY "{self.repo}" GIT_TAG {tag} '
            f'PATCHES "{patch or self.patch}")\n')
        return project

    def configure(self, project, native=False):
        args = ['cmake', '-S', str(project), '-B', str(project / ('native' if native else 'host'))]
        if native:
            args += ['-DANDROID=ON']
        return subprocess.run(args, capture_output=True, text=True)

    def assert_success(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def source(self, project):
        return (project / 'modules/dependency').resolve()

    def test_concurrent_cold_population_then_offline_java_and_native_reuse(self):
        copy = self.root / 'same-patch-another-path.patch'
        shutil.copyfile(self.patch, copy)
        projects = [self.project('first'), self.project('second', copy)]
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(self.configure, projects))
        for result in results:
            self.assert_success(result)
        self.assertEqual(sum('Preparing AAP sources:' in r.stdout for r in results), 1)
        self.assertEqual(self.source(projects[0]), self.source(projects[1]))
        self.assertEqual((self.source(projects[0]) / 'value.txt').read_text(), 'patched\n')
        # Neither the repository nor CPM's loader is available for warm reuse.
        self.repo.rename(self.root / 'offline-repository')
        (self.cache / 'cpm/CPM_0.40.2.cmake').unlink()
        for project in projects:
            self.assert_success(self.configure(project))
            self.assert_success(self.configure(project, native=True))

    def test_native_requires_preparation_and_rejects_local_patch_edits(self):
        project = self.project('app')
        result = self.configure(project, native=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Run prepareAapJuce first', result.stderr)
        self.assertFalse((project / 'modules/dependency').exists())
        self.assert_success(self.configure(project))
        value = self.source(project) / 'value.txt'
        value.write_text('developer edit\n')
        result = self.configure(project, native=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('changed outside preparation', result.stderr)
        self.assertEqual(value.read_text(), 'developer edit\n')

    def test_patch_profile_isolated_and_source_link_published_after_success(self):
        project = self.project('app')
        self.assert_success(self.configure(project))
        old = self.source(project)
        self.patch.write_text(self.patch.read_text().replace('+patched', '+updated'))
        # Android never changes a previously published link for a new profile.
        self.assertNotEqual(self.configure(project, native=True).returncode, 0)
        self.assertEqual(self.source(project), old)
        self.assert_success(self.configure(project))
        self.assertNotEqual(self.source(project), old)
        self.assertEqual((old / 'value.txt').read_text(), 'patched\n')
        self.assertEqual((self.source(project) / 'value.txt').read_text(), 'updated\n')

    def test_interrupted_population_recovers_managed_partial_clone(self):
        project = self.project('app')
        self.assert_success(self.configure(project))
        source = self.source(project)
        shutil.rmtree(source)
        source.mkdir()
        (source / 'interrupted').write_text('partial clone')
        (source / 'CMakeLists.txt').write_text('part of an interrupted checkout')
        self.assert_success(self.configure(project))
        self.assertEqual((source / 'value.txt').read_text(), 'patched\n')
        self.assertFalse((source / 'interrupted').exists())

    def test_configuration_populates_before_project_without_a_cache_override(self):
        header = self.repo / 'modules/juce_core/system/juce_StandardHeader.h'
        header.parent.mkdir(parents=True)
        header.write_text('#define JUCE_MAJOR_VERSION 99\n'
                          '#define JUCE_MINOR_VERSION 0\n#define JUCE_BUILDNUMBER 1\n')
        self.git('add', '.')
        self.git('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                 'commit', '-qm', 'version header')
        self.git('tag', '99.0.1')
        project = self.root / 'app'
        project.mkdir()
        loader = project / '.aap-juce/dependencies/cpm'
        loader.mkdir(parents=True)
        shutil.copyfile(os.environ['CPM_TEST_FILE'], loader / 'CPM_0.40.2.cmake')
        (project / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.22)\n'
            f'add_subdirectory("{ROOT}" aap-juce)\n'
            f'aap_juce_configure(JUCE_SOURCE_DIR modules/dependency SOURCE_CACHE "${{CPM_SOURCE_CACHE}}" '
            f'JUCE_PATCHES "{self.patch}" JUCE_CPM_ARGS NAME Fixture '
            f'GIT_REPOSITORY "{self.repo}" GIT_TAG 99.0.1)\n'
            'project(fixture LANGUAGES NONE)\n')
        self.assert_success(self.configure(project))
        self.assert_success(self.configure(project, native=True))
        self.assertEqual((self.source(project) / 'value.txt').read_text(), 'patched\n')

    def test_additional_java_dependency_preserves_existing_checkout(self):
        project = self.root / 'app'
        project.mkdir()
        source = project / 'editables/keyboard'
        source.parent.mkdir()
        shutil.copytree(self.repo, source)
        (source / 'developer-file').write_text('keep\n')
        (project / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.22)\nproject(fixture LANGUAGES NONE)\n'
            f'include("{ROOT}/cmake/AAPJuce.cmake")\n'
            f'aap_juce_fetch_dependency(SOURCE_DIR editables/keyboard SOURCE_CACHE "{self.cache}" '
            f'NAME Keyboard GIT_REPOSITORY "{self.repo}" GIT_TAG 1.0.0)\n')
        self.assert_success(self.configure(project))
        self.assert_success(self.configure(project, native=True))
        self.assertFalse(source.is_symlink())
        self.assertEqual((source / 'developer-file').read_text(), 'keep\n')
