// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/tab_width_constraints.h"

#include "chrome/browser/ui/tabs/tab_style.h"
#include "chrome/browser/ui/tabs/tab_types.h"
#include "chrome/browser/ui/views/tabs/tab_strip_layout.h"
#include "ui/gfx/animation/tween.h"

TabWidthConstraints::TabWidthConstraints(const TabLayoutState& state,
                                         const TabSizeInfo& size_info)
    : state_(state), size_info_(size_info) {}

float TabWidthConstraints::GetMinimumWidth() const {
  const float min_width = state_.active() == TabActive::kActive
                              ? size_info_.min_active_width
                              : size_info_.min_inactive_width;
  return TransformForPinnednessAndOpenness(min_width);
}

float TabWidthConstraints::GetLayoutCrossoverWidth() const {
  return TransformForPinnednessAndOpenness(size_info_.min_active_width);
}

float TabWidthConstraints::GetPreferredWidth() const {
  return TransformForPinnednessAndOpenness(size_info_.standard_width);
}

float TabWidthConstraints::TransformForPinnednessAndOpenness(
    float width) const {
  if (state_.IsClosed()) {
    return TabStyle::Get()->GetTabOverlap();
  } else if (state_.pinned() == TabPinned::kPinned) {
    return size_info_.pinned_tab_width;
  } else {
    return width;
  }
}
