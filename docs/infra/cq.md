# CQ

This document describes how the Chromium Commit Queue (CQ) is structured and
managed. This is specific for the Chromium CQ. Questions about other CQs should
be directed to infra-dev@chromium.org. If you find terms you're not familiar
with in this doc, consult the infra [glossary](glossary.md).


[TOC]

## Purpose

The Chromium CQ exists to test developer changes before they land into
[chromium/src](https://chromium.googlesource.com/chromium/src/). It runs a
curated set of test suites across a curated set of platforms for a given CL,
and ensures the CL doesn't introduce any new regressions.

## Modes

The CQ supports a few different modes of execution. Each mode differs in what
tests it runs and/or what happens when all those tests pass. These modes are
described below.

### Dry-Run

This runs all the normal set of tests for a CL. When the dry-run has complete,
it will simply report the results of the CQ attempt as a Gerrit comment on the
CL and take no further action. This mode is intended to be used frequently as a
CL is developed. Can be triggered via either the `CQ DRY RUN` button in Gerrit
or by applying the `Commit-Queue +1` label vote. See
[below](#what-exactly-does-the-cq-run) for the anatomy of a build on the CQ.

### Full-Run

Runs all the same tests as dry-run. If there are no new regressions introduced,
the CL will be submitted into the repo. This mode should only be used when a CL
is finalized. Can be triggered via either the `SUBMIT TO CQ` button in Gerrit or
by applying the `Commit-Queue +2` label vote.

### Mega-CQ Dry-Run

This runs a much larger set of tests compared to the previous two CQ modes.
Those run a limited & curated set of tests that optimizes for quick turn-around
time while still catching most _but not all_ regressions. The Mega CQ, on the
other hand, aims to catch nearly _all_ regressions regardless of cycle time.
Consequently, the Mega CQ takes much longer, and should only be used for
particularly risky CLs. Triggered via the `Mega CQ: Dry Run` button under the
three-dot menu in Gerrit.

For a peak under the hood, the Mega CQ determines its test coverage by including
in it the trybot mirrors of all CI builders gardened by the main Chromium
gardening rotations. It also unconditionally applies
`Include-Ci-Only-Tests: true` to its builds (see [below](#options)).

If you find that the Mega CQ isn't covering a build or test config that it
should, please file a general [trooper bug](https://g.co/bugatrooper) for the
missing coverage.

### Mega-CQ Full-Run

Runs all the same tests as the Mega-CQ dry-run. Will submit the CL if
everything passes. Triggered via the `Mega CQ: Submit` button under the
three-dot menu in Gerrit. The amount of builds and tests the Mega CQ runs makes
a passing run much more unlikely than a normal CQ run. Consequently, when
running the Mega CQ, you'll likely want to spot-check the failures listed on
Gerrit for anything that looks particularly relevant to your CL. Then you can use
the normal `SUBMIT TO CQ` button to land once all failures look unrelated.

## Options

The Chromium CQ supports a variety of options that can change what it checks.

> These options are supported via git footers. They must appear in the last
> paragraph of your commit message to be used. See `git help footers` or
> [git_footers.py][1] for more information.

* `Binary-Size: <rationale>`

  This should be used when you are landing a change that will intentionally
  increase the size of the Chrome binaries on Android (since we try not to
  accidentally do so). The rationale should explain why this is okay to do.

* `Commit: false`

  You can mark a CL with this if you are working on experimental code and do not
  want to risk accidentally submitting it via the CQ. The CQ will immediately
  stop processing the change if it contains this option.

* `Cq-Include-Trybots: <trybots>`

  This flag allows you to specify some additional bots to run for this CL, in
  addition to the default bots. The format for the list of trybots is
  "bucket:trybot1,trybot2;bucket2:trybot3".

* `Disable-Retries: true`

  The CQ will normally try to retry failed test shards (up to a point) to work
  around any intermittent infra failures. If this footer is set, it won't try
  to retry failed shards no matter what happens.

* `Ignore-Freeze: true`

  Whenever there is an active prod freeze (usually around Christmas), it can be
  bypassed by setting this footer.

* `Include-Ci-Only-Tests: true` or
  `Include-Ci-Only-Tests: <comma-separated-builder-ids>|<comma-separated-tests>`

  Some builder configurations may run configure to run some tests only after
  submission (on CI) and not before submission (in the CQ) by default. Possible
  reasons this might be that the tests are too slow or too expensive or there is
  insufficient capacity to run the tests for every CL. In order to still be able
  to explicitly reproduce what the CI builder is doing, you can specify this
  footer to run those tests before submission anyway. Specifying true will run
  all such tests on any triggered try builders. Specifying builder IDs and tests
  will run only the named tests defined for the identified CI builders on the
  try builders that mirror those CI builders. A * can be used in place of a
  builder ID or test name to match any builder/test.

  Constructing a footer value manually should generally be unnecessary: tests
  configured to run only on CI will have the necessary footer included in their
  step text and in the build summary when they fail.

  If it is necessary to construct a footer manually, the builder IDs have the
  format of _builder-group_:_builder-name_. _builder-name_ is the name of the CI
  builder that the test is configured for. _builder-group_ is the "builder
  group" of that builder, which is a concept specific to chromium infra. The
  builder group of a builder can be found by going to a build of the builder,
  and finding the `builder_group` property in the `Input Properties` section of
  the `Infra` tab.

* `No-Presubmit: true`

  If you want to skip the presubmit check, you can add this line, and the commit
  queue won't run the presubmit for your change. This should only be used when
  there's a bug in the PRESUBMIT scripts. Please check that there's a bug filed
  against the bad script, and if there isn't, [file one](https://crbug.com/new).

* `No-Tree-Checks: true`

  Add this line if you want to skip the tree status checks. This means the CQ
  will commit a CL even if the tree is closed. Obviously this is strongly
  discouraged, since the tree is usually closed for a reason. However, in rare
  cases this is acceptable, primarily to fix build breakages (i.e., your CL will
  help in reopening the tree).

* `No-Try: true`

  This should only be used for reverts to green the tree, since it skips try
  bots and might therefore break the tree. You shouldn't use this otherwise.

* `Validate-Test-Flakiness: skip`

  This will disable the `test new tests for flakiness.*` steps in CQ builds that
  check new tests for flakiness.

* `Skip-Clang-Tidy-Checks: <check_1>,<check_2>,...`

  This will skip the specified clang-tidy checks. The checks can be specified
  as check name (e.g. `modernize-use-equals-default`) or glob to skip a set of
  checks (e.g. `modernize-*` to skip checks that advocate usage of modern
  language constructs). This option can span across multiple lines, for example:
  ```
  Skip-Clang-Tidy-Checks: google-explicit-constructor
  Skip-Clang-Tidy-Checks: modernize-*,readability-*
  ```

## FAQ

### What exactly does the CQ run?

CQ runs the jobs specified in [commit-queue.cfg][2]. See
[`cq-builders.md`](../../infra/config/generated/cq-builders.md)
for an auto generated file with links to information about the builders on the
CQ.

Some of these jobs are experimental. This means they are executed on a
percentage of CQ builds, and the outcome of the build doesn't affect if the CL
can land or not. See the schema linked at the top of the file for more
information on what the fields in the config do.

The CQ has the following structure:

* Compile all test suites that might be affected by the CL.
* Runs all test suites that might be affected by the CL.
    * Many test suites are divided into shards. Each shard is run as a separate
      swarming task.
    * These steps are labeled '(with patch)'
* Retry each shard that has a test failure. The retry has the exact same
  configuration as the original run. No recompile is necessary.
    * If the retry succeeds, then the failure is ignored.
    * These steps are labeled '(retry shards with patch)'
    * It's important to retry with the exact same configuration. Attempting to
      retry the failing test in isolation often produces different behavior.
* Recompile each failing test suite without the CL. Rerun each failing test
  suite in isolation.
    * If the retry fails, then the fail is ignored, as it's assumed that the test
      is broken/flaky on tip of tree.
    * These steps are labeled '(without patch)'
* Fail the build if there are tests which failed in both '(with patch)' and
  '(retry shards with patch)' but passed in '(without patch)'.

### Why did my CL fail the CQ?

Please follow these general guidelines:

1. Check to see if your patch caused the build failures, and fix if possible.
1. If compilation or individual tests are failing on one or more CQ bots and you
   suspect that your CL is not responsible, please contact your friendly
   neighborhood gardener by filing a
   [gardener bug](https://bugs.chromium.org/p/chromium/issues/entry?template=Defect%20report%20from%20developer&labels=Gardener-Chromium&summary=%5BBrief%20description%20of%20problem%5D&comment=What%27s%20wrong?).
   If the code in question has appropriate OWNERS, consider contacting or CCing
   them.
1. If other parts of CQ bot execution (e.g. `bot_update`) are failing, or you
   have reason to believe the CQ itself is broken, or you can't really
   tell what's wrong, please file a [trooper bug](https://g.co/bugatrooper).

In both cases, when filing bugs, please include links to the build and/or CL
(including relevant patchset information) in question.

### How do I stop the CQ?

There are a few ways to do this. Here are 3:

1. Change the Commit-Queue value from +1 to 0 in Gerrit UI.
2. Upload a new patchset which triggers a new dry run (Ex: git cl upload -d).
3. Code-Review -1. This prevents a CL from landing.

### How do I add a new builder to the CQ?

There are several requirements for a builder to be added to the Commit Queue.

* There must be a "mirrored" (aka matching) CI builder that is gardened, to
  ensure that someone is actively keeping the configuration green.
* All the code for this configuration must be in Chromium's public repository or
  brought in through [src/DEPS](../../DEPS).
* Setting up the build should be straightforward for a Chromium developer
  familiar with existing configurations.
* Tests should use existing test harnesses i.e.
  [gtest](../../third_party/googletest).
* It should be possible for any committer to replicate any testing run, i.e.
  tests and their data must be in the public repository.
* Median cycle time needs to be under 40 minutes for trybots. 90th percentile
  should be around an hour (preferably shorter).
* Configurations need to catch enough failures to be worth adding to the CQ.
  Running builds on every CL requires a significant amount of compute resources.
  If a configuration only fails once every couple of weeks on the waterfalls,
  then it's probably not worth adding it to the commit queue.

Please email estaab@chromium.org, who will approve new build configurations.

### How do I ensure a trybot runs on all changes to a specific directory?

Several builders are included in the CQ only for changes that affect specific
directories. These used to be configured via Cq-Include-Trybots footers
injected at CL upload time. They are now configured via the `location_regexp`
attribute of the tryjob parameter to the try builder's definition e.g.

```
  try_.some_builder_function(
      name = "my-specific-try-builder",
      tryjob = try_.job(
          location_regexp = [
              ".+/{+]/path/to/my/specific/directory/.+"
          ],
      ),
  )
```

## Flakiness

The CQ can sometimes be flaky. Flakiness is when a test on the CQ fails, but
should have passed (commonly known as a false negative). There are a few common
causes of flaky tests on the CQ:

* Machine issues; weird system processes running, running out of disk space,
  etc...
* Test issues; individual tests not being independent and relying on the order
  of tests being run, not mocking out network traffic or other real world
  interactions.

The CQ mitigates flakiness by retrying failed tests. The core tradeoff in retry
policy is that adding retries increases the probability that a flaky test will
land on tip of tree sublinearly, but mitigates the impact of the flaky test on
unrelated CLs exponentially.

For example, imagine a CL that adds a test that fails with 50% probability. Even
with no retries, the test will land with 50% probability. Subsequently, 50% of
all unrelated CQ attempts would flakily fail. This effect is cumulative across
different flaky tests. Since the CQ has roughly ~20,000 unique flaky tests,
without retries, pretty much no CL would ever pass the CQ.

## Help!

Have other questions? Run into any issues with the CQ? Email
infra-dev@chromium.org, or file a [trooper bug](https://g.co/bugatrooper).


[1]: https://chromium.googlesource.com/chromium/tools/depot_tools/+/HEAD/git_footers.py
[2]: ../../infra/config/generated/luci/commit-queue.cfg
