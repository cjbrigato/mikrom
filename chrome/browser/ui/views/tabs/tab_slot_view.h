// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_TABS_TAB_SLOT_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_TABS_TAB_SLOT_VIEW_H_

#include <optional>

#include "chrome/browser/ui/views/tabs/tab_strip_layout.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/split_tab_id.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

// View that can be laid out in the tabstrip.
class TabSlotView : public views::View {
  METADATA_HEADER(TabSlotView, views::View)

 public:
  enum class ViewType {
    kTab,
    kTabGroupHeader,
  };

  TabSlotView();
  ~TabSlotView() override;

  // Returns the type of TabSlotView that this is. Used for distinguishing
  // between Tabs and TabGroupHeaders when dragging.
  virtual ViewType GetTabSlotViewType() const = 0;

  // Returns the TabSizeInfo for this View. Used by tab strip layout to size it
  // appropriately.
  virtual TabSizeInfo GetTabSizeInfo() const = 0;

  // Used to set the tab group that this view belongs to.
  virtual void SetGroup(std::optional<tab_groups::TabGroupId> group);

  std::optional<tab_groups::TabGroupId> group() const { return group_; }

  virtual void SetSplit(std::optional<split_tabs::SplitTabId> split);

  std::optional<split_tabs::SplitTabId> split() const { return split_; }

  // Used to mark the view as having been detached.  Once this has happened, the
  // view should be invisibly closed.  This is irreversible.
  void set_detached() { detached_ = true; }
  bool detached() const { return detached_; }

  // Used to mark the view as being dragged.
  void set_dragging(bool dragging) { dragging_ = dragging; }
  bool dragging() const { return dragging_; }

  void set_animating(bool animating) { animating_ = animating; }
  bool animating() const { return animating_; }

  // views::View:
  gfx::Rect GetAnchorBoundsInScreen() const override;

 private:
  std::optional<tab_groups::TabGroupId> group_;

  // True if the view has been detached.
  bool detached_ = false;

  // True if the tab is being dragged.
  bool dragging_ = false;

  // True if the tab's bounds are being animated by the tabstrip.
  bool animating_ = false;

  std::optional<split_tabs::SplitTabId> split_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_TABS_TAB_SLOT_VIEW_H_
