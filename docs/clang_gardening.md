# Clang Gardening

Chromium bundles its own pre-built version of [Clang](clang.md) and
[Rust](rust.md). This is done so that Chromium developers have access to the
latest and greatest developer tools provided by Clang and LLVM (ASan, CFI,
coverage, etc). In order to [update the compiler](updating_clang.md)
(roll clang), it has to be tested so that we can be confident that it works
in the configurations that Chromium cares about.

The Clang gardener is responsible for monitoring the health of the latest
versions of Clang + Rust, and how they work with the latest version of
Chromium; raise any issues by filing bugs; address those issues or find someone
to do so; and ultimately attempt to update the compiler by performing [a Clang
roll](updating_clang.md).

There are two main sources of information about the state of the build:

1. Buildbots on the [tip-of-tree clang
   waterfall](https://ci.chromium.org/p/chromium/g/chromium.clang/console)
   continuously build the latest version of Clang and use that to build and
   test Chromium in various build configurations. These provide the fastest
   signal about problems such as the compiler crashing, new warnings causing
   build failures, miscompiles causing test failures, etc. Unlike production
   buildbots, these build Clang with assertions enabled to detect as many
   problems as possible. (Clicking 'Log in' in the top right corner with a
   Google account will reveal a few more bots.)

1. Automatically generated [Clang roll
   CLs](https://chromium-review.googlesource.com/q/path:tools/clang/scripts/update.py)
   ("dry run CLs"). These are generated every few hours by a Cron job and
   attempt to package the latest version of Clang and Rust. That process can
   fail for many reasons, especially due to failures in the compilers' test
   suites. If the CLs stop being generated, that also needs to be addressed.

Although both of these pull & build the latest version of clang, things may go
wrong in one place but not the other, so it's important to keep an eye on both.
In particular, the ToT buildbots don't run the entire clang test suite, and the
dry run CLs don't build chromium, just clang and rust.

Issues should be filed in the [Chromium > Tools >
LLVM](https://g-issues.chromium.org/issues?q=status:open%20componentid:1457173)
bug tracker component, and marked as blockers of the tracking bug for the next
toolchain update. That bug should be named "roll clang and rust again", so that
it can easily be found by a [search in the LLVM component](https://g-issues.chromium.org/issues?q=componentid:1457173%20%22roll%20clang%20and%20rust%20again%22).

The roll bug should be filed as a P1 Process bug. Anything that blocks the roll
should should be filed and treated as P1 bugs. Non-blocking issues should be
logged as a child issue of our [long-term tracking bug](https://g-issues.chromium.org/issues/417753763).

Here is a suggested set of steps to iterate over while gardening:

* If there is no bug for tracking the next toolchain update, file one.

* Go over the blockers of the toolchain update tracking bug. Close obsolete
  ones, try to fix or find someone to fix the remaining ones.

* Check the [tip-of-tree clang
  waterfall](https://ci.chromium.org/p/chromium/g/chromium.clang/console) and
  file bugs for any issues.

* Check the automatic [Clang roll
  CLs](https://chromium-review.googlesource.com/q/path:tools/clang/scripts/update.py).
  File a bug for any packaging issues. File a bug if the CLs stop being produced.

* When packaging succeeds on a roll CL, and the ToT waterfall is reasonably
  green, attempt a clang roll by following the instructions in [update the
  compiler](updating_clang.md). This will push the packages from the roll CL to
  production and begin a commit queue dry run. File a bug for any issues that
  come up.

* If the commit queue dry run was successful, review and land the CL.

The key to success is to detect as many problems as early as possible. Rather
than stopping to dig deeply into the first problem encountered, it's better to
do a broad sweep to find all the problems. That way they can be shared among
the team. Also, problems are often much easier to address when found early.

The other key to success is communication. The bug tracker is the main tool for
that, and the other is the team chat room. When it's not clear whether
something is an issue or not, how to resolve an problem, how something works,
etc., just ask away.

The gardener is also responsible for taking notes during the weekly Chrome
toolchain sync meeting.

[TOC]

## Clang packaging test failures

When packaging clang/LLVM on our various supported platforms (`upload_*_clang`
tryjobs), we run the entire LLVM test suite and block the build if any
test failed. The most common test failures we see are Mac and Windows-specific
tests since upstream LLVM is mostly Linux-focused. There are public bots that
also run LLVM tests, mostly accessible from https://lab.llvm.org/buildbot.
There are also some Apple bots running at
http://green.lab.llvm.org/job/llvm.org/ which mirror test failures we see on
Mac. Reverting the culprit change upstream with a pointer to a public bot
showing the test failure is encouraged.

See LLVM's [Patch reversion
policy](https://llvm.org/docs/DeveloperPolicy.html#patch-reversion-policy).

## Disk out of space

If there are any issues with disk running out of space, file a go/bug-a-trooper
bug, for example https://crbug.com/1105134.

## Is it the compiler?

Chromium does not always build and pass tests in all configurations that
everyone cares about. Some configurations simply take too long to build
(ThinLTO) or be tested (dbg) on the CQ before committing. And, some tests are
flaky. So, our console is often filled with red boxes, and the boxes don't
always need to be green to roll clang.

Oftentimes, if a bot is red with a test failure, it's not a bug in the compiler.
To check this, the easiest and best thing to do is to try to find a
corresponding builder that doesn't use ToT clang. For standard configurations,
start on the waterfall that corresponds to the OS of the red bot, and search
from there. If the failing bot is Google Chrome branded, go to the (Google
internal) [official builder
list](https://uberchromegw.corp.google.com/i/official.desktop.continuous/builders/)
and start searching from there.

If you are feeling charitable, you can try to see when the test failure was
introduced by looking at the history in the bot. One way to do this is to add
`?numbuilds=200` to the builder URL to see more history. If that isn't enough
history, you can manually binary search build numbers by editing the URL until
you find where the regression was introduced. If it's immediately clear what CL
introduced the regression (i.e.  caused tests to fail reliably in the official
build configuration), you can simply load the change in gerrit and revert it,
linking to the first failing build that implicates the change being reverted.

If the failure looks like a compiler bug, these are the common failures we see
and what to do about them:

1. compiler crash
1. compiler warning change
1. compiler error
1. miscompile
1. linker errors

## Compiler crash

This is probably the most common bug. The standard procedure is to do these
things:

1. Open the `gclient runhooks` stdout log from the first red build.  Near the
   top of that log you can find the range of upstream llvm revisions.  For
   example:

       From https://github.com/llvm/llvm-project
           f917356f9ce..292e898c16d  master     -> origin/master

1. File a crbug documenting the crash. Include the range, and any other bots
   displaying the same symptoms.
1. All clang crashes on the Chromium bots are automatically uploaded to
   Cloud Storage. On the failing build, click the "stdout" link of the
   "process clang crashes" step right after the red compile step. It will
   print something like

       processing heap_page-65b34d... compressing... uploading... done
           gs://chrome-clang-crash-reports/v1/2019/08/27/chromium.clang-ToTMac-20955-heap_page-65b34d.tgz
       removing heap_page-65b34d.sh
       removing heap_page-65b34d.cpp

   Use
   `gsutil.py cp gs://chrome-clang-crash-reports/v1/2019/08/27/chromium.clang-ToTMac-20955-heap_page-65b34d.tgz .`
   to copy it to your local machine. Untar with
   `tar xzf chromium.clang-ToTMac-20955-heap_page-65b34d.tgz` and change the
   included shell script to point to a locally-built clang. Remove the
   `-Xclang -plugin` flags.  If you re-run the shell script, it should
   reproduce the crash.
1. Identify the revision that introduced the crash. First, look at the commit
   messages in the LLVM revision range to see if one modifies the code near the
   point of the crash. If so, try reverting it locally, rebuild, and run the
   reproducer to see if the crash goes away.

   If that doesn't work, use `git bisect`. Use this as a template for the bisect
   run script:
   ```shell
   #!/bin/bash
   cd $(dirname $0)  # get into llvm build dir
   ninja -j900 clang || exit 125 # skip revisions that don't compile
   ./t-8f292b.sh || exit 1  # exit 0 if good, 1 if bad
   ```
1. File an upstream bug like http://llvm.org/PR43016. Usually the unminimized repro
   is too large for LLVM's bugzilla, so attach it to a (public) crbug and link
   to that from the LLVM bug. Then revert with a commit message like
   "Revert r368987, it caused PR43016."
1. If you want, make a reduced repro using CReduce.  Clang contains a handy wrapper around
   CReduce that you can invoke like so:

       clang/utils/creduce-clang-crash.py --llvm-bin bin \
           angle_deqp_gtest-d421b0.sh angle_deqp_gtest-d421b0.cpp

   Attach the reproducer to the llvm bug you filed in the previous step. You can
   disable Creduce's renaming passes with the options
   `--remove-pass pass_clang rename-fun --remove-pass pass_clang rename-param
   --remove-pass pass_clang rename-var --remove-pass pass_clang rename-class
   --remove-pass pass_clang rename-cxx-method --remove-pass pass_clex
   rename-toks` which makes it easier for the author to reason about and to
   further reduce it manually.

   If you need to do something the wrapper doesn't support,
   follow the [official CReduce docs](https://embed.cs.utah.edu/creduce/using/)
   for writing an interestingness test and use creduce directly.

## Compiler warning change

New Clang versions often find new bad code patterns to warn on. Chromium builds
with `-Werror`, so improvements to warnings often turn into build failures in
Chromium. Once you understand the code pattern Clang is complaining about, file
a bug to do either fix or silence the new warning.

If this is a completely new warning, disable it by adding `-Wno-NEW-WARNING` to
the "tot_warnings" config. Here is [an example](https://source.chromium.org/chromium/chromium/src/+/main:build/config/compiler/BUILD.gn;l=1959;drc=b3280fe4347b9093c0e4ef2ff592ee0353037fe7).
This will keep the ToT bots green while you decide what to do. You may need to
add additional checks if the warning is only needed for some configurations, but
you will always need to check `llvm_force_head_revision` to ensure you don't
pass the warning flag to older versions of clang that don't recognize it.

Sometimes, behavior changes and a pre-existing warning changes to warn on new
code. In this case, fixing Chromium may be the easiest and quickest fix. If
there are many sites, you may consider changing clang to put the new diagnostic
into a new warning group so you can handle it as a new warning as described
above.

If the warning is high value, then eventually our team or other contributors
will end up fixing the crbug and there is nothing more to do.  If the warning
seems low value, pass that feedback along to the author of the new warning
upstream. It's unlikely that it should be on by default or enabled by `-Wall` if
users don't find it valuable. If the warning is particularly noisy and can't be
easily disabled without disabling other high value warnings, you should consider
reverting the change upstream and asking for more discussion.

## Compiler error

This rarely happens, but sometimes clang becomes more strict and no longer
accepts code that it previously did. The standard procedure for a new warning
may apply, but it's more likely that the upstream Clang change should be
reverted, if the C++ code in question in Chromium looks valid.

## Miscompile

Miscompiles tend to result in crashes, so if you see a test with the CRASHED
status, this is probably what you want to do.

1. Bisect object files to find the object with the code that changed. LLVM
   contains `llvm/utils/rsp_bisect.py` which may be useful for bisecting object
   files using an rsp file.
1. Debug it with a traditional debugger

## Linker error

`ld.lld`'s `--reproduce` flag makes LLD write a tar archive of all its inputs
and a file `response.txt` that contains the link command. This allows people to
work on linker bugs without having to have a Chromium build environment.

To use `ld.lld`'s `--reproduce` flag, follow these steps:

1. Locally (build Chromium with a locally-built
   clang)[https://chromium.googlesource.com/chromium/src.git/+/main/docs/clang.md#Using-a-custom-clang-binary]

1. After reproducing the link error, build just the failing target with
   ninja's `-v -d keeprsp` flags added:
  `ninja -C out/gn base_unittests -v -d keeprsp`.

1. Copy the link command that ninja prints, `cd out/gn`, paste it, and manually
   append `-Wl,--reproduce,repro.tar`. With `lld-link`, instead append
   `/reproduce:repro.tar`. (`ld.lld` is invoked through the `clang` driver, so
   it needs `-Wl` to pass the flag through to the linker. `lld-link` is called
   directly, so the flag needs no prefix.)

1. Zip up the tar file: `gzip repro.tar`. This will take a few minutes and
   produce a .tar.gz file that's 0.5-1 GB.

1. Upload the .tar.gz to Google Drive. If you're signed in with your @google
   address, you won't be able to make a world-shareable link to it, so upload
   it in a Window where you're signed in with your @chromium account.

1. File an LLVM bug linking to the file. Example: http://llvm.org/PR43241

TODO: Describe object file bisection, identify obj with symbol that no longer
has the section.

## ThinLTO Trouble

Sometimes, problems occur in ThinLTO builds that do not occur in non-LTO builds.
These steps can be used to debug such problems.

Notes:

 - All steps assume they are run from the output directory (the same directory args.gn is in).

 - Commands have been shortened for clarity. In particular, Chromium build commands are
   generally long, with many parts that you just copy-paste when debugging. These have
   largely been omitted.

 - The commands below use "clang++", where in practice there would be some path prefix
   in front of this. Make sure you are invoking the right clang++. In particular, there
   may be one in the PATH which behaves very differently.

### Get the full command that is used for linking

To get the command that is used to link base_unittests:

```sh
$ rm base_unittests
$ ninja -n -d keeprsp -v base_unittests
```

This will print a command line. It will also write a file called `base_unittests.rsp`, which
contains additional parameters to be passed.

### Remove ThinLTO cache flags

ThinLTO uses a cache to avoid compilation in some cases. This can be confusing
when debugging, so make sure to remove the various cache flags like
`-Wl,--thinlto-cache-dir`.

### Expand Thin Archives on Command Line

Expand thin archives mentioned in the command line to their individual object files.
The script `tools/clang/scripts/expand_thin_archives.py` can be used for this purpose.
For example:

```sh
$ ../../tools/clang/scripts/expand_thin_archives.py -p=-Wl, -- @base_unittests.rsp > base_unittests.expanded.rsp
```

The `-p` parameter here specifies the prefix for parameters to be passed to the linker.
If you are invoking the linker directly (as opposed to through clang++), the prefix should
be empty.

```sh
$ ../../tools/clang/scripts/expand_thin_archives.py -p='', -- @base_unittests.rsp > base_unittests.expanded.rsp
```

### Remove -Wl,--start-group and -Wl,--end-group

Edit the link command to use the expanded command line, and remove any mention of `-Wl,--start-group`
and `-Wl,--end-group` that surround the expanded command line. For example, if the original command was:

    clang++ -fuse-ld=lld -o ./base_unittests -Wl,--start-group @base_unittests.rsp -Wl,--end-group

the new command should be:

    clang++ -fuse-ld=lld -o ./base_unittests @base_unittests.expanded.rsp

The reason for this is that the `-start-lib` and `-end-lib` flags that expanding the command
line produces cannot be nested inside `--start-group` and `--end-group`.

### Producing ThinLTO Bitcode Files

In a ThinLTO build, what is normally the compile step that produces native object files
instead produces LLVM bitcode files. A simple example would be:

```sh
$ clang++ -c -flto=thin foo.cpp -o foo.o
```

In a Chromium build, these files reside under `obj/`, and you can generate them using ninja.
For example:

```sh
$ ninja obj/base/base/lock.o
```

These can be fed to `llvm-dis` to produce textual LLVM IR:
   
```
$ llvm-dis -o - obj/base/base/lock.o | less
```

When using split LTO unit (`-fsplit-lto-unit`, which is required for
some features, CFI among them), this may produce a message like:

    llvm-dis: error: Expected a single module

   In that case, you can use `llvm-modextract`:
   
```sh
$ llvm-modextract -n 0 -o - obj/base/base/lock.o | llvm-dis -o - | less
```

### Saving Intermediate Bitcode

The ThinLTO linking process proceeds in a number of stages. The bitcode that is
generated during these stages can be saved by passing `-save-temps` to the linker:

```
$ clang++ -fuse-ld=lld -Wl,-save-temps -o ./base_unittests @base_unittests.expanded.rsp
```

This generates files such as:
 - lock.o.0.preopt.bc
 - lock.o.3.import.bc
 - lock.o.5.precodegen.bc

in the directory where lock.o is (obj/base/base).

These can be fed to `llvm-dis` to produce textual LLVM IR. They show
how the code is transformed as it progresses through ThinLTO stages.
Of particular interest are:
 - .3.import.bc, which shows the IR after definitions have been imported from
   other modules, but before optimizations. Running this through LLVM's `opt`
   tool with the right optimization level can often reproduce issues.
 - .5.precodegen.bc, which shows the IR just before it is transformed to native
   code. Running this through LLVM's `llc` tool with the right optimization level
   can often reproduce issues.

The same `-save-temps` command also produces `base_unittests.resolution.txt`, which
shows symbol resolutions. These look like:

    -r=obj/base/test/run_all_base_unittests/run_all_base_unittests.o,main,plx

In this example, run_all_base_unittests.o contains a symbol named
main, with flags plx.
   
The possible flags are:
 - p: prevailing: of symbols with this name, this one has been chosen.
 - l: final definition in this linkage unit.
 - r: redefined by the linker.
 - x: visible to regular (that is, non-LTO) objects.

### Code Generation for a Single Module

To speed up debugging, it may be helpful to limit code generation to a single
module if you know the name of the module (e.g. the module name is in a crash
dump).

`-Wl,--thinlto-single-module=foo` tells ThinLTO to only run
optimizations/codegen on files matching the pattern and skip linking. This is
helpful especially in combination with `-Wl,-save-temps`.

```sh
$ clang++ -fuse-ld=lld -Wl,--thinlto-single-module=obj/base/base/lock.o -o ./base_unittests @base_unittests.expanded.rsp
```

You should see

```sh
[ThinLTO] Selecting obj/base/base/lock.o to compile
```

being printed.

## Tips and tricks

Finding what object files differ between two directories:

```
$ diff -u <(cd out.good && find . -name "*.o" -exec sha1sum {} \; | sort -k2) \
          <(cd out.bad  && find . -name "*.o" -exec sha1sum {} \; | sort -k2)
```

Or with cmp:

```
$ find good -name "*.o" -exec bash -c 'cmp -s $0 ${0/good/bad} || echo $0' {} \;
```
