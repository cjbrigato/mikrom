// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_GFX_SKIA_EXPORT_H_
#define UI_GFX_GFX_SKIA_EXPORT_H_

#if defined(COMPONENT_BUILD)
#if defined(WIN32)

#if defined(GFX_SKIA_IMPLEMENTATION)
#define GFX_SKIA_EXPORT __declspec(dllexport)
#else
#define GFX_SKIA_EXPORT __declspec(dllimport)
#endif  // defined(GEOMETRY_SKIA_IMPLEMENTATION)

#else  // defined(WIN32)
#define GFX_SKIA_EXPORT __attribute__((visibility("default")))
#endif

#else  // defined(COMPONENT_BUILD)
#define GFX_SKIA_EXPORT
#endif

#endif  // UI_GFX_GFX_SKIA_EXPORT_H_
