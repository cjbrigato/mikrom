# Copyright 2014 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import codecs
import contextlib
import json
import os
import logging
import platform
import subprocess
import sys
import tempfile
import time
import traceback

logging.basicConfig(level=logging.INFO)

sys.path.append(
    os.path.abspath(os.path.join(os.path.dirname(__file__), os.path.pardir)))
# //testing imports.
import test_env
if sys.platform.startswith('linux'):
  import xvfb

SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
SRC_DIR = os.path.abspath(
    os.path.join(SCRIPT_DIR, os.path.pardir, os.path.pardir))

# Use result_sink.py in //build/util/lib/results/ for uploading the
# results of non-isolated script tests.
BUILD_UTIL_DIR = os.path.join(SRC_DIR, 'build', 'util')
sys.path.insert(0, BUILD_UTIL_DIR)
try:
  from lib.results import result_sink
  from lib.results import result_types
except ImportError:
  # Some build-time scripts import this file and run into issues with
  # result_sink's dependency on requests since we can't depend on vpython
  # during build-time. So silently swallow the error in that case.
  result_sink = None

# run_web_tests.py returns the number of failures as the return
# code, but caps the return code at 101 to avoid overflow or colliding
# with reserved values from the shell.
MAX_FAILURES_EXIT_STATUS = 101

# Exit code to indicate infrastructure issue.
INFRA_FAILURE_EXIT_CODE = 87

# ACL might be explicitly set or inherited.
CORRECT_ACL_VARIANTS = [
    'APPLICATION PACKAGE AUTHORITY' \
    '\\ALL RESTRICTED APPLICATION PACKAGES:(OI)(CI)(RX)', \
    'APPLICATION PACKAGE AUTHORITY' \
    '\\ALL RESTRICTED APPLICATION PACKAGES:(I)(OI)(CI)(RX)'
]

# pylint: disable=useless-object-inheritance


def set_lpac_acls(acl_dir, is_test_script=False):
  """Sets LPAC ACLs on a directory. Windows 10 only."""
  if platform.release() != '10':
    return
  try:
    existing_acls = subprocess.check_output(['icacls', acl_dir],
                                            stderr=subprocess.STDOUT,
                                            universal_newlines=True)
  except subprocess.CalledProcessError as e:
    logging.error('Failed to retrieve existing ACLs for directory %s', acl_dir)
    logging.error('Command output: %s', e.output)
    sys.exit(e.returncode)
  acls_correct = False
  for acl in CORRECT_ACL_VARIANTS:
    if acl in existing_acls:
      acls_correct = True
  if not acls_correct:
    try:
      existing_acls = subprocess.check_output(
          ['icacls', acl_dir, '/grant', '*S-1-15-2-2:(OI)(CI)(RX)'],
          stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
      logging.error('Failed to retrieve existing ACLs for directory %s',
                    acl_dir)
      logging.error('Command output: %s', e.output)
      sys.exit(e.returncode)
  if not is_test_script:
    return
  # Bots running on luci use hardlinks that do not have correct ACLs so these
  # must be manually overridden here.
  with temporary_file() as tempfile_path:
    subprocess.check_output(
        ['icacls', acl_dir, '/save', tempfile_path, '/t', '/q', '/c'],
        stderr=subprocess.STDOUT)
    # ACL files look like this, e.g. for c:\a\b\c\d\Release_x64
    #
    # Release_x64
    # D:AI(A;OICI;0x1200a9;;;S-1-15-2-2)(A;OICIID;FA;;;BA)
    # Release_x64\icudtl_extra.dat
    # D:AI(A;ID;0x1200a9;;;S-1-15-2-2)(A;ID;FA;;;BA)(A;ID;0x1301bf;;;BU)
    with codecs.open(tempfile_path, encoding='utf_16_le') as aclfile:
      for filename in aclfile:
        acl = next(aclfile).strip()
        full_filename = os.path.abspath(
            os.path.join(acl_dir, os.pardir, filename.strip()))
        if 'S-1-15-2-2' in acl:
          continue
        if os.path.isdir(full_filename):
          continue
        subprocess.check_output(
            ['icacls', full_filename, '/grant', '*S-1-15-2-2:(RX)'],
            stderr=subprocess.STDOUT)


def run_script(argv, funcs):

  def parse_json(path):
    with open(path) as f:
      return json.load(f)

  parser = argparse.ArgumentParser()
  parser.add_argument('--build-dir',
                      help='Absolute path to build-dir.',
                      required=True)
  parser.add_argument('--paths', type=parse_json, default={})
  # Properties describe the environment of the build, and are the same per
  # script invocation.
  parser.add_argument('--properties', type=parse_json, default={})
  # Args contains per-invocation arguments that potentially change the
  # behavior of the script.
  parser.add_argument('--args', type=parse_json, default=[])

  subparsers = parser.add_subparsers()

  run_parser = subparsers.add_parser('run')
  run_parser.add_argument('--output',
                          type=argparse.FileType('w'),
                          required=True)
  run_parser.add_argument('--filter-file', type=argparse.FileType('r'))
  run_parser.set_defaults(func=funcs['run'])

  run_parser = subparsers.add_parser('compile_targets')
  run_parser.add_argument('--output',
                          type=argparse.FileType('w'),
                          required=True)
  run_parser.set_defaults(func=funcs['compile_targets'])

  args = parser.parse_args(argv)
  return args.func(args)


def run_command(argv, env=None, cwd=None):
  print('Running %r in %r (env: %r)' % (argv, cwd, env), file=sys.stderr)
  rc = test_env.run_command(argv, env=env, cwd=cwd)
  print('Command %r returned exit code %d' % (argv, rc), file=sys.stderr)
  return rc


@contextlib.contextmanager
def temporary_file():
  fd, path = tempfile.mkstemp()
  os.close(fd)
  try:
    yield path
  finally:
    os.remove(path)


def record_local_script_results(name, output_fd, failures, valid):
  """Records to a local json file and to RDB the results of the script test.

  For legacy reasons, local script tests (ie: script tests that run
  locally and that don't conform to the isolated-test API) are expected to
  record their results using a specific format. This method encapsulates
  that format and also uploads those results to Result DB.

  Args:
    name: Name of the script test.
    output_fd: A .write()-supporting file descriptor to write results to.
    failures: List of strings representing test failures.
    valid: Whether the results are valid.
  """
  local_script_results = {'valid': valid, 'failures': failures}
  with open(output_fd.name, 'w') as fd:
    json.dump(local_script_results, fd)

  if not result_sink:
    return
  result_sink_client = result_sink.TryInitClient()
  if not result_sink_client:
    return
  status = result_types.PASS
  if not valid:
    status = result_types.UNKNOWN
  elif failures:
    status = result_types.FAIL
  test_log = '\n'.join(failures)

  # Source comes from:
  # infra/go/src/go.chromium.org/luci/resultdb/sink/proto/v1/test_result.proto
  struct_test_dict = {
      'coarseName': None,  # Not used for single tests.
      'fineName': None,  # Not used for single tests.
      'caseNameComponents': ['*fixture'],
  }
  result_sink_client.Post(name,
                          status,
                          None,
                          test_log,
                          None,
                          test_id_structured=struct_test_dict)


def parse_common_test_results(json_results, test_separator='/'):

  def convert_trie_to_flat_paths(trie, prefix=None):
    # Also see blinkpy.web_tests.layout_package.json_results_generator
    result = {}
    for name, data in trie.items():
      if prefix:
        name = prefix + test_separator + name
      if len(data) and not 'actual' in data and not 'expected' in data:
        result.update(convert_trie_to_flat_paths(data, name))
      else:
        result[name] = data
    return result

  results = {
      'passes': {},
      'unexpected_passes': {},
      'failures': {},
      'unexpected_failures': {},
      'flakes': {},
      'unexpected_flakes': {},
  }

  # TODO(dpranke): crbug.com/357866 - we should simplify the handling of
  # both the return code and parsing the actual results, below.

  passing_statuses = ('PASS', 'SLOW', 'NEEDSREBASELINE')

  for test, result in convert_trie_to_flat_paths(json_results['tests']).items():
    key = 'unexpected_' if result.get('is_unexpected') else ''
    data = result['actual']
    actual_results = data.split()
    last_result = actual_results[-1]
    expected_results = result['expected'].split()

    if (len(actual_results) > 1 and
        (last_result in expected_results or last_result in passing_statuses)):
      key += 'flakes'
    elif last_result in passing_statuses:
      key += 'passes'
      # TODO(dpranke): crbug.com/357867 ...  Why are we assigning result
      # instead of actual_result here. Do we even need these things to be
      # hashes, or just lists?
      data = result
    else:
      key += 'failures'
    results[key][test] = data

  return results


def write_interrupted_test_results_to(filepath, test_start_time):
  """Writes a test results JSON file* to filepath.

  This JSON file is formatted to explain that something went wrong.

  *src/docs/testing/json_test_results_format.md

  Args:
    filepath: A path to a file to write the output to.
    test_start_time: The start time of the test run expressed as a
      floating-point offset in seconds from the UNIX epoch.
  """
  with open(filepath, 'w') as fh:
    output = {
        'interrupted': True,
        'num_failures_by_type': {},
        'seconds_since_epoch': test_start_time,
        'tests': {},
        'version': 3,
    }
    json.dump(output, fh)


def get_gtest_summary_passes(output):
  """Returns a mapping of test to boolean indicating if the test passed.

  Only partially parses the format. This code is based on code in tools/build,
  specifically
  https://chromium.googlesource.com/chromium/tools/build/+/17fef98756c5f250b20bf716829a0004857235ff/scripts/slave/recipe_modules/test_utils/util.py#189
  """
  if not output:
    return {}

  mapping = {}

  for cur_iteration_data in output.get('per_iteration_data', []):
    for test_fullname, results in cur_iteration_data.items():
      # Results is a list with one entry per test try. Last one is the final
      # result.
      last_result = results[-1]

      if last_result['status'] == 'SUCCESS':
        mapping[test_fullname] = True
      elif last_result['status'] != 'SKIPPED':
        mapping[test_fullname] = False

  return mapping


def extract_filter_list(filter_list):
  """Helper for isolated script test wrappers. Parses the
  --isolated-script-test-filter command line argument. Currently, double-colon
  ('::') is used as the separator between test names, because a single colon may
  be used in the names of perf benchmarks, which contain URLs.
  """
  return filter_list.split('::')


def add_emulator_args(parser):
  parser.add_argument(
      '--avd-config',
      type=os.path.realpath,
      help=('Path to the avd config. Required for Android products. '
            '(See //tools/android/avd/proto for message definition '
            'and existing *.textpb files.)'))
  parser.add_argument('--emulator-window',
                      action='store_true',
                      default=False,
                      help='Enable graphical window display on the emulator.')


class BaseIsolatedScriptArgsAdapter:
  """The base class for all script adapters that need to translate flags
  set by isolated script test contract into the specific test script's flags.
  """

  def __init__(self):
    self._parser = argparse.ArgumentParser()
    self._options = None
    self._rest_args = None
    self._script_writes_output_json = None
    self._parser.add_argument(
        '--isolated-outdir',
        type=str,
        required=False,
        help='value of $ISOLATED_OUTDIR from swarming task')
    self._parser.add_argument('--isolated-script-test-output',
                              type=os.path.abspath,
                              required=False,
                              help='path to write test results JSON object to')
    self._parser.add_argument('--isolated-script-test-filter',
                              type=str,
                              required=False)
    self._parser.add_argument('--isolated-script-test-repeat',
                              type=int,
                              required=False)
    self._parser.add_argument('--isolated-script-test-launcher-retry-limit',
                              type=int,
                              required=False)
    self._parser.add_argument('--isolated-script-test-also-run-disabled-tests',
                              default=False,
                              action='store_true',
                              required=False)

    self._parser.add_argument(
        '--xvfb',
        help='start xvfb. Ignored on unsupported platforms',
        action='store_true')
    # Used to create the correct subclass.
    self._parser.add_argument('--script-type',
                              choices=['isolated', 'typ', 'bare'],
                              help='Which script adapter to use')

    # Arguments that are ignored, but added here because it's easier to ignore
    # them to to update bot configs to not pass them.
    self._parser.add_argument('--isolated-script-test-chartjson-output')
    self._parser.add_argument('--isolated-script-test-perf-output')

  def parse_args(self, args=None):
    self._options, self._rest_args = self._parser.parse_known_args(args)

  @property
  def parser(self):
    return self._parser

  @property
  def options(self):
    return self._options

  @property
  def rest_args(self):
    return self._rest_args

  # Overridden by subclasses.
  # pylint: disable=no-self-use
  def generate_test_output_args(self, output):
    del output  # unused
    return []
  # pylint: enable=no-self-use

  # Overridden by subclasses.
  # pylint: disable=no-self-use
  def generate_test_filter_args(self, test_filter_str):
    del test_filter_str  # unused
    raise RuntimeError('Flag not supported.')
  # pylint: enable=no-self-use

  # Overridden by subclasses.
  # pylint: disable=no-self-use
  def generate_test_repeat_args(self, repeat_count):
    del repeat_count  # unused
    raise RuntimeError('Flag not supported.')
  # pylint: enable=no-self-use

  # Overridden by subclasses.
  # pylint: disable=no-self-use
  def generate_test_launcher_retry_limit_args(self, retry_limit):
    del retry_limit  # unused
    raise RuntimeError('Flag not supported.')
  # pylint: enable=no-self-use

  # Overridden by subclasses.
  # pylint: disable=no-self-use
  def generate_sharding_args(self, total_shards, shard_index):
    del total_shards, shard_index  # unused
    raise RuntimeError('Flag not supported.')
  # pylint: enable=no-self-use

  # Overridden by subclasses.
  # pylint: disable=no-self-use
  def generate_test_also_run_disabled_tests_args(self):
    raise RuntimeError('Flag not supported.')
  # pylint: enable=no-self-use

  # Overridden by subclasses.
  # pylint: disable=no-self-use
  def select_python_executable(self):
    return sys.executable
  # pylint: enable=no-self-use

  def generate_isolated_script_cmd(self):
    isolated_script_cmd = [self.select_python_executable()] + self.rest_args

    if self.options.isolated_script_test_output:
      output_args = self.generate_test_output_args(
          self.options.isolated_script_test_output)
      self._script_writes_output_json = bool(output_args)
      isolated_script_cmd += output_args

    # Augment test filter args if needed
    if self.options.isolated_script_test_filter:
      isolated_script_cmd += self.generate_test_filter_args(
          self.options.isolated_script_test_filter)

    # Augment test repeat if needed
    if self.options.isolated_script_test_repeat is not None:
      isolated_script_cmd += self.generate_test_repeat_args(
          self.options.isolated_script_test_repeat)

    # Augment test launcher retry limit args if needed
    if self.options.isolated_script_test_launcher_retry_limit is not None:
      isolated_script_cmd += self.generate_test_launcher_retry_limit_args(
          self.options.isolated_script_test_launcher_retry_limit)

    # Augment test also run disable tests args if needed
    if self.options.isolated_script_test_also_run_disabled_tests:
      isolated_script_cmd += self.generate_test_also_run_disabled_tests_args()

    # Augment shard args if needed
    env = os.environ.copy()

    total_shards = None
    shard_index = None

    if 'GTEST_TOTAL_SHARDS' in env:
      total_shards = int(env['GTEST_TOTAL_SHARDS'])
    if 'GTEST_SHARD_INDEX' in env:
      shard_index = int(env['GTEST_SHARD_INDEX'])
    if total_shards is not None and shard_index is not None:
      isolated_script_cmd += self.generate_sharding_args(
          total_shards, shard_index)

    return isolated_script_cmd

  def clean_up_after_test_run(self):
    pass

  def do_pre_test_run_tasks(self):
    pass

  def do_post_test_run_tasks(self):
    pass

  def _write_simple_test_results(self, start_time, exit_code):
    if exit_code is None:
      failure_type = 'CRASH'
    elif exit_code == 0:
      failure_type = 'PASS'
    else:
      failure_type = 'FAIL'

    test_name = os.path.basename(self._rest_args[0])
    # See //docs/testing/json_test_results_format.md
    results_json = {
        'version': 3,
        'interrupted': False,
        'num_failures_by_type': {
            failure_type: 1
        },
        'path_delimiter': '/',
        'seconds_since_epoch': start_time,
        'tests': {
            test_name: {
                'expected': 'PASS',
                'actual': failure_type,
                'time': time.time() - start_time,
            },
        },
    }
    with open(self.options.isolated_script_test_output, 'w') as fp:
      json.dump(results_json, fp)

  def run_test(self, cwd=None):
    self.parse_args()
    cmd = self.generate_isolated_script_cmd()

    self.do_pre_test_run_tasks()

    env = os.environ.copy()

    env['CHROME_HEADLESS'] = '1'
    print('Running command: %s\nwith env: %r' % (' '.join(cmd), env))
    sys.stdout.flush()
    start_time = time.time()
    try:
      if self.options.xvfb and sys.platform.startswith('linux'):
        exit_code = xvfb.run_executable(cmd, env, cwd=cwd)
      else:
        exit_code = test_env.run_command(cmd, env=env, cwd=cwd, log=False)
      print('Command returned exit code %d' % exit_code)
      sys.stdout.flush()
      self.do_post_test_run_tasks()
    except Exception:  # pylint: disable=broad-except
      traceback.print_exc()
      exit_code = None
    finally:
      self.clean_up_after_test_run()

    if (self.options.isolated_script_test_output
        and not self._script_writes_output_json):
      self._write_simple_test_results(start_time, exit_code)

    return exit_code if exit_code is not None else 2
