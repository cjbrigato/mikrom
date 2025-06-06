#!/usr/bin/env python3
# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys
import os

_HERE_PATH = os.path.dirname(__file__)
_SRC_PATH = os.path.normpath(os.path.join(_HERE_PATH, '..', '..', '..', '..'))
_CWD = os.getcwd()

sys.path.append(os.path.join(_SRC_PATH, 'third_party', 'node'))
import node


def main(argv):
  parser = argparse.ArgumentParser()
  parser.add_argument('--in_folder', required=True)
  parser.add_argument('--out_folder', required=True)
  parser.add_argument('--in_files', nargs='*', required=True)
  parser.add_argument('--out_manifest', required=True)

  args = parser.parse_args(argv)
  out_path = os.path.normpath(
      os.path.join(_CWD, args.out_folder).replace('\\', '/'))
  in_path = os.path.normpath(
      os.path.join(_CWD, args.in_folder).replace('\\', '/'))

  # Spawn a NodeJS script to use the programmatic Terser API, since the CLI API
  # does not allow compressing multiple files at once. This is done to avoid
  # launching NodeJS once for every input file.
  node.RunNode([
      os.path.join(_HERE_PATH, 'minify_js.js'), '--in_folder', in_path,
      '--out_folder', out_path
  ] + args.in_files)

  manifest_data = {}
  manifest_data['base_dir'] = args.out_folder
  manifest_data['files'] = args.in_files
  with open(os.path.normpath(os.path.join(_CWD, args.out_manifest)), 'w',
            newline='', encoding='utf-8') \
      as manifest_file:
    json.dump(manifest_data, manifest_file)


if __name__ == '__main__':
  main(sys.argv[1:])
