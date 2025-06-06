// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_ASH_LOBSTER_LOBSTER_VIEW_H_
#define CHROME_BROWSER_UI_WEBUI_ASH_LOBSTER_LOBSTER_VIEW_H_

#include "chrome/browser/ui/webui/ash/mako/draggable_bubble_dialog_view.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/view.h"

namespace ash {

// A view to contain the Lobster UI.
class LobsterView : public DraggableBubbleDialogView {
  METADATA_HEADER(LobsterView, WebUIBubbleDialogView)

 public:
  LobsterView(WebUIContentsWrapper* contents_wrapper,
              const gfx::Rect& caret_bounds);
  LobsterView(const LobsterView&) = delete;
  LobsterView& operator=(const LobsterView&) = delete;
  ~LobsterView() override;

  // WebUIBubbleDialogView:
  void ResizeDueToAutoResize(content::WebContents* source,
                             const gfx::Size& new_size) override;
  void SetContentsBounds(content::WebContents* source,
                         const gfx::Rect& new_size) override;
  void ShowUI() override;

  // MakoBubbleEventHandler::Delegate
  bool IsDraggingEnabled() override;
  bool IsResizingEnabled() override;

 private:
  // Caches the current caret bounds of the target input field based on the
  // screen coordinates. The bubble view will be positioned around this caret
  // bound.
  gfx::Rect caret_bounds_;
};

}  // namespace ash

#endif  // CHROME_BROWSER_UI_WEBUI_ASH_LOBSTER_LOBSTER_VIEW_H_
