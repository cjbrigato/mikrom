#!/bin/bash
# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

if [ $(basename ${PWD}) != "src" ]; then
  echo "Please set the current working directory to chromium/src first!"
  exit 1
fi

files=(
  "LICENSE"
  "tensorflow_text/core/kernels/darts_clone_trie_wrapper.h"
  "tensorflow_text/core/kernels/disjoint_set_forest.h"
  "tensorflow_text/core/kernels/fast_bert_normalizer.h"
  "tensorflow_text/core/kernels/fast_bert_normalizer_kernel_template.h"
  "tensorflow_text/core/kernels/fast_bert_normalizer_model.fbs"
  "tensorflow_text/core/kernels/fast_bert_normalizer_tflite.h"
  "tensorflow_text/core/kernels/fast_wordpiece_tokenizer.cc"
  "tensorflow_text/core/kernels/fast_wordpiece_tokenizer.h"
  "tensorflow_text/core/kernels/fast_wordpiece_tokenizer_kernel_template.h"
  "tensorflow_text/core/kernels/fast_wordpiece_tokenizer_model.fbs"
  "tensorflow_text/core/kernels/fast_wordpiece_tokenizer_tflite.h"
  "tensorflow_text/core/kernels/fast_wordpiece_tokenizer_utils.h"
  "tensorflow_text/core/kernels/mst_solver.h"
  "tensorflow_text/core/kernels/regex_split.cc"
  "tensorflow_text/core/kernels/regex_split.h"
  "tensorflow_text/core/kernels/wordpiece_tokenizer.cc"
  "tensorflow_text/core/kernels/wordpiece_tokenizer.h"
)

git clone --depth 1 https://github.com/tensorflow/text /tmp/text
rm -rf third_party/tensorflow-text/src/*
pushd third_party/tensorflow-text/src/

for file in ${files[@]} ; do
  if [ ! -d "$(dirname ${file})" ] ; then
    mkdir -p "$(dirname ${file})"
  fi
  cp "/tmp/text/${file}" "${file}"
done

popd
rm -rf /tmp/text
