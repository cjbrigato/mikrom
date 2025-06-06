# Caspian

[TOC]

## What is it?

Caspian is the name for the WebAssembly portion of the SuperSize Tiger Viewer.
It also contains a Linux command-line test binary.

## Building the WASM Module

### Install Emscripten

This step needs to be only once. Docs: https://emscripten.org/docs/getting_started/downloads.html

```sh
cd /usr/local/google/code
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install 4.0.9
./emsdk activate 4.0.9
source emsdk_env.sh
```

### Build

```sh
# Omit is_official_build=true if developing locally.
gn gen out/caspian --args='enable_caspian=true is_official_build=true treat_warnings_as_errors=false fatal_linker_warnings=false emscripten_path="/usr/local/google/code/emsdk/upstream/emscripten"'
# Build and copy into static/ directory:
( cd out/caspian; autoninja caspian_web && cp wasm/caspian_web.* ../../tools/binary_size/libsupersize/viewer/static/ )
```

## Building the Linux Test App & Unit Tests

There is a test and a binary that you can run to help with development (and
allows debugging outside of the browser).

```sh
OUT=$out/linux_debug
autoninja -C $OUT caspian_cli caspian_unittests
$OUT/caspian_cli --help
$OUT/caspian_unittests
# To debug a crash:
lldb $OUT/caspian_cli validatediff supersize_diff.sizediff
```

## Debugging WASM

Based on this article: https://developer.chrome.com/blog/wasm-debugging-2020/

The one-time setup steps are:

 * On the same machine as your source code:
   * Install Chrome Canary (`sudo apt-get install google-chrome-unstable`)
   * Open Chrome Canary
 * Install the Chrome extension: https://goo.gle/wasm-debugging-extension
 * Enable DWARF debugging in DevTools' experiments panel
   * Gears button in top right, then "experiments" on left nav, then filter for "dwarf".

The every-time steps are:

 * Start server: `tools/binary_size/libsupersize/viewer/upload_html_viewer.py --local`
 * Open `viewer.html` and load a file.
 * Open DevTools to the "Sources" panel.
 * Look for `tree-worker-wasm.js  > file://`
 * Find the `.cc` files within it and set breakpoints.

## Updating Emscripten Version

1. Run:
   ```
   cd $PATH_TO_EMSDK
   git pull origin main --tags
   ./emsdk install latest
   ./emsdk activate latest
   source ./emsdk_env.sh
   ```
2.  Update this README's Emscripten version above, then follow its steps.

## Code Overview

Caspian is a port of the Python implementation, and tries as much as possible to
follow the same patterns and names as the Python. Keeping them as similar as
possible makes it easier to keep them in sync.

### Code Style

Follow Chrome's C++ [styleguide] where possible. One notable exception is that
Caspian does not use `//base` due to the current lack of WASM support in it.

[styleguide]: https://chromium.googlesource.com/chromium/src/+/main/styleguide/c++/c++.md
