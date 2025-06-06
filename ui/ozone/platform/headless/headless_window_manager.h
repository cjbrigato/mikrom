// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_HEADLESS_HEADLESS_WINDOW_MANAGER_H_
#define UI_OZONE_PLATFORM_HEADLESS_HEADLESS_WINDOW_MANAGER_H_

#include <stdint.h>

#include "base/containers/id_map.h"
#include "base/threading/thread_checker.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/ozone/public/surface_factory_ozone.h"

namespace ui {

class HeadlessWindow;

class HeadlessWindowManager {
 public:
  HeadlessWindowManager();

  HeadlessWindowManager(const HeadlessWindowManager&) = delete;
  HeadlessWindowManager& operator=(const HeadlessWindowManager&) = delete;

  ~HeadlessWindowManager();

  // Register a new window. Returns the window id.
  gfx::AcceleratedWidget AddWindow(HeadlessWindow* window);

  // Remove a window.
  void RemoveWindow(gfx::AcceleratedWidget widget, HeadlessWindow* window);

  // Find a window object by id;
  HeadlessWindow* GetWindow(gfx::AcceleratedWidget widget);

  // Return an accelerated widget at screen point.
  gfx::AcceleratedWidget GetAcceleratedWidgetAtScreenPoint(
      const gfx::Point& point);

 private:
  base::IDMap<HeadlessWindow*> windows_;
  base::ThreadChecker thread_checker_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_HEADLESS_HEADLESS_WINDOW_MANAGER_H_
