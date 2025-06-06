// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_MAC_NSWINDOW_FRAME_CONTROLS_H_
#define UI_GFX_MAC_NSWINDOW_FRAME_CONTROLS_H_

#include "base/component_export.h"

@class NSWindow;

namespace gfx {

class Size;

// Set whether the window can be fullscreened.
COMPONENT_EXPORT(GFX)
void SetNSWindowCanFullscreen(NSWindow* window, bool allow_fullscreen);

// Sets whether the window appears on all workspaces.
COMPONENT_EXPORT(GFX)
void SetNSWindowVisibleOnAllWorkspaces(NSWindow* window, bool always_visible);

// Sets the min/max size of the window as well as showing/hiding resize,
// maximize, and fullscreen controls.
//
// For the maximum size, a height/width specified as 0 means unbounded.
//
// Sizes refer to the content size (inner bounds).
COMPONENT_EXPORT(GFX)
void ApplyNSWindowSizeConstraints(NSWindow* window,
                                  const gfx::Size& min_size,
                                  const gfx::Size& max_size,
                                  bool can_resize,
                                  bool can_fullscreen);

}  // namespace gfx

#endif  // UI_GFX_MAC_NSWINDOW_FRAME_CONTROLS_H_
