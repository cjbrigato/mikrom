// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_TOP_CONTAINER_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_TOP_CONTAINER_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"
#include "ui/views/view_targeter_delegate.h"

class BrowserView;

// Container for the BrowserView's tab strip, toolbar, and sometimes bookmark
// bar. In Chrome OS immersive fullscreen it stacks on top of other views in
// order to slide in and out over the web contents.
class TopContainerView : public views::View,
                         public views::ViewTargeterDelegate {
  METADATA_HEADER(TopContainerView, views::View)

 public:
  explicit TopContainerView(BrowserView* browser_view);
  TopContainerView(const TopContainerView&) = delete;
  TopContainerView& operator=(const TopContainerView&) = delete;
  ~TopContainerView() override;

  void OnImmersiveRevealUpdated();

  void set_web_app_frame_toolbar(views::View* web_app_frame_toolbar) {
    web_app_frame_toolbar_ = web_app_frame_toolbar;
  }

  // views::View overrides:
  void PaintChildren(const views::PaintInfo& paint_info) override;
  void ChildPreferredSizeChanged(views::View* child) override;

 private:
  // ViewTargeterDelegate:
  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override;

  // The parent of this view. Not owned.
  raw_ptr<BrowserView> browser_view_;
  raw_ptr<views::View> web_app_frame_toolbar_ = nullptr;
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_TOP_CONTAINER_VIEW_H_
