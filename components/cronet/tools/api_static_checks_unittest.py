#!/usr/bin/env python3
# Copyright 2016 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""api_static_checks_unittest.py - Unittests for api_static_checks.py"""

import contextlib
import hashlib
import io
import os
import shutil
import sys
import tempfile
import unittest

REPOSITORY_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), '..', '..', '..'))
sys.path.insert(0, REPOSITORY_ROOT)

from build.android.gyp.util import build_utils  # pylint: disable=wrong-import-position
from components.cronet.tools import api_static_checks  # pylint: disable=wrong-import-position
from components.cronet.tools import update_api  # pylint: disable=wrong-import-position

JAR_PATH = os.path.join(build_utils.JAVA_HOME, 'bin', 'jar')
JAVAC_PATH = os.path.join(build_utils.JAVA_HOME, 'bin', 'javac')

# pylint: disable=useless-object-inheritance

ERROR_PREFIX_CHECK_API_CALLS = (
    """ERROR: Found the following calls from implementation classes through
       API classes.  These could fail if older API is used that
       does not contain newer methods.  Please call through a
       wrapper class from VersionSafeCallbacks.
""")

ERROR_PREFIX_UPDATE_API = ("""ERROR: This API was modified or removed:
             """)

ERROR_SUFFIX_UPDATE_API = ("""

       Cronet API methods and classes cannot be modified.
""")

CHECK_API_VERSION_PREFIX = (
    """DO NOT EDIT THIS FILE, USE update_api.py TO UPDATE IT

""")

API_FILENAME = './android/api.txt'
API_VERSION_FILENAME = './android/api_version.txt'


@contextlib.contextmanager
def capture_output():
  # A contextmanger that collects the stdout and stderr of wrapped code

  oldout, olderr = sys.stdout, sys.stderr
  try:
    out = [io.StringIO(), io.StringIO()]
    sys.stdout, sys.stderr = out
    yield out
  finally:
    sys.stdout, sys.stderr = oldout, olderr
    out[0] = out[0].getvalue()
    out[1] = out[1].getvalue()


class ApiStaticCheckUnitTest(unittest.TestCase):

  def setUp(self):
    self.exe_path = os.path.join(REPOSITORY_ROOT, 'out')
    self.temp_dir = tempfile.mkdtemp(dir=self.exe_path)
    os.chdir(self.temp_dir)
    os.mkdir('android')
    with open(API_VERSION_FILENAME, 'w') as api_version_file:
      api_version_file.write('0')
    with open(API_FILENAME, 'w') as api_file:
      api_file.write('}\nStamp: 7d9d25f71cb8a5aba86202540a20d405\n')
    shutil.copytree(os.path.dirname(__file__), 'tools')

  def tearDown(self):
    shutil.rmtree(self.temp_dir)

  def make_jar(self, java, class_name):
    # Compile |java| wrapped in a class named |class_name| to a jar file and
    # return jar filename.

    java_filename = class_name + '.java'
    class_filenames = class_name + '*.class'
    jar_filename = class_name + '.jar'

    with open(java_filename, 'w') as java_file:
      java_file.write('public class %s {' % class_name)
      java_file.write(java)
      java_file.write('}')
    os.system(f'{os.path.abspath(JAVAC_PATH)} {java_filename}')
    os.system(
        f'{os.path.abspath(JAR_PATH)} cf {jar_filename} {class_filenames}')
    return jar_filename

  def run_check_api_calls(self, api_java, impl_java):
    test = self

    class MockOpts(object):

      def __init__(self):
        self.api_jar = test.make_jar(api_java, 'Api')
        self.impl_jar = [test.make_jar(impl_java, 'Impl')]

    opts = MockOpts()
    with capture_output() as return_output:
      return_code = api_static_checks.check_api_calls(opts)
    return [return_code, return_output[0]]

  def test_check_api_calls_success(self):
    # Test simple classes with functions
    self.assertEqual(self.run_check_api_calls('void a(){}', 'void b(){}'),
                     [True, ''])
    # Test simple classes with functions calling themselves
    self.assertEqual(
        self.run_check_api_calls('void a(){} void b(){a();}',
                                 'void c(){} void d(){c();}'), [True, ''])

  def test_check_api_calls_failure(self):
    # Test static call
    self.assertEqual(
        self.run_check_api_calls('public static void a(){}',
                                 'void b(){Api.a();}'),
        [False, ERROR_PREFIX_CHECK_API_CALLS + 'Impl/b -> Api/a:()V\n'])
    # Test virtual call
    self.assertEqual(
        self.run_check_api_calls('public void a(){}',
                                 'void b(){new Api().a();}'),
        [False, ERROR_PREFIX_CHECK_API_CALLS + 'Impl/b -> Api/a:()V\n'])

  def run_check_api_version(self, java):
    OUT_FILENAME = 'out.txt'
    return_code = os.system('./tools/update_api.py --api_jar %s > %s' %
                            (self.make_jar(java, 'Api'), OUT_FILENAME))
    with open(API_FILENAME, 'r') as api_file:
      api = api_file.read()
    with open(API_VERSION_FILENAME, 'r') as api_version_file:
      api_version = api_version_file.read()
    with open(OUT_FILENAME, 'r') as out_file:
      output = out_file.read()

    # Verify stamp
    api_stamp = api.split('\n')[-2]
    stamp_length = len('Stamp: 78418460c193047980ae9eabb79293f2\n')
    api = api[:-stamp_length]
    api_hash = hashlib.md5()
    api_hash.update(api.encode('utf-8'))
    self.assertEqual(api_stamp, 'Stamp: %s' % api_hash.hexdigest())

    return [return_code == 0, output, api, api_version]

  def test_split_by_class_sort(self):
    expected = [
        [
            'public class Api {',
            'public Api();',
            'public void a();',
            'public void b();',
            '}',
        ],
        [
            'public class zee {',
            'public abstract int z();',
            'public void x();',
            'public void y();',
            'public zee();',
            '}',
        ],
    ]
    input_str = """Compiled from Api.java
public class Api {
public void b();
public Api();
public void a();
}
Compiled from zee.java
public class zee {
public void x();
public zee();
public void y();
public abstract int z();
}
"""
    self.assertEqual(update_api._split_by_class(input_str.splitlines()),
                     expected)

  def test_update_api_success(self):
    # Test simple new API
    self.assertEqual(self.run_check_api_version('public void a(){}'), [
        True, '', CHECK_API_VERSION_PREFIX + """public class Api {
  public Api();
  public void a();
}
""", '1'
    ])
    # Test version number not increased when API not changed
    self.assertEqual(self.run_check_api_version('public void a(){}'), [
        True, '', CHECK_API_VERSION_PREFIX + """public class Api {
  public Api();
  public void a();
}
""", '1'
    ])
    # Test acceptable API method addition
    self.assertEqual(
        self.run_check_api_version('public void a(){} public void b(){}'), [
            True, '', CHECK_API_VERSION_PREFIX + """public class Api {
  public Api();
  public void a();
  public void b();
}
""", '2'
        ])
    # Test version number not increased when API not changed
    self.assertEqual(
        self.run_check_api_version('public void a(){} public void b(){}'), [
            True, '', CHECK_API_VERSION_PREFIX + """public class Api {
  public Api();
  public void a();
  public void b();
}
""", '2'
        ])
    # Test acceptable API class addition
    self.assertEqual(
        self.run_check_api_version(
            'public void a(){} public void b(){} public class C {}'), [
                True, '', CHECK_API_VERSION_PREFIX + """public class Api$C {
  public Api$C(Api);
}
public class Api {
  public Api();
  public void a();
  public void b();
}
""", '3'
            ])
    # Test version number not increased when API not changed
    self.assertEqual(
        self.run_check_api_version(
            'public void a(){} public void b(){} public class C {}'), [
                True, '', CHECK_API_VERSION_PREFIX + """public class Api$C {
  public Api$C(Api);
}
public class Api {
  public Api();
  public void a();
  public void b();
}
""", '3'
            ])

  def test_update_api_failure(self):
    # Create a simple new API
    self.assertEqual(self.run_check_api_version('public void a(){}'), [
        True, '', CHECK_API_VERSION_PREFIX + """public class Api {
  public Api();
  public void a();
}
""", '1'
    ])
    # Test removing API method not allowed
    self.assertEqual(self.run_check_api_version(''), [
        False,
        ERROR_PREFIX_UPDATE_API + 'public void a();' + ERROR_SUFFIX_UPDATE_API,
        CHECK_API_VERSION_PREFIX + """public class Api {
  public Api();
  public void a();
}
""", '1'
    ])
    # Test modifying API method not allowed
    self.assertEqual(self.run_check_api_version('public void a(int x){}'), [
        False,
        ERROR_PREFIX_UPDATE_API + 'public void a();' + ERROR_SUFFIX_UPDATE_API,
        CHECK_API_VERSION_PREFIX + """public class Api {
  public Api();
  public void a();
}
""", '1'
    ])
