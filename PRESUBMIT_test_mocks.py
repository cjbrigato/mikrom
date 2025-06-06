# Copyright 2014 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from collections import defaultdict
import fnmatch
import json
import os
import re
import subprocess
import sys


_REPO_ROOT = os.path.abspath(os.path.dirname(__file__))

# TODO(dcheng): It's kind of horrible that this is copy and pasted from
# presubmit_canned_checks.py, but it's far easier than any of the alternatives.
def _ReportErrorFileAndLine(filename, line_num, dummy_line):
    """Default error formatter for _FindNewViolationsOfRule."""
    return '%s:%s' % (filename, line_num)


class MockCannedChecks(object):
    def _FindNewViolationsOfRule(self, callable_rule, input_api,
                                 source_file_filter=None,
                                 error_formatter=_ReportErrorFileAndLine):
        """Find all newly introduced violations of a per-line rule (a callable).

    Arguments:
      callable_rule: a callable taking a file extension and line of input and
        returning True if the rule is satisfied and False if there was a
        problem.
      input_api: object to enumerate the affected files.
      source_file_filter: a filter to be passed to the input api.
      error_formatter: a callable taking (filename, line_number, line) and
        returning a formatted error string.

    Returns:
      A list of the newly-introduced violations reported by the rule.
    """
        errors = []
        for f in input_api.AffectedFiles(include_deletes=False,
                                         file_filter=source_file_filter):
            # For speed, we do two passes, checking first the full file.
            # Shelling out to the SCM to determine the changed region can be
            # quite expensive on Win32. Assuming that most files will be kept
            # problem-free, we can skip the SCM operations most of the time.
            extension = str(f.UnixLocalPath()).rsplit('.', 1)[-1]
            if all(callable_rule(extension, line) for line in f.NewContents()):
                # No violation found in full text: can skip considering diff.
                continue

            for line_num, line in f.ChangedContents():
                if not callable_rule(extension, line):
                    errors.append(
                        error_formatter(f.LocalPath(), line_num, line))

        return errors


class MockInputApi(object):
    """Mock class for the InputApi class.

  This class can be used for unittests for presubmit by initializing the files
  attribute as the list of changed files.
  """

    DEFAULT_FILES_TO_SKIP = ()

    def __init__(self):
        self.basename = os.path.basename
        self.canned_checks = MockCannedChecks()
        self.fnmatch = fnmatch
        self.json = json
        self.re = re

        # We want os_path.exists() and os_path.isfile() to work for files
        # that are both in the filesystem and mock files we have added
        # via InitFiles().
        # By setting os_path to a copy of os.path rather than directly we
        # can not only have os_path.exists() be a combined output for fake
        # files and real files in the filesystem.
        import importlib.util
        SPEC_OS_PATH = importlib.util.find_spec('os.path')
        os_path1 = importlib.util.module_from_spec(SPEC_OS_PATH)
        SPEC_OS_PATH.loader.exec_module(os_path1)
        sys.modules['os_path1'] = os_path1
        self.os_path = os_path1

        self.platform = sys.platform
        self.python_executable = sys.executable
        self.python3_executable = sys.executable
        self.platform = sys.platform
        self.subprocess = subprocess
        self.sys = sys
        self.files = []
        self.is_committing = False
        self.change = MockChange([])
        self.presubmit_local_path = os.path.dirname(
            os.path.abspath(sys.argv[0]))
        self.is_windows = sys.platform == 'win32'
        self.no_diffs = False
        # Although this makes assumptions about command line arguments used by
        # test scripts that create mocks, it is a convenient way to set up the
        # verbosity via the input api.
        self.verbose = '--verbose' in sys.argv

    def InitFiles(self, files):
        # Actual presubmit calls normpath, but too many tests break to do this
        # right in MockFile().
        for f in files:
            f._local_path = os.path.normpath(f._local_path)
        self.files = files
        files_that_exist = {
            p.AbsoluteLocalPath()
            for p in files if p.Action() != 'D'
        }

        def mock_exists(path):
            if not os.path.isabs(path):
                path = os.path.join(self.presubmit_local_path, path)
            path = os.path.normpath(path)
            return path in files_that_exist or any(
                f.startswith(path)
                for f in files_that_exist) or os.path.exists(path)

        def mock_isfile(path):
            if not os.path.isabs(path):
                path = os.path.join(self.presubmit_local_path, path)
            path = os.path.normpath(path)
            return path in files_that_exist or os.path.isfile(path)

        def mock_glob(pattern, *args, **kwargs):
            return fnmatch.filter(files_that_exist, pattern)

        # Do not stub these in the constructor to not break existing tests.
        self.os_path.exists = mock_exists
        self.os_path.isfile = mock_isfile
        self.glob = mock_glob

    def AffectedFiles(self, file_filter=None, include_deletes=True):
        for file in self.files:
            if file_filter and not file_filter(file):
                continue
            if not include_deletes and file.Action() == 'D':
                continue
            yield file

    def RightHandSideLines(self, source_file_filter=None):
        affected_files = self.AffectedSourceFiles(source_file_filter)
        for af in affected_files:
            lines = af.ChangedContents()
            for line in lines:
                yield (af, line[0], line[1])

    def AffectedSourceFiles(self, file_filter=None):
        return self.AffectedFiles(file_filter=file_filter,
                                  include_deletes=False)

    def AffectedTestableFiles(self, file_filter=None):
        return self.AffectedFiles(file_filter=file_filter,
                                  include_deletes=False)

    def FilterSourceFile(self, file, files_to_check=(), files_to_skip=()):
        local_path = file.UnixLocalPath()
        found_in_files_to_check = not files_to_check
        if files_to_check:
            if type(files_to_check) is str:
                raise TypeError(
                    'files_to_check should be an iterable of strings')
            for pattern in files_to_check:
                compiled_pattern = re.compile(pattern)
                if compiled_pattern.match(local_path):
                    found_in_files_to_check = True
                    break
        if files_to_skip:
            if type(files_to_skip) is str:
                raise TypeError(
                    'files_to_skip should be an iterable of strings')
            for pattern in files_to_skip:
                compiled_pattern = re.compile(pattern)
                if compiled_pattern.match(local_path):
                    return False
        return found_in_files_to_check

    def LocalPaths(self):
        return [file.LocalPath() for file in self.files]

    def PresubmitLocalPath(self):
        return self.presubmit_local_path

    def ReadFile(self, filename, mode='r'):
        if hasattr(filename, 'AbsoluteLocalPath'):
            filename = filename.AbsoluteLocalPath()
        norm_filename = os.path.normpath(filename)
        for file_ in self.files:
            to_check = (file_.LocalPath(), file_.AbsoluteLocalPath())
            if filename in to_check or norm_filename in to_check:
                return '\n'.join(file_.NewContents())
        # Otherwise, file is not in our mock API.
        raise IOError("No such file or directory: '%s'" % filename)


class MockOutputApi(object):
    """Mock class for the OutputApi class.

  An instance of this class can be passed to presubmit unittests for outputting
  various types of results.
  """

    class PresubmitResult(object):

        def __init__(self, message, items=None, long_text='', locations=[]):
            self.message = message
            self.items = items
            self.long_text = long_text
            self.locations = locations

        def __repr__(self):
            return self.message

    class PresubmitError(PresubmitResult):

        def __init__(self, *args, **kwargs):
            MockOutputApi.PresubmitResult.__init__(self, *args, **kwargs)
            self.type = 'error'

    class PresubmitPromptWarning(PresubmitResult):

        def __init__(self, *args, **kwargs):
            MockOutputApi.PresubmitResult.__init__(self, *args, **kwargs)
            self.type = 'warning'

    class PresubmitNotifyResult(PresubmitResult):

        def __init__(self, *args, **kwargs):
            MockOutputApi.PresubmitResult.__init__(self, *args, **kwargs)
            self.type = 'notify'

    class PresubmitPromptOrNotify(PresubmitResult):

        def __init__(self, *args, **kwargs):
            MockOutputApi.PresubmitResult.__init__(self, *args,  **kwargs)
            self.type = 'promptOrNotify'

    class PresubmitResultLocation(object):

        def __init__(self, file_path, start_line, end_line):
            self.file_path = file_path
            self.start_line = start_line
            self.end_line = end_line

    def __init__(self):
        self.more_cc = []

    def AppendCC(self, more_cc):
        self.more_cc.append(more_cc)


class MockFile(object):
    """Mock class for the File class.

  This class can be used to form the mock list of changed files in
  MockInputApi for presubmit unittests.
  """

    def __init__(self,
                 local_path,
                 new_contents,
                 old_contents=None,
                 action='A',
                 scm_diff=None):
        self._local_path = local_path
        self._new_contents = new_contents
        self._changed_contents = [(i + 1, l)
                                  for i, l in enumerate(new_contents)]
        self._action = action
        if scm_diff:
            self._scm_diff = scm_diff
        else:
            self._scm_diff = ("--- /dev/null\n+++ %s\n@@ -0,0 +1,%d @@\n" %
                              (local_path, len(new_contents)))
            for l in new_contents:
                self._scm_diff += "+%s\n" % l
        self._old_contents = old_contents or []

    def __str__(self):
        return self._local_path

    def Action(self):
        return self._action

    def ChangedContents(self):
        return self._changed_contents

    def NewContents(self):
        return self._new_contents

    def LocalPath(self):
        return self._local_path

    def AbsoluteLocalPath(self):
        return os.path.join(_REPO_ROOT, self._local_path)

    # This method must be functionally identical to
    # AffectedFile.UnixLocalPath(), but must normalize Windows-style
    # paths even on non-Windows platforms because tests contain them
    def UnixLocalPath(self):
        return self._local_path.replace('\\', '/')

    def GenerateScmDiff(self):
        return self._scm_diff

    def OldContents(self):
        return self._old_contents

    def rfind(self, p):
        """Required when os.path.basename() is called on MockFile."""
        return self._local_path.rfind(p)

    def __getitem__(self, i):
        """Required when os.path.basename() is called on MockFile."""
        return self._local_path[i]

    def __len__(self):
        """Required when os.path.basename() is called on MockFile."""
        return len(self._local_path)

    def replace(self, altsep, sep):
        """Required when os.path.basename() is called on MockFile."""
        return self._local_path.replace(altsep, sep)


class MockAffectedFile(MockFile):
    pass


class MockChange(object):
    """Mock class for Change class.

  This class can be used in presubmit unittests to mock the query of the
  current change.
  """

    def __init__(self, changed_files):
        self._changed_files = changed_files
        self.author_email = None
        self.footers = defaultdict(list)

    def LocalPaths(self):
        return self._changed_files

    def AffectedFiles(self,
                      include_dirs=False,
                      include_deletes=True,
                      file_filter=None):
        return self._changed_files

    def GitFootersFromDescription(self):
        return self.footers

    def RepositoryRoot(self):
        return _REPO_ROOT
