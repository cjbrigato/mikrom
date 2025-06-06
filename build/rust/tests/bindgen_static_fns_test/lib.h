// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUILD_RUST_TESTS_BINDGEN_STATIC_FNS_TEST_LIB_H_
#define BUILD_RUST_TESTS_BINDGEN_STATIC_FNS_TEST_LIB_H_

#include <stdint.h>

// The following is equivalent to //base/base_export.h.

#if defined(COMPONENT_BUILD)
#if defined(WIN32)

#if defined(COMPONENT_IMPLEMENTATION)
#define COMPONENT_EXPORT __declspec(dllexport)
#else
#define COMPONENT_EXPORT __declspec(dllimport)
#endif  // defined(COMPONENT_IMPLEMENTATION)

#else  // defined(WIN32)
#define COMPONENT_EXPORT __attribute__((visibility("default")))
#endif

#else  // defined(COMPONENT_BUILD)
#define COMPONENT_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

COMPONENT_EXPORT uint32_t mul_two_numbers(uint32_t a, uint32_t b);

[[maybe_unused]] static inline uint32_t mul_three_numbers(uint32_t a,
                                                          uint32_t b,
                                                          uint32_t c) {
  return mul_two_numbers(mul_two_numbers(a, b), c);
}

#ifdef __cplusplus
}
#endif

#endif  // BUILD_RUST_TESTS_BINDGEN_STATIC_FNS_TEST_LIB_H_
