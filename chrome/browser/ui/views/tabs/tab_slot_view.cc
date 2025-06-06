// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/tab_slot_view.h"

#include "components/tabs/public/split_tab_id.h"
#include "ui/base/metadata/metadata_impl_macros.h"

TabSlotView::TabSlotView() = default;
TabSlotView::~TabSlotView() = default;

gfx::Rect TabSlotView::GetAnchorBoundsInScreen() const {
  gfx::Rect bounds = View::GetAnchorBoundsInScreen();

  // Slightly inset anchor bounds to let bubbles hug the tabs more closely.
  bounds.Inset(gfx::Insets::VH(2, 0));
  return bounds;
}

void TabSlotView::SetGroup(std::optional<tab_groups::TabGroupId> group) {
  group_ = group;
}

void TabSlotView::SetSplit(std::optional<split_tabs::SplitTabId> split) {
  split_ = split;
}

BEGIN_METADATA(TabSlotView)
END_METADATA
