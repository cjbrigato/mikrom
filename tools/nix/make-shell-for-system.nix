# Copyright 2025 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Make a function that can be used as shell.nix, given a system.
# For nix-shell(1), this is builtins.currentSystem (see shell.nix).
# For nix3-develop(1), this is passed in by the flake (see flake.nix).
{ system }:
let
  pkgsForNpins =
    import
      (fetchTarball {
        url = "https://github.com/NixOS/nixpkgs/archive/a793ee3962cf3be3d0e9ed1022147ea9cd34eea9.tar.gz";
        sha256 = "023d699q0s9rzx87x6fp4jpar7pd2y3h4gjrdmhznxbbg76yhp9b";
      })
      {
        # Needed for nix3-develop(1) in pure evaluation mode (no --impure).
        inherit system;
      };
  # Grab the npins default.nix and put it in the store next to sources.json,
  # so the `(builtins.readFile ./sources.json)` in default.nix works correctly.
  npins = pkgsForNpins.runCommand "npins" { } ''
    mkdir -v $out
    cp -v ${npins/sources.json} $out/sources.json
    cp -v ${
      pkgsForNpins.fetchurl {
        url = "https://raw.githubusercontent.com/andir/npins/refs/tags/0.3.0/src/default.nix";
        sha256 = "0gx3wcx6lhgk1hfvrmy6737bwa9bxfym6li6rb1gsxs1jaakj0bs";
      }
    } $out/default.nix
  '';
  pins = import npins;
in
{
  pkgs ? import pins.nixpkgs {
    inherit system;
  },
}:
(pkgs.buildFHSEnv {
  name = "chromium-env";

  targetPkgs =
    pkgs:
    (with pkgs; [
      ## for reclient...

      # E0508 08:28:41.337763 2357393 auth.go:527] Credentials helper warnings
      # ... and errors: E0508 08:28:41.335345 2357400 main.go:64] Failed to
      # ... initialize credentials: failed to retrieve gcloud credentials.
      # ... gcloud not installed: exec: "gcloud": executable file not found in
      # ... $PATH
      google-cloud-sdk

      ## for `autoninja -C out/Debug content_shell`...

      # ERROR at //build/config/linux/pkg_config.gni:104:17: Script returned
      # ... non-zero exit code.
      # FileNotFoundError: [Errno 2] No such file or directory: 'pkg-config'
      pkg-config

      # ../../third_party/llvm-build/Release+Asserts/bin/clang++: error while
      # ... loading shared libraries: libz.so.1: cannot open shared object file:
      # ... No such file or directory
      # ../../third_party/rust-toolchain/bin/rustc: error while loading shared
      # ... libraries: libz.so.1: cannot open shared object file: No such file
      # ... or directory
      zlib

      # FileNotFoundError: [Errno 2] No such file or directory: 'gperf'
      gperf

      # subprocess.CalledProcessError: Command '['/cuffs/build/chromium/src/out/
      # ...Reclient/wayland_scanner', '--version']' returned non-zero exit
      # ... status 127.
      # => /cuffs/build/chromium/src/out/Reclient/wayland_scanner: error while
      # ... loading shared libraries: libexpat.so.1: cannot open shared object
      # ... file: No such file or directory
      expat

      # ./root_store_tool: error while loading shared libraries: <soname>:
      # ... cannot open shared object file: No such file or directory
      glib # libglib-2.0.so.0
      nss # libnss3.so

      # ./transport_security_state_generator: error while loading shared
      # ... libraries: libnspr4.so: cannot open shared object file: No such file
      # ... or directory
      nspr

      # ././v8_context_snapshot_generator: error while loading shared libraries:
      # ... <soname>: cannot open shared object file: No such file or directory
      xorg.libX11 # libX11.so.6
      xorg.libXcomposite # libXcomposite.so.1
      xorg.libXdamage # libXdamage.so.1
      xorg.libXext # libXext.so.6
      xorg.libXfixes # libXfixes.so.3
      xorg.libXrandr # libXrandr.so.2
      xorg.libXtst # libXtst.so.6
      mesa # libgbm.so.1
      libdrm # libdrm.so.2
      alsa-lib # libasound.so.2
      libxkbcommon # libxkbcommon.so.0
      pango # libpango-1.0.so.0
      dbus # libdbus-1.so.3

      ## for `content_shell`...

      # out/Default/content_shell: error while loading shared libraries:
      # ... <soname>: cannot open shared object file: No such file or directory
      at-spi2-atk # libatk-1.0.so.0
      xorg.libxcb # libxcb.so.1
      cairo # libcairo.so.2

      # [788412:788449:0502/062912.187804:FATAL:udev_loader.cc(48)] Check
      # ... failed: false.
      systemd

      ## for `third_party/blink/tools/run_web_tests.py`...

      # FileNotFoundError raised: [Errno 2] No such file or directory:
      # ... 'xdpyinfo'
      xorg.xdpyinfo

      ## for `chrome`...

      # out/Default/chrome: error while loading shared libraries: libcups.so.2:
      # cannot open shared object file: No such file or directory
      cups

      ## for clangd with remote indexing support...
      ## (see <https://github.com/clangd/chrome-remote-index>)
      (stdenv.mkDerivation rec {
        pname = "clangd";
        version = "18.1.3";
        src = pkgs.fetchzip {
          url = "https://github.com/clangd/clangd/releases/download/${version}/${pname}-linux-${version}.zip";
          hash = "sha256-6d1P510uHtXJ8fOyi2OZFyILDS8XgK6vsWFewKFVvq4=";
        };
        # autoPatchelfHook lets you run clangd (and your editor) outside this
        # nix-shell
        nativeBuildInputs = [ autoPatchelfHook ];
        installPhase = ''
          mkdir -p $out
          mv bin $out
          mv lib $out
        '';
      })
    ]);

  # workaround for `nix-shell --run '...'` not working with buildFHSEnv.
  # instead use `NIX_SHELL_RUN='...' nix-shell`.
  runScript = pkgs.writeShellScript "chromium-env-run" (
    ''if [ -n "$''
    + ''
      {NIX_SHELL_RUN:+set}" ]; then
            eval "$NIX_SHELL_RUN"
          else
            exec bash
          fi
    ''
  );
}).env
