# Copyright 2016 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import functools
import logging
import optparse

from blinkpy.common.checkout.baseline_optimizer import BaselineOptimizer
from blinkpy.tool.commands.command import resolve_test_patterns
from blinkpy.tool.commands.rebaseline import AbstractParallelRebaselineCommand

_log = logging.getLogger(__name__)


class OptimizeBaselines(AbstractParallelRebaselineCommand):
    name = 'optimize-baselines'
    help_text = ('Reshuffles the baselines for the given tests to use '
                 'as little space on disk as possible.')
    show_in_main_help = True
    argument_names = '[TEST_NAMES]'

    all_option = optparse.make_option(
        '--all',
        dest='all_tests',
        action='store_true',
        default=False,
        help=('Optimize all tests (instead of using TEST_NAMES)'))
    check_option = optparse.make_option(
        '--check',
        action='store_true',
        help=('Only check for redundant baselines instead of removing them. '
              'Exits with code 0 if and only if no optimizations are '
              'possible.'))

    def __init__(self):
        super().__init__(options=[
            self.suffixes_option,
            self.port_name_option,
            self.all_option,
            self.check_option,
            self.test_name_file_option,
        ] + self.platform_options + self.wpt_options)
        self._successful = True

    def execute(self, options, args, tool):
        self._successful = True
        if options.test_name_file:
            tests = self._host_port.tests_from_file(options.test_name_file)
            args.extend(sorted(tests))

        if not args != options.all_tests:
            _log.error('Must provide one of --all or TEST_NAMES')
            return 1

        port_names = tool.port_factory.all_port_names(options.platform)
        if not port_names:
            _log.error("No port names match '%s'", options.platform)
            return 1

        test_set = self._get_test_set(options, args)
        if not test_set:
            _log.error('No tests to optimize. Ensure all listed tests exist.')
            return 1

        worker_factory = functools.partial(Worker,
                                           port_names=port_names,
                                           options=options)
        tasks = [(self.name, test_name) for test_name in sorted(test_set)]
        self._run_in_message_pool(worker_factory, tasks)
        if options.check:
            if self._successful:
                _log.info('All baselines are optimal.')
            else:
                _log.warning('Some baselines require further optimization.')
                _log.warning('Rerun `optimize-baselines` without `--check` '
                             'to fix these issues.')
                return 2

    def _get_test_set(self, options, args):
        if options.all_tests:
            test_set = set(self._host_port.tests())
        else:
            test_set = resolve_test_patterns(self._host_port, args)
        # Coerce virtual tests to their respective base tests so that the base
        # test's entire baseline graph is optimized as one task. Otherwise, two
        # virtual tests `virtual/a/t.html` and `virtual/b/t.html` can be
        # scheduled as separate tasks on different workers. Those workers could
        # then race to delete the same nonvirtual (but possibly
        # platform-specific) `t-expected.txt`.
        return {
            self._host_port.lookup_virtual_test_base(test) or test
            for test in test_set
        }

    def handle(self, name: str, source: str, successful: bool):
        self._successful = self._successful and successful


class Worker:
    def __init__(self, connection, port_names, options):
        self._connection = connection
        self._options = options
        self._port_names = port_names

    def start(self):
        # Workers should never update the manifest, as this could cause a race.
        # The manifest should already be updated by `optimize-baselines` or
        # `rebaseline-cl`.
        self._options.manifest_update = False
        self._default_port = self._connection.host.port_factory.get(
            options=self._options)
        self._optimizer = BaselineOptimizer(self._connection.host,
                                            self._default_port,
                                            self._port_names,
                                            check=self._options.check)

    def handle(self, name: str, source: str, test_name: str):
        suffixes = sorted(
            set(self._options.suffixes)
            & self._default_port.allowed_suffixes(test_name))
        successful = all(
            self._optimizer.optimize(test_name, suffix) for suffix in suffixes)
        if self._options.check and not self._options.verbose and successful:
            # Without `--verbose`, do not show optimization logs when a test
            # passes the check.
            self._connection.log_messages.clear()
        else:
            self._connection.post(name, successful)
