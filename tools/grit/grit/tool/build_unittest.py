#!/usr/bin/env python3
# Copyright 2012 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''Unit tests for the 'grit build' tool.
'''


import codecs
import os
import re
import sys
import tempfile
import zipfile
if __name__ == '__main__':
  sys.path.append(os.path.join(os.path.dirname(__file__), '../..'))

import unittest

from grit import util
from grit.tool import build


ZIP_ENTRY_PATH_TRIMMED_RE = re.compile(r'^values-\w{2}/components_strings.xml$')


class BuildUnittest(unittest.TestCase):

  # IDs should not change based on allowlisting.
  # Android WebView currently relies on this.
  EXPECTED_ID_MAP = {
      'IDR_INCLUDE_ALLOWLISTED': 9369,
      'IDR_STRUCTURE_ALLOWLISTED': 8062,
      'IDR_STRUCTURE_IN_TRUE_IF_ALLOWLISTED': 8064,
      'IDS_MESSAGE_ALLOWLISTED': 20376,
  }

  def testFindTranslationsWithSubstitutions(self):
    # This is a regression test; we had a bug where GRIT would fail to find
    # messages with substitutions e.g. "Hello [IDS_USER]" where IDS_USER is
    # another <message>.
    output_dir = util.TempDir({})
    builder = build.RcBuilder()
    class DummyOpts:
      def __init__(self):
        self.input = util.PathFromRoot('grit/testdata/substitute.grd')
        self.verbose = False
        self.extra_verbose = False
    builder.Run(DummyOpts(), ['-o', output_dir.GetPath()])
    output_dir.CleanUp()

  def testGenerateDepFileWithoutGenderSupport(self):
    self._testGenerateDepFileInternal(False)

  def testGenerateDepFileWithGenderSupport(self):
    self._testGenerateDepFileInternal(True)

  def _testGenerateDepFileInternal(self, translate_genders):
    output_dir = util.TempDir({})
    builder = build.RcBuilder()
    class DummyOpts:
      def __init__(self):
        self.input = util.PathFromRoot('grit/testdata/depfile.grd')
        self.verbose = False
        self.extra_verbose = False
    expected_dep_file = output_dir.GetPath('substitute.grd.d')

    args = [
        '-o',
        output_dir.GetPath(), '--depdir',
        output_dir.GetPath(), '--depfile', expected_dep_file
    ]
    if translate_genders:
      args.append('--translate-genders')

    builder.Run(DummyOpts(), args)

    self.assertTrue(os.path.isfile(expected_dep_file))
    with open(expected_dep_file) as f:
      line = f.readline()
      (dep_output_file, deps_string) = line.split(': ')
      deps = deps_string.split(' ')

      self.assertEqual("default_100_percent.pak", dep_output_file)
      self.assertEqual(deps, [
          util.PathFromRoot('grit/testdata/default_100_percent/a.png'),
          util.PathFromRoot('grit/testdata/grit_part.grdp'),
          util.PathFromRoot('grit/testdata/special_100_percent/a.png'),
      ])
    output_dir.CleanUp()

  def testGenerateDepFileWithResourceIds(self):
    output_dir = util.TempDir({})
    builder = build.RcBuilder()
    class DummyOpts:
      def __init__(self):
        self.input = util.PathFromRoot('grit/testdata/substitute_no_ids.grd')
        self.verbose = False
        self.extra_verbose = False
    expected_dep_file = output_dir.GetPath('substitute_no_ids.grd.d')
    builder.Run(DummyOpts(),
        ['-f', util.PathFromRoot('grit/testdata/resource_ids'),
         '-o', output_dir.GetPath(),
         '--depdir', output_dir.GetPath(),
         '--depfile', expected_dep_file])

    self.assertTrue(os.path.isfile(expected_dep_file))
    with open(expected_dep_file) as f:
      line = f.readline()
      (dep_output_file, deps_string) = line.split(': ')
      deps = deps_string.split(' ')

      self.assertEqual("resource.h", dep_output_file)
      self.assertEqual(2, len(deps))
      self.assertEqual(deps[0],
          util.PathFromRoot('grit/testdata/substitute.xmb'))
      self.assertEqual(deps[1],
          util.PathFromRoot('grit/testdata/resource_ids'))
    output_dir.CleanUp()

  def testAssertOutputs(self):
    output_dir = util.TempDir({})
    class DummyOpts:
      def __init__(self):
        self.input = util.PathFromRoot('grit/testdata/substitute.grd')
        self.verbose = False
        self.extra_verbose = False

    # Incomplete output file list should fail.
    builder_fail = build.RcBuilder()
    self.assertEqual(2,
        builder_fail.Run(DummyOpts(), [
            '-o', output_dir.GetPath(),
            '-a', os.path.abspath(
                output_dir.GetPath('en_generated_resources.rc'))]))

    # Complete output file list should succeed.
    builder_ok = build.RcBuilder()
    self.assertEqual(0,
        builder_ok.Run(DummyOpts(), [
            '-o', output_dir.GetPath(),
            '-a', os.path.abspath(
                output_dir.GetPath('en_generated_resources.rc')),
            '-a', os.path.abspath(
                output_dir.GetPath('sv_generated_resources.rc')),
            '-a', os.path.abspath(output_dir.GetPath('resource.h'))]))
    output_dir.CleanUp()

  def testAssertTemplateOutputs(self):
    output_dir = util.TempDir({})
    class DummyOpts:
      def __init__(self):
        self.input = util.PathFromRoot('grit/testdata/substitute_tmpl.grd')
        self.verbose = False
        self.extra_verbose = False

    # Incomplete output file list should fail.
    builder_fail = build.RcBuilder()
    self.assertEqual(2,
        builder_fail.Run(DummyOpts(), [
            '-o', output_dir.GetPath(),
            '-E', 'name=foo',
            '-a', os.path.abspath(output_dir.GetPath('en_foo_resources.rc'))]))

    # Complete output file list should succeed.
    builder_ok = build.RcBuilder()
    self.assertEqual(0,
        builder_ok.Run(DummyOpts(), [
            '-o', output_dir.GetPath(),
            '-E', 'name=foo',
            '-a', os.path.abspath(output_dir.GetPath('en_foo_resources.rc')),
            '-a', os.path.abspath(output_dir.GetPath('sv_foo_resources.rc')),
            '-a', os.path.abspath(output_dir.GetPath('resource.h'))]))
    output_dir.CleanUp()

  def testAssertZippedAndroidOutputs(self):
    output_dir = util.TempDir({})

    class DummyOpts:

      def __init__(self):
        self.input = util.PathFromRoot('grit/testdata/substitute_android.grd')
        self.verbose = False
        self.extra_verbose = False

    # Incomplete output file list (without zipping XMLs) should fail.
    builder_fail = build.RcBuilder()
    self.assertEqual(
        2,
        builder_fail.Run(DummyOpts(), [
            '-o',
            output_dir.GetPath(),
            '-a',
            os.path.abspath(output_dir.GetPath('en_generated_resources.rc')),
        ]))

    # Complete output file list  (without zipping XMLs) should succeed.
    builder_ok = build.RcBuilder()
    self.assertEqual(
        0,
        builder_ok.Run(DummyOpts(), [
            '-o',
            output_dir.GetPath(),
            '-a',
            os.path.abspath(output_dir.GetPath('en_generated_resources.rc')),
            '-a',
            os.path.abspath(output_dir.GetPath('sv_generated_resources.rc')),
            '-a',
            os.path.abspath(output_dir.GetPath('resource.h')),
            '-a',
            os.path.abspath(
                output_dir.GetPath(
                    'java/res/values-af/components_strings.xml')),
            '-a',
            os.path.abspath(
                output_dir.GetPath(
                    'java/res/values-am/components_strings.xml')),
            '-a',
            os.path.abspath(
                output_dir.GetPath('values-ar/components_strings.xml')),
        ]))

    # Incomplete output file list (while zipping XMLs) should fail.
    builder_fail = build.RcBuilder()
    self.assertEqual(
        2,
        builder_fail.Run(DummyOpts(), [
            '-o',
            output_dir.GetPath(),
            '-a',
            os.path.abspath(output_dir.GetPath('en_generated_resources.rc')),
            '--android-output-zip',
            os.path.abspath(output_dir.GetPath('android_resources.zip')),
        ]))

    # Complete output file list (while zipping XMLs) should succeed.
    builder_ok = build.RcBuilder()
    self.assertEqual(
        0,
        builder_ok.Run(DummyOpts(), [
            '-o',
            output_dir.GetPath(),
            '-a',
            os.path.abspath(output_dir.GetPath('en_generated_resources.rc')),
            '-a',
            os.path.abspath(output_dir.GetPath('sv_generated_resources.rc')),
            '-a',
            os.path.abspath(output_dir.GetPath('resource.h')),
            '-a',
            os.path.abspath(output_dir.GetPath('android_resources.zip')),
            '--android-output-zip',
            os.path.abspath(output_dir.GetPath('android_resources.zip')),
        ]))

    # Complete output file list (while zipping XMLs) should succeed, even when
    # --android-output-zip is a relative path.
    builder_ok = build.RcBuilder()
    self.assertEqual(
        0,
        builder_ok.Run(DummyOpts(), [
            '-o',
            output_dir.GetPath(),
            '-a',
            os.path.abspath(output_dir.GetPath('en_generated_resources.rc')),
            '-a',
            os.path.abspath(output_dir.GetPath('sv_generated_resources.rc')),
            '-a',
            os.path.abspath(output_dir.GetPath('resource.h')),
            '-a',
            os.path.abspath(output_dir.GetPath('android_resources.zip')),
            '--android-output-zip',
            os.path.relpath(output_dir.GetPath('android_resources.zip')),
        ]))

    output_dir.CleanUp()

  def testZippedAndroidOutputs(self):

    class DummyOpts:

      def __init__(self):
        self.input = util.PathFromRoot('grit/testdata/substitute_android.grd')
        self.verbose = False
        self.extra_verbose = False

    # Don't supply a '--android-output-zip' path: each of the 3 xml outputs gets
    # its own file.
    output_dir = util.TempDir({})

    builder_ok = build.RcBuilder()
    builder_ok.Run(DummyOpts(), [
        '-o',
        output_dir.GetPath(),
    ])

    self.assertTrue(
        os.path.exists(
            os.path.abspath(
                output_dir.GetPath(
                    'java/res/values-af/components_strings.xml'))))
    self.assertTrue(
        os.path.exists(
            os.path.abspath(
                output_dir.GetPath(
                    'java/res/values-am/components_strings.xml'))))
    self.assertTrue(
        os.path.exists(
            os.path.abspath(
                output_dir.GetPath('values-ar/components_strings.xml'))))

    output_dir.CleanUp()

    def getZipPath():
      return os.path.abspath(output_dir.GetPath('android_resources.zip'))

    # Supply a '--android-output-zip' xml files don't exist, but zip file does.
    # Zip file has trimmed paths.
    output_dir = util.TempDir({})
    zip_path = getZipPath()

    builder_ok = build.RcBuilder()
    builder_ok.Run(
        DummyOpts(),
        ['-o', output_dir.GetPath(), '--android-output-zip', zip_path])

    self.assertTrue(os.path.exists(zip_path))
    self.assertFalse(
        os.path.exists(
            os.path.abspath(
                output_dir.GetPath(
                    'java/res/values-af/components_strings.xml'))))
    self.assertFalse(
        os.path.exists(
            os.path.abspath(
                output_dir.GetPath(
                    'java/res/values-am/components_strings.xml'))))
    self.assertFalse(
        os.path.exists(
            os.path.abspath(
                output_dir.GetPath('values-ar/components_strings.xml'))))

    with zipfile.ZipFile(zip_path, 'r') as zip_file:
      for info in zip_file.infolist():
        self.assertIsNotNone(ZIP_ENTRY_PATH_TRIMMED_RE.match(info.filename))
        self.assertEqual(info.date_time, (2001, 1, 1, 0, 0, 0))
        self.assertGreater(info.file_size, 0)

    output_dir.CleanUp()

  def testGetTempAndroidOutputPath(self):
    builder_ok = build.RcBuilder()
    builder_ok.android_output_tmp_dir = tempfile.TemporaryDirectory(
        ignore_cleanup_errors=True)

    # easy case: an absolute path will just get the root removed and be joined
    # to the tmp dir.
    self.assertEqual(
        builder_ok.GetTempAndroidOutputPath('/absolute/path.zip'),
        os.path.join(builder_ok.android_output_tmp_dir.name,
                     os.path.join('absolute', 'path.zip')))

    # relative paths are more complicated to test, because they depend on the
    # cwd. os.path.splitdrive() removes the drive from the path in a different
    # way from the GetTempAndroidOutputPath() implementation, so it should
    # verify the behaviour we want.
    #
    # Note: it would be better to use os.path.splitroot() here, but our vpython3
    # environment is currently too old for that function (it was added in 3.12).
    #
    # For example, assuming the cwd is 'C:/chromium/src/out/Debug',
    # |expected_abspath| should evaluate to
    # (f'{builder_ok.android_output_tmp_dir}/chromium/src/out/Debug/relative/'
    #   'path.zip').
    original_abspath = os.path.abspath(
        'relative/path.zip'
    )  # eg, 'C:/chromium/src/out/Debug/relative/path.zip'
    (_, tail) = os.path.splitdrive(
        original_abspath)  # eg, '/chromium/src/out/Debug/relative/path.zip'
    expected_abspath = os.path.join(builder_ok.android_output_tmp_dir.name,
                                    tail[1:])
    self.assertEqual(builder_ok.GetTempAndroidOutputPath('relative/path.zip'),
                     expected_abspath)

    # similar, but ensures that '..' elements work in relative paths.
    #
    # again assuming the cwd is '/chromium/src/out/Debug',
    # |expected_abspath| should evaluate to
    # f'{builder_ok.android_output_tmp_dir}/chromium/src/relative/path.zip'.
    #
    # eg, '/chromium/src/relative/path.zip' (derived from
    # 'chromium/src/out/Debug/../../relative/path.zip')
    original_abspath = os.path.abspath('../../relative/path.zip')
    (_, tail) = os.path.splitdrive(
        original_abspath)  # eg, '/chromium/src/relative/path.zip'
    expected_abspath = os.path.join(builder_ok.android_output_tmp_dir.name,
                                    tail[1:])
    self.assertEqual(
        builder_ok.GetTempAndroidOutputPath('../../relative/path.zip'),
        expected_abspath)

  def _verifyAllowlistedOutput(self,
                               filename,
                               allowlisted_ids,
                               non_allowlisted_ids,
                               encoding='utf8'):
    self.assertTrue(os.path.exists(filename))
    allowlisted_ids_found = []
    non_allowlisted_ids_found = []
    with codecs.open(filename, encoding=encoding) as f:
      for line in f.readlines():
        for allowlisted_id in allowlisted_ids:
          if allowlisted_id in line:
            allowlisted_ids_found.append(allowlisted_id)
            if filename.endswith('.h'):
              numeric_id = int(line.split()[2])
              expected_numeric_id = self.EXPECTED_ID_MAP.get(allowlisted_id)
              self.assertEqual(
                  expected_numeric_id, numeric_id,
                  'Numeric ID for {} was {} should be {}'.format(
                      allowlisted_id, numeric_id, expected_numeric_id))
        for non_allowlisted_id in non_allowlisted_ids:
          if non_allowlisted_id in line:
            non_allowlisted_ids_found.append(non_allowlisted_id)
    self.longMessage = True
    self.assertEqual(allowlisted_ids, allowlisted_ids_found,
                     '\nin file {}'.format(os.path.basename(filename)))
    non_allowlisted_msg = ('Non-Allowlisted IDs {} found in {}'.format(
        non_allowlisted_ids_found, os.path.basename(filename)))
    self.assertFalse(non_allowlisted_ids_found, non_allowlisted_msg)

  def testAllowlistStrings(self):
    output_dir = util.TempDir({})
    builder = build.RcBuilder()
    class DummyOpts:
      def __init__(self):
        self.input = util.PathFromRoot('grit/testdata/allowlist_strings.grd')
        self.verbose = False
        self.extra_verbose = False

    allowlist_file = util.PathFromRoot('grit/testdata/allowlist.txt')
    builder.Run(DummyOpts(), ['-o', output_dir.GetPath(), '-w', allowlist_file])
    header = output_dir.GetPath('allowlist_test_resources.h')
    rc = output_dir.GetPath('en_allowlist_test_strings.rc')

    allowlisted_ids = ['IDS_MESSAGE_ALLOWLISTED']
    non_allowlisted_ids = ['IDS_MESSAGE_NOT_ALLOWLISTED']
    self._verifyAllowlistedOutput(
        header,
        allowlisted_ids,
        non_allowlisted_ids,
    )
    self._verifyAllowlistedOutput(rc,
                                  allowlisted_ids,
                                  non_allowlisted_ids,
                                  encoding='utf16')
    output_dir.CleanUp()

  def testAllowlistResourcesWithoutGenderSupport(self):
    self._testAllowlistResourcesInternal(False)

  def testAllowlistResourcesWithGenderSupport(self):
    self._testAllowlistResourcesInternal(True)

  def _testAllowlistResourcesInternal(self, translate_genders):
    output_dir = util.TempDir({})
    builder = build.RcBuilder()
    class DummyOpts:
      def __init__(self):
        self.input = util.PathFromRoot('grit/testdata/allowlist_resources.grd')
        self.verbose = False
        self.extra_verbose = False

    allowlist_file = util.PathFromRoot('grit/testdata/allowlist.txt')

    args = ['-o', output_dir.GetPath(), '-w', allowlist_file]
    if translate_genders:
      args.append('--translate-genders')
    builder.Run(DummyOpts(), args)

    header = output_dir.GetPath('allowlist_test_resources.h')
    map_cc = output_dir.GetPath('allowlist_test_resources_map.cc')
    map_h = output_dir.GetPath('allowlist_test_resources_map.h')
    pak = output_dir.GetPath('allowlist_test_resources.pak')

    # Ensure the resource map header and .pak files exist, but don't verify
    # their content.
    self.assertTrue(os.path.exists(map_h))
    self.assertTrue(os.path.exists(pak))

    allowlisted_ids = [
        'IDR_STRUCTURE_ALLOWLISTED',
        'IDR_STRUCTURE_IN_TRUE_IF_ALLOWLISTED',
        'IDR_INCLUDE_ALLOWLISTED',
    ]
    non_allowlisted_ids = [
        'IDR_STRUCTURE_NOT_ALLOWLISTED',
        'IDR_STRUCTURE_IN_TRUE_IF_NOT_ALLOWLISTED',
        'IDR_STRUCTURE_IN_FALSE_IF_ALLOWLISTED',
        'IDR_STRUCTURE_IN_FALSE_IF_NOT_ALLOWLISTED',
        'IDR_INCLUDE_NOT_ALLOWLISTED',
    ]
    for output_file in (header, map_cc):
      self._verifyAllowlistedOutput(
          output_file,
          allowlisted_ids,
          non_allowlisted_ids,
      )
    output_dir.CleanUp()

  def testWriteOnlyNew(self):
    output_dir = util.TempDir({})
    builder = build.RcBuilder()
    class DummyOpts:
      def __init__(self):
        self.input = util.PathFromRoot('grit/testdata/substitute.grd')
        self.verbose = False
        self.extra_verbose = False
    UNCHANGED = 10
    header = output_dir.GetPath('resource.h')

    builder.Run(DummyOpts(), ['-o', output_dir.GetPath()])
    self.assertTrue(os.path.exists(header))
    first_mtime = os.stat(header).st_mtime

    os.utime(header, (UNCHANGED, UNCHANGED))
    builder.Run(DummyOpts(),
                ['-o', output_dir.GetPath(), '--write-only-new', '0'])
    self.assertTrue(os.path.exists(header))
    second_mtime = os.stat(header).st_mtime

    os.utime(header, (UNCHANGED, UNCHANGED))
    builder.Run(DummyOpts(),
                ['-o', output_dir.GetPath(), '--write-only-new', '1'])
    self.assertTrue(os.path.exists(header))
    third_mtime = os.stat(header).st_mtime

    self.assertTrue(abs(second_mtime - UNCHANGED) > 5)
    self.assertTrue(abs(third_mtime - UNCHANGED) < 5)
    output_dir.CleanUp()

  def testGenerateDepFileWithDependOnStamp(self):
    output_dir = util.TempDir({})
    builder = build.RcBuilder()
    class DummyOpts:
      def __init__(self):
        self.input = util.PathFromRoot('grit/testdata/substitute.grd')
        self.verbose = False
        self.extra_verbose = False
    expected_dep_file_name = 'substitute.grd.d'
    expected_stamp_file_name = expected_dep_file_name + '.stamp'
    expected_dep_file = output_dir.GetPath(expected_dep_file_name)
    expected_stamp_file = output_dir.GetPath(expected_stamp_file_name)
    if os.path.isfile(expected_stamp_file):
      os.remove(expected_stamp_file)
    builder.Run(DummyOpts(), ['-o', output_dir.GetPath(),
                              '--depdir', output_dir.GetPath(),
                              '--depfile', expected_dep_file,
                              '--depend-on-stamp'])
    self.assertTrue(os.path.isfile(expected_stamp_file))
    first_mtime = os.stat(expected_stamp_file).st_mtime

    # Reset mtime to very old.
    OLDTIME = 10
    os.utime(expected_stamp_file, (OLDTIME, OLDTIME))

    builder.Run(DummyOpts(), ['-o', output_dir.GetPath(),
                              '--depdir', output_dir.GetPath(),
                              '--depfile', expected_dep_file,
                              '--depend-on-stamp'])
    self.assertTrue(os.path.isfile(expected_stamp_file))
    second_mtime = os.stat(expected_stamp_file).st_mtime

    # Some OS have a 2s stat resolution window, so can't do a direct comparison.
    self.assertTrue((second_mtime - OLDTIME) > 5)
    self.assertTrue(abs(second_mtime - first_mtime) < 5)

    self.assertTrue(os.path.isfile(expected_dep_file))
    with open(expected_dep_file) as f:
      line = f.readline()
      (dep_output_file, deps_string) = line.split(': ')
      deps = deps_string.split(' ')

      self.assertEqual(expected_stamp_file_name, dep_output_file)
      self.assertEqual(deps, [
          util.PathFromRoot('grit/testdata/substitute.xmb'),
      ])
    output_dir.CleanUp()


if __name__ == '__main__':
  unittest.main()
