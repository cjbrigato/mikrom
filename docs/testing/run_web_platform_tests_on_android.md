# Running Web Platform Tests on Android

## Overview

This document provides a guide to running Web Platform Tests on Android.

For general instruction for running the Web Platform Tests, you should read
[Running Web Platform Tests with run_wpt_tests.py](./run_web_platform_tests.md).

[TOC]

## Initial Setup

Please follow the steps at [Checking out and building Chromium for Android
](/docs/android_build_instructions.md) to
setup your local environment. Once that is done, you need to build one of the
following targets:

```bash
autoninja -C out/Default chrome_public_wpt        # For testing with Chrome Android
autoninja -C out/Default trichrome_webview_wpt_64 # For testing with WebView
```

## Running the Tests

Once you have Chrome Android/WebView and `chromedriver` built, you can launch
`run_wpt_tests.py` to run WPTs. You can either run the tests on an Android
emulator or a real Android device.

Note: You can enable [Incremental Install](/build/android/incremental_install/README.md)
for Chrome Android by setting `incremental_install = True` in your `args.gn`
file.

This will cause the test harness to use the build directory's incremental
installation script instead of the default browser APK.

### Running WPTs on an Android emulator

You will need to follow the steps in
[Using an Android Emulator](/docs/android_emulator.md) to be ready to run the
Android Emulator. Passing the `--avd-config` option to `run_wpt_tests.py` will
launch an emulator that will be shut down after running the tests.

The example below runs `external/wpt/badging/badge-success.https.html` in Chrome
Android on Android 13:

```bash
third_party/blink/tools/run_wpt_tests.py \
  -t Default \
  -p clank \
  --avd-config=tools/android/avd/proto/android_33_google_apis_x64.textpb \
  external/wpt/badging/badge-success.https.html
```

* `-t Default`: Use the build in `//out/Default/`
* `-p clank`: Runs the tests using Chrome for Android (clank).
* `--avd-config=tools/.../android_33_google_apis_x64.textpb`: Runs the tests on
Android 13 emulator (Google API 33).

To run the example in WebView:

```bash
$ third_party/blink/tools/run_wpt_tests.py \
  -t Default \
  -p android_webview \
  --webview-provider out/Default/apks/TrichromeWebView64.apk \
  --additional-apk out/Default/apks/TrichromeLibrary64.apk \
  --avd-config=tools/android/avd/proto/android_33_google_apis_x64.textpb \
  external/wpt/badging/badge-success.https.html
```

* `-p webview`: Runs the tests using WebView.
* `--webview-provider out/.../TrichromeWebView64.apk`: Specify
TrichromeWebView64 as WebView APK.
* `--additional-apk out/.../TrichromeLibrary64.apk`: Install TrichromeLibrary64
needed for the WebView APK after Android 10 (see [WebView Channels](/android_webview/docs/channels.md)).

Alternatively, you can launch the emulator yourself and `run_wpt_tests.py` will
detect and connect to the emulator and run WPTs with it. This can save you the
time to repeatedly launch the emulator. The commands below show how this works.

```bash
$ tools/android/avd/avd.py start \
  --avd-config=tools/android/avd/proto/android_33_google_apis_x64.textpb
$ third_party/blink/tools/run_wpt_tests.py \
  -t Default \
  -p clank \
  external/wpt/badging/badge-success.https.html
```

### Running WPTs on a real Android device

`run_wpt_tests.py` should be able to work with a real device as long as the
device can be found by `adb devices`. You will need to make sure the ABI matches
and these [steps
](/docs/android_build_instructions.md#installing-and-running-chromium-on-a-device)
are followed.

### Running Tests in CQ/CI

To test Android-specific behaviors, the
`android-chrome-13-x64-wpt-android-specific` and
`android-webview-13-x64-wpt-android-specific` builders run the tests in
[`android.filter`](/third_party/blink/web_tests/TestLists/android.filter) and
[`webview.filter`](/third_party/blink/web_tests/TestLists/webview.filter) respectively. Developers can add additional
tests to the lists when necessary. A resource request might be
needed depending on the case. See [below](#Run-more-tests-with-Chrome-Android-or-WebView)
for details.

To satisfy different testing requirements, WPT coverage in CQ/CI is partitioned
between suites that target different `//content` embedders:

Suite Name | Browser Under Test | Harness | Tests Run
--- | --- | --- | ---
`android_chrome_wpt_tests` | `chrome_android` | `run_wpt_tests.py` | Tests listed in [`android.filter`](#running-tests-in-cqci).
`android_webview_wpt_tests` | `android_webview` | `run_wpt_tests.py` | Tests listed in [`webview.filter`](#running-tests-in-cqci).

## Test expectations and Baselines

Expected failures on Chrome Android or WebView should be added to the default
[TestExpectations](../../third_party/blink/web_tests/TestExpectations) file with the modifier "Android" or "Webview" respectively.

For baselines:
* Chrome Android specific baselines reside at
  `third_party/blink/web_tests/platform/android`, and fall back to
  `third_party/blink/web_tests/platform/linux`.
* WebView specific baselines reside at
  `third_party/blink/web_tests/platform/webview`, and fall back to
  `third_party/blink/web_tests/platform/linux`.

To update baselines:
1. Trigger tryjob(s)
  * For Chrome Androids: trigger `android-chrome-13-x64-wpt-android-specific`
  * For WebView: trigger `android-webview-13-x64-wpt-android-specific`
2. Run [the rebaseline tool](./web_test_expectations.md#How-to-rebaseline) after
  the results are ready.

## Run more tests with Chrome Android or WebView

A resource request would be needed if running the additional tests increases the
total test time by more than one minute. If this is the case, developers can use
[the bot estimator](https://data.corp.google.com/sites/datasite_browser_infra/bot_estimator?p=task_type:%22test%22)
to estimate the bots needed, then follow the steps at [go/i-need-hw](https://g3doc.corp.google.com/company/teams/chrome/ops/business/resources/resource-request-program.md?cl=head&polyglot=physical-hw#i-need-new-resources)
to get the resources. The tests can be added to the filter file after the resource
is deployed.

Please make sure there is no unexpected failures on the related steps before using
the bot estimator, as the bot will retry those tests and that will increase the
total test time.

## Known Issues

For runner bugs and feature requests, please file [an issue against
`Blink>Infra`
](https://issues.chromium.org/issues/new?component=1456928&template=1923166).
