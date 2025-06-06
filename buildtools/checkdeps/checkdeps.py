#!/usr/bin/env python3
# Copyright 2012 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Makes sure that files include headers from allowed directories.

Checks DEPS files in the source tree for rules, and applies those rules to
"#include" and "import" directives in the .cpp, .java, and .proto source files.
Any source file including something not permitted by the DEPS files will fail.

See README.md for a detailed description of the DEPS format.
"""



import os
import optparse
import re
import sys

import proto_checker
import cpp_checker
import java_checker
import results

from builddeps import DepsBuilder
from rules import Rule, Rules


def _IsTestFile(filename):
  """Does a rudimentary check to try to skip test files; this could be
  improved but is good enough for now.
  """
  return re.match(r'(test|mock|dummy)_.*|.*_[a-z]*test\.(cc|mm|java)', filename)


class DepsChecker(DepsBuilder):
  """Parses include_rules from DEPS files and verifies files in the
  source tree against them.
  """

  def __init__(self,
               base_directory=None,
               extra_repos=[],
               verbose=False,
               being_tested=False,
               ignore_temp_rules=False,
               skip_tests=False,
               resolve_dotdot=True):
    """Creates a new DepsChecker.

    Args:
      base_directory: OS-compatible path to root of checkout, e.g. C:\\chr\\src.
      verbose: Set to true for debug output.
      being_tested: Set to true to ignore the DEPS file at
                    buildtools/checkdeps/DEPS.
      ignore_temp_rules: Ignore rules that start with Rule.TEMP_ALLOW ("!").
    """
    DepsBuilder.__init__(
        self, base_directory, extra_repos, verbose, being_tested,
        ignore_temp_rules)

    self._skip_tests = skip_tests
    self._resolve_dotdot = resolve_dotdot
    self.results_formatter = results.NormalResultsFormatter(verbose)

  def Report(self):
    """Prints a report of results, and returns an exit code for the process."""
    if self.results_formatter.GetResults():
      self.results_formatter.PrintResults()
      return 1
    print('\nSUCCESS\n')
    return 0

  def CheckDirectory(self, start_dir):
    """Checks all relevant source files in the specified directory and
    its subdirectories for compliance with DEPS rules throughout the
    tree (starting at |self.base_directory|).  |start_dir| must be a
    subdirectory of |self.base_directory|.

    On completion, self.results_formatter has the results of
    processing, and calling Report() will print a report of results.
    """
    java = java_checker.JavaChecker(self.base_directory, self.verbose)
    cpp = cpp_checker.CppChecker(
        self.verbose, self._resolve_dotdot, self.base_directory)
    proto = proto_checker.ProtoChecker(
        self.verbose, self._resolve_dotdot, self.base_directory)
    checkers = dict(
        (extension, checker)
        for checker in [java, cpp, proto] for extension in checker.EXTENSIONS)

    for rules, file_paths in self.GetAllRulesAndFiles(start_dir):
      for full_name in file_paths:
        if self._skip_tests and _IsTestFile(os.path.basename(full_name)):
          continue
        file_extension = os.path.splitext(full_name)[1]
        if not file_extension in checkers:
          continue
        checker = checkers[file_extension]
        file_status = checker.CheckFile(rules, full_name)
        if file_status.HasViolations():
          self.results_formatter.AddError(file_status)

  def CheckIncludesAndImports(self, added_lines, checker):
    """Check new import/#include statements added in the change
    being presubmit checked.

    Args:
      added_lines: ((file_path, (changed_line, changed_line, ...), ...)
      checker: CppChecker/JavaChecker/ProtoChecker checker instance

    Return:
      A list of tuples, (bad_file_path, rule_type, rule_description)
      where rule_type is one of Rule.DISALLOW or Rule.TEMP_ALLOW and
      rule_description is human-readable. Empty if no problems.
    """
    problems = []
    for file_path, changed_lines in added_lines:
      if not checker.ShouldCheck(file_path):
        continue
      rules_for_file = self.GetDirectoryRules(os.path.dirname(file_path))
      if not rules_for_file:
        continue
      for line in changed_lines:
        is_include, violation = checker.CheckLine(
            rules_for_file, line, file_path, True)
        if not violation:
          continue
        rule_type = violation.violated_rule.allow
        if rule_type == Rule.ALLOW:
          continue
        violation_text = results.NormalResultsFormatter.FormatViolation(
            violation, self.verbose)
        problems.append((file_path, rule_type, violation_text))
    return problems

  def CheckAddedCppIncludes(self, added_includes):
    """This is used from PRESUBMIT.py to check new #include statements added in
    the change being presubmit checked.

    Args:
      added_includes: ((file_path, (include_line, include_line, ...), ...)

    Return:
      A list of tuples, (bad_file_path, rule_type, rule_description)
      where rule_type is one of Rule.DISALLOW or Rule.TEMP_ALLOW and
      rule_description is human-readable. Empty if no problems.
    """
    return self.CheckIncludesAndImports(
        added_includes, cpp_checker.CppChecker(self.verbose))

  def CheckAddedJavaImports(self, added_imports,
                            allow_multiple_definitions=None):
    """This is used from PRESUBMIT.py to check new import statements added in
    the change being presubmit checked.

    Args:
      added_imports: ((file_path, (import_line, import_line, ...), ...)
      allow_multiple_definitions: [file_name, file_name, ...]. List of java
                                  file names allowing multiple definitions in
                                  presubmit check.

    Return:
      A list of tuples, (bad_file_path, rule_type, rule_description)
      where rule_type is one of Rule.DISALLOW or Rule.TEMP_ALLOW and
      rule_description is human-readable. Empty if no problems.
    """
    return self.CheckIncludesAndImports(
        added_imports,
        java_checker.JavaChecker(self.base_directory, self.verbose,
                                 added_imports, allow_multiple_definitions))

  def CheckAddedProtoImports(self, added_imports):
    """This is used from PRESUBMIT.py to check new #import statements added in
    the change being presubmit checked.

    Args:
      added_imports : ((file_path, (import_line, import_line, ...), ...)

    Return:
      A list of tuples, (bad_file_path, rule_type, rule_description)
      where rule_type is one of Rule.DISALLOW or Rule.TEMP_ALLOW and
      rule_description is human-readable. Empty if no problems.
    """
    return self.CheckIncludesAndImports(
        added_imports, proto_checker.ProtoChecker(
            verbose=self.verbose, root_dir=self.base_directory))

def PrintUsage():
  print("""Usage: python checkdeps.py [--root <root>] [tocheck]

  --root ROOT Specifies the repository root. This defaults to "../../.."
              relative to the script file. This will be correct given the
              normal location of the script in "<root>/buildtools/checkdeps".

  --(others)  There are a few lesser-used options; run with --help to show them.

  tocheck  Specifies the directory, relative to root, to check. This defaults
           to "." so it checks everything.

Examples:
  python checkdeps.py
  python checkdeps.py --root c:\\source chrome""")


def main():
  option_parser = optparse.OptionParser()
  option_parser.add_option(
      '', '--root',
      default='', dest='base_directory',
      help='Specifies the repository root. This defaults '
           'to "../../.." relative to the script file, which '
           'will normally be the repository root.')
  option_parser.add_option(
      '', '--extra-repos',
      action='append', dest='extra_repos', default=[],
      help='Specifies extra repositories relative to root repository.')
  option_parser.add_option(
      '', '--ignore-temp-rules',
      action='store_true', dest='ignore_temp_rules', default=False,
      help='Ignore !-prefixed (temporary) rules.')
  option_parser.add_option(
      '', '--generate-temp-rules',
      action='store_true', dest='generate_temp_rules', default=False,
      help='Print rules to temporarily allow files that fail '
           'dependency checking.')
  option_parser.add_option(
      '', '--count-violations',
      action='store_true', dest='count_violations', default=False,
      help='Count #includes in violation of intended rules.')
  option_parser.add_option(
      '', '--skip-tests',
      action='store_true', dest='skip_tests', default=False,
      help='Skip checking test files (best effort).')
  option_parser.add_option(
      '-v', '--verbose',
      action='store_true', default=False,
      help='Print debug logging')
  option_parser.add_option(
      '', '--json',
      help='Path to JSON output file')
  option_parser.add_option(
      '', '--no-resolve-dotdot',
      action='store_false', dest='resolve_dotdot', default=True,
      help='resolve leading ../ in include directive paths relative '
           'to the file perfoming the inclusion.')

  options, args = option_parser.parse_args()

  deps_checker = DepsChecker(options.base_directory,
                             extra_repos=options.extra_repos,
                             verbose=options.verbose,
                             ignore_temp_rules=options.ignore_temp_rules,
                             skip_tests=options.skip_tests,
                             resolve_dotdot=options.resolve_dotdot)
  base_directory = deps_checker.base_directory  # Default if needed, normalized

  # Figure out which directory we have to check.
  start_dir = base_directory
  if len(args) == 1:
    # Directory specified. Start here. It's supposed to be relative to the
    # base directory.
    start_dir = os.path.abspath(os.path.join(base_directory, args[0]))
  elif len(args) >= 2 or (options.generate_temp_rules and
                          options.count_violations):
    # More than one argument, or incompatible flags, we don't handle this.
    PrintUsage()
    return 1

  if not start_dir.startswith(deps_checker.base_directory):
    print('Directory to check must be a subdirectory of the base directory,')
    print('but %s is not a subdirectory of %s' % (start_dir, base_directory))
    return 1

  print('Using base directory:', base_directory)
  print('Checking:', start_dir)

  if options.generate_temp_rules:
    deps_checker.results_formatter = results.TemporaryRulesFormatter()
  elif options.count_violations:
    deps_checker.results_formatter = results.CountViolationsFormatter()

  if options.json:
    deps_checker.results_formatter = results.JSONResultsFormatter(
        options.json, deps_checker.results_formatter)

  deps_checker.CheckDirectory(start_dir)
  return deps_checker.Report()


if '__main__' == __name__:
  sys.exit(main())
