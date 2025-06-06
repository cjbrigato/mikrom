#!/usr/bin/env python3
# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json


def do_latest():
  print('2025.01.10')  # Update to current date when updating the URLs below


def get_download_url():
  filenames = [
      "android-cts-8.0_R26-linux_x86-arm.zip",
      "android-cts-8.0_R26-linux_x86-x86.zip",
      "android-cts-9.0_r20-linux_x86-arm.zip",
      "android-cts-9.0_r20-linux_x86-x86.zip",
      "android-cts-10_r16-linux_x86-arm.zip",
      "android-cts-10_r16-linux_x86-x86.zip",
      "android-cts-11_r16-linux_x86-arm.zip",
      "android-cts-11_r16-linux_x86-x86.zip",
      "android-cts-12_r14-linux_x86-arm.zip",
      "android-cts-12_r14-linux_x86-x86.zip",
      "android-cts-13_r10-linux_x86-arm.zip",
      "android-cts-13_r10-linux_x86-x86.zip",
      "android-cts-14_r6-linux_x86-arm.zip",
      "android-cts-14_r6-linux_x86-x86.zip",
      "android-cts-15_r2-linux_x86-arm.zip",
      "android-cts-15_r2-linux_x86-x86.zip",
  ]
  url_prefix = "https://dl.google.com/dl/android/cts/"
  urls = [url_prefix + f for f in filenames]

  partial_manifest = {
      'url': urls,
      'name': filenames,
      'ext': '.zip',
  }
  print(json.dumps(partial_manifest))


def main():
  ap = argparse.ArgumentParser()
  sub = ap.add_subparsers()

  latest = sub.add_parser("latest")
  latest.set_defaults(func=lambda _opts: do_latest())

  download = sub.add_parser("get_url")
  download.set_defaults(func=lambda _opts: get_download_url())

  opts = ap.parse_args()
  opts.func(opts)


if __name__ == '__main__':
  main()
