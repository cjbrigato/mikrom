// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_DISPLAY_MANAGER_DISPLAY_MANAGER_EXPORT_H_
#define UI_DISPLAY_MANAGER_DISPLAY_MANAGER_EXPORT_H_

#if defined(COMPONENT_BUILD)
#if defined(WIN32)

#if defined(DISPLAY_MANAGER_IMPLEMENTATION)
#define DISPLAY_MANAGER_EXPORT __declspec(dllexport)
#else
#define DISPLAY_MANAGER_EXPORT __declspec(dllimport)
#endif

#else  // !defined(WIN32)
#define DISPLAY_MANAGER_EXPORT __attribute__((visibility("default")))
#endif

#else  // !defined(COMPONENT_BUILD)
#define DISPLAY_MANAGER_EXPORT
#endif

#endif  // UI_DISPLAY_MANAGER_DISPLAY_MANAGER_EXPORT_H_
