#!/usr/bin/env python3
# Copyright 2012 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Makes sure files have the right permissions.

Some developers have broken SCM configurations that flip the executable
permission on for no good reason. Unix developers who run ls --color will then
see .cc files in green and get confused.

- For file extensions that must be executable, add it to EXECUTABLE_EXTENSIONS.
- For file extensions that must not be executable, add it to
  NOT_EXECUTABLE_EXTENSIONS.
- To ignore all the files inside a directory, add it to IGNORED_PATHS.
- For file base name with ambiguous state and that should not be checked for
  shebang, add it to IGNORED_FILENAMES.

Any file not matching the above will be opened and looked if it has a shebang
or an ELF or Mach-O header. If this does not match the executable bit on the
file, the file will be flagged. Mach-O files are allowed to exist with or
without an executable bit set, as there are many examples of it appearing as
test data, and as Mach-O types such as dSYM that canonically do not have their
executable bits set.

Note that all directory separators must be slashes (Unix-style) and not
backslashes. All directories should be relative to the source root and all
file paths should be only lowercase.
"""

from __future__ import print_function

import json
import logging
import optparse
import os
import stat
import string
import subprocess
import sys

#### USER EDITABLE SECTION STARTS HERE ####

# Files with these extensions must have executable bit set.
#
# Case-sensitive.
EXECUTABLE_EXTENSIONS = (
  'bat',
  'dll',
  'exe',
)

# Files for which the executable bit may or may not be set.
IGNORED_EXTENSIONS = (
  'dylib',
)

# These files must have executable bit set.
#
# Case-insensitive, lower-case only.
EXECUTABLE_PATHS = (
  'chrome/test/data/app_shim/app_shim_32_bit.app/contents/'
      'macos/app_mode_loader',
)

# These files must not have the executable bit set. This is mainly a performance
# optimization as these files are not checked for shebang. The list was
# partially generated from:
# git ls-files | grep "\\." | sed 's/.*\.//' | sort | uniq -c | sort -b -g
#
# Case-sensitive.
NON_EXECUTABLE_EXTENSIONS = (
  '1',
  '3ds',
  'S',
  'am',
  'applescript',
  'asm',
  'c',
  'cc',
  'cfg',
  'chromium',
  'cpp',
  'crx',
  'cs',
  'css',
  'cur',
  'def',
  'der',
  'expected',
  'gif',
  'grd',
  'gyp',
  'gypi',
  'h',
  'hh',
  'htm',
  'html',
  'hyph',
  'ico',
  'idl',
  'java',
  'jpg',
  'js',
  'json',
  'm',
  'm4',
  'mm',
  'mms',
  'mock-http-headers',
  'nexe',
  'nmf',
  'onc',
  'pat',
  'patch',
  'pdf',
  'pem',
  'plist',
  'png',
  'proto',
  'rc',
  'rfx',
  'rgs',
  'rules',
  'spec',
  'sql',
  'srpc',
  'svg',
  'tcl',
  'test',
  'tga',
  'txt',
  'vcproj',
  'vsprops',
  'webm',
  'word',
  'xib',
  'xml',
  'xtb',
  'zip',
)

# These files must not have executable bit set.
#
# Case-insensitive, lower-case only.
NON_EXECUTABLE_PATHS = (
  'build/android/tests/symbolize/liba.so',
  'build/android/tests/symbolize/libb.so',
  'chrome/installer/mac/sign_app.sh.in',
  'chrome/installer/mac/sign_versioned_dir.sh.in',
  'courgette/testdata/elf-32-1',
  'courgette/testdata/elf-32-2',
  'courgette/testdata/elf-64',
)

# File names that are always whitelisted.  (These are mostly autoconf spew.)
#
# Case-sensitive.
IGNORED_FILENAMES = (
  'config.guess',
  'config.sub',
  'configure',
  'depcomp',
  'install-sh',
  'missing',
  'mkinstalldirs',
  'naclsdk',
  'scons',
)

# File paths starting with one of these will be ignored as well.
# Please consider fixing your file permissions, rather than adding to this list.
#
# Case-insensitive, lower-case only.
IGNORED_PATHS = (
    'native_client_sdk/src/build_tools/sdk_tools/third_party/fancy_urllib/'
    '__init__.py',
    'out/',
    'third_party/rust/chromium_crates_io/vendor/',
    'third_party/wpt_tools/wpt/tools/third_party/',
    # TODO(maruel): Fix these.
    'third_party/devscripts/licensecheck.pl.vanilla',
    'third_party/libxml/linux/xml2-config',
    'third_party/protobuf/',
    'third_party/sqlite/',
)

#### USER EDITABLE SECTION ENDS HERE ####

assert (set(EXECUTABLE_EXTENSIONS) & set(IGNORED_EXTENSIONS) &
        set(NON_EXECUTABLE_EXTENSIONS) == set())
assert set(EXECUTABLE_PATHS) & set(NON_EXECUTABLE_PATHS) == set()

VALID_CHARS = set(string.ascii_lowercase + string.digits + '/-_.')
for paths in (EXECUTABLE_PATHS, NON_EXECUTABLE_PATHS, IGNORED_PATHS):
  assert all(set(path).issubset(VALID_CHARS) for path in paths)

git_name = 'git.bat' if sys.platform.startswith('win') else 'git'


def capture(cmd, cwd):
  """Returns the output of a command.

  Ignores the error code or stderr.
  """
  logging.debug('%s; cwd=%s' % (' '.join(cmd), cwd))
  env = os.environ.copy()
  env['LANGUAGE'] = 'en_US.UTF-8'
  p = subprocess.Popen(
      cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd, env=env)
  return p.communicate()[0].decode('utf-8', 'ignore')


def get_git_root(dir_path):
  """Returns the git checkout root or None."""
  root = capture([git_name, 'rev-parse', '--show-toplevel'], dir_path).strip()
  if root:
    return root
  # Should not be reached.
  return None


def is_ignored(rel_path):
  """Returns True if rel_path is in our whitelist of files to ignore."""
  rel_path = rel_path.lower()
  return (
      os.path.basename(rel_path) in IGNORED_FILENAMES or
      rel_path.lower().startswith(IGNORED_PATHS))


def must_be_executable(rel_path):
  """The file name represents a file type that must have the executable bit
  set.
  """
  return (os.path.splitext(rel_path)[1][1:] in EXECUTABLE_EXTENSIONS or
          rel_path.lower() in EXECUTABLE_PATHS)


def ignored_extension(rel_path):
  """The file name represents a file type that may or may not have the
  executable set.
  """
  return os.path.splitext(rel_path)[1][1:] in IGNORED_EXTENSIONS


def must_not_be_executable(rel_path):
  """The file name represents a file type that must not have the executable
  bit set.
  """
  return (os.path.splitext(rel_path)[1][1:] in NON_EXECUTABLE_EXTENSIONS or
          rel_path.lower() in NON_EXECUTABLE_PATHS)


def has_executable_bit(full_path):
  """Returns if any executable bit is set."""
  if sys.platform.startswith('win'):
    # Using stat doesn't work on Windows, we have to ask git what the
    # permissions are.
    dir_part, file_part = os.path.split(full_path)
    bits = capture([git_name, 'ls-files', '-s', file_part], dir_part).strip()
    return bits.startswith('100755')
  permission = stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH
  return bool(permission & os.stat(full_path).st_mode)


def has_shebang_or_is_elf_or_mach_o(full_path):
  """Returns a three-element tuple that indicates if the file starts with #!/,
  is an ELF binary, or Mach-O binary.

  full_path is the absolute path to the file.
  """
  with open(full_path, 'rb') as f:
    data = f.read(4)
    return (
        data[:3] == b'#!/' or data == b'#! /',
        data == b'\x7fELF',  # ELFMAG
        data in (
            b'\xfe\xed\xfa\xce',  # MH_MAGIC
            b'\xce\xfa\xed\xfe',  # MH_CIGAM
            b'\xfe\xed\xfa\xcf',  # MH_MAGIC_64
            b'\xcf\xfa\xed\xfe',  # MH_CIGAM_64
            b'\xca\xfe\xba\xbe',  # FAT_MAGIC
            b'\xbe\xba\xfe\xca',  # FAT_CIGAM
            b'\xca\xfe\xba\xbf',  # FAT_MAGIC_64
            b'\xbf\xba\xfe\xca'))  # FAT_CIGAM_64


def check_file(root_path, rel_path):
  """Checks the permissions of the file whose path is root_path + rel_path and
  returns an error if it is inconsistent. Returns None on success.

  It is assumed that the file is not ignored by is_ignored().

  If the file name is matched with must_be_executable() or
  must_not_be_executable(), only its executable bit is checked.
  Otherwise, the first few bytes of the file are read to verify if it has a
  shebang or ELF or Mach-O header and compares this with the executable bit on
  the file.
  """
  full_path = os.path.join(root_path, rel_path)
  def result_dict(error):
    return {
      'error': error,
      'full_path': full_path,
      'rel_path': rel_path,
    }
  try:
    bit = has_executable_bit(full_path)
  except OSError:
    # It's faster to catch exception than call os.path.islink(). The Chromium
    # tree may have invalid symlinks.
    return None

  exec_add = 'git add --chmod=+x %s' % rel_path
  exec_remove = 'git add --chmod=-x %s' % rel_path
  if must_be_executable(rel_path):
    if not bit:
      return result_dict('Must have executable bit set: %s' % exec_add)
    return None
  if must_not_be_executable(rel_path):
    if bit:
      return result_dict('Must not have executable bit set: %s' % exec_remove)
    return None
  if ignored_extension(rel_path):
    return None

  # For the others, it depends on the file header.
  (shebang, elf, mach_o) = has_shebang_or_is_elf_or_mach_o(full_path)
  if bit != (shebang or elf or mach_o):
    if bit:
      return result_dict(
          'Has executable bit but not shebang or ELF or Mach-O header: %s' %
          exec_remove)
    if shebang:
      return result_dict('Has shebang but not executable bit: %s' % exec_add)
    if elf:
      return result_dict('Has ELF header but not executable bit: %s' % exec_add)
    # Mach-O is allowed to exist in the tree with or without an executable bit.
  return None


def check_files(root, files):
  gen = (check_file(root, f) for f in files
         if not is_ignored(f) and not os.path.isdir(f))
  return filter(None, gen)


class ApiBase:
  def __init__(self, root_dir, bare_output):
    self.root_dir = root_dir
    self.bare_output = bare_output
    self.count = 0
    self.count_read_header = 0

  def check_file(self, rel_path):
    logging.debug('check_file(%s)' % rel_path)
    self.count += 1

    if (not must_be_executable(rel_path) and
        not must_not_be_executable(rel_path)):
      self.count_read_header += 1

    return check_file(self.root_dir, rel_path)

  def check_dir(self, rel_path):
    return self.check(rel_path)

  def check(self, start_dir):
    """Check the files in start_dir, recursively check its subdirectories."""
    errors = []
    items = self.list_dir(start_dir)
    logging.info('check(%s) -> %d' % (start_dir, len(items)))
    for item in items:
      full_path = os.path.join(self.root_dir, start_dir, item)
      rel_path = full_path[len(self.root_dir) + 1:]
      if is_ignored(rel_path):
        continue
      if os.path.isdir(full_path):
        # Depth first.
        errors.extend(self.check_dir(rel_path))
      else:
        error = self.check_file(rel_path)
        if error:
          errors.append(error)
    return errors

  def list_dir(self, start_dir):
    """Lists all the files and directory inside start_dir."""
    return sorted(
      x for x in os.listdir(os.path.join(self.root_dir, start_dir))
      if not x.startswith('.')
    )


class ApiAllFilesAtOnceBase(ApiBase):
  _files = None

  def list_dir(self, start_dir):
    """Lists all the files and directory inside start_dir."""
    if self._files is None:
      self._files = sorted(self._get_all_files())
      if not self.bare_output:
        print('Found %s files' % len(self._files))
    start_dir = start_dir[len(self.root_dir) + 1:]
    return [
      x[len(start_dir):] for x in self._files if x.startswith(start_dir)
    ]

  def _get_all_files(self):
    """Lists all the files and directory inside self._root_dir."""
    raise NotImplementedError()


class ApiGit(ApiAllFilesAtOnceBase):
  def _get_all_files(self):
    # ls-files -s outputs in the following format:
    # <mode> <SP> <object> <SP> <stage> <TAB> <file>
    # Example output:
    # 100644 08f1a0445babd612aab0a9934eabc0a7ae3d48ef 0<TAB>README.md
    # 160000 e9f9f56b0dee9032936d23c81c8246ae0ffe36bd 0<TAB>build
    out = capture([git_name, 'ls-files', '-s'], cwd=self.root_dir).splitlines()

    # Return only actual files, which start with 100, and ignore anything else.
    # See: https://git-scm.com/book/en/v2/Git-Internals-Git-Objects
    return [x.split(maxsplit=3)[-1] for x in out if x.startswith('100')]


def get_scm(dir_path, bare):
  """Returns a properly configured ApiBase instance."""
  cwd = os.getcwd()
  root = get_git_root(dir_path or cwd)
  if root:
    if not bare:
      print('Found git repository at %s' % root)
    return ApiGit(dir_path or root, bare)

  # Returns a non-scm aware checker.
  if not bare:
    print('Failed to determine the SCM for %s' % dir_path)
  return ApiBase(dir_path or cwd, bare)


def main():
  usage = """Usage: python %prog [--root <root>] [tocheck]
  tocheck  Specifies the directory, relative to root, to check. This defaults
           to "." so it checks everything.

Examples:
  python %prog
  python %prog --root /path/to/source chrome"""

  parser = optparse.OptionParser(usage=usage)
  parser.add_option(
      '--root',
      help='Specifies the repository root. This defaults '
           'to the checkout repository root')
  parser.add_option(
      '-v', '--verbose', action='count', default=0, help='Print debug logging')
  parser.add_option(
      '--bare',
      action='store_true',
      default=False,
      help='Prints the bare filename triggering the checks')
  parser.add_option(
      '--file', action='append', dest='files',
      help='Specifics a list of files to check the permissions of. Only these '
      'files will be checked')
  parser.add_option(
      '--file-list',
      help='Specifies a file with a list of files (one per line) to check the '
      'permissions of. Only these files will be checked')
  parser.add_option('--json', help='Path to JSON output file')
  options, args = parser.parse_args()

  levels = [logging.ERROR, logging.INFO, logging.DEBUG]
  logging.basicConfig(level=levels[min(len(levels) - 1, options.verbose)])

  if len(args) > 1:
    parser.error('Too many arguments used')

  if options.files and options.file_list:
    parser.error('--file and --file-list are mutually exclusive options')

  if sys.platform.startswith(
      'win') and not options.files and not options.file_list:
    # checkperms of the entire tree on Windows takes many hours so is not
    # supported. Instead just check this script.
    options.files = [sys.argv[0]]
    options.root = '.'
    print('Full-tree checkperms not supported on Windows.')

  if options.root:
    options.root = os.path.abspath(options.root)

  if options.files:
    errors = check_files(options.root, options.files)
  elif options.file_list:
    with open(options.file_list) as file_list:
      files = file_list.read().splitlines()
    errors = check_files(options.root, files)
  else:
    api = get_scm(options.root, options.bare)
    start_dir = args[0] if args else api.root_dir
    errors = api.check(start_dir)

    if not options.bare:
      print('Processed %s files, %d files were tested for shebang/ELF/Mach-O '
            'header' % (api.count, api.count_read_header))

  # Convert to an actual list.
  errors = list(errors)

  if options.json:
    with open(options.json, 'w') as f:
      json.dump(errors, f)

  if errors:
    if options.bare:
      print('\n'.join(e['full_path'] for e in errors))
    else:
      print('\nFAILED\n')
      print('\n'.join('%s: %s' % (e['full_path'], e['error']) for e in errors))
    return 1
  if not options.bare:
    print('\nSUCCESS\n')
  return 0


if '__main__' == __name__:
  sys.exit(main())
