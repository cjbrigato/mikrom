// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shelf/shelf_bubble.h"

#include <memory>

#include "ash/public/cpp/shell_window_ids.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/aura/window.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/bubble/bubble_frame_view.h"

namespace {

class ShelfTooltipBubbleFrameView : public views::BubbleFrameView {
  METADATA_HEADER(ShelfTooltipBubbleFrameView, views::BubbleFrameView)

 public:
  ShelfTooltipBubbleFrameView()
      : BubbleFrameView(gfx::Insets(), gfx::Insets()) {}
  ShelfTooltipBubbleFrameView(const ShelfTooltipBubbleFrameView&) = delete;
  ShelfTooltipBubbleFrameView& operator=(const ShelfTooltipBubbleFrameView&) =
      delete;
  ~ShelfTooltipBubbleFrameView() override = default;

 private:
  // views::BubbleFrameView:
  gfx::Rect GetAvailableScreenBounds(const gfx::Rect& rect) const override {
    return display::Screen::GetScreen()
        ->GetDisplayNearestPoint(rect.CenterPoint())
        .bounds();
  }
};

BEGIN_METADATA(ShelfTooltipBubbleFrameView)
END_METADATA

views::BubbleBorder::Arrow GetArrow(ash::ShelfAlignment alignment) {
  switch (alignment) {
    case ash::ShelfAlignment::kBottom:
    case ash::ShelfAlignment::kBottomLocked:
      return views::BubbleBorder::BOTTOM_CENTER;
    case ash::ShelfAlignment::kLeft:
      return views::BubbleBorder::LEFT_CENTER;
    case ash::ShelfAlignment::kRight:
      return views::BubbleBorder::RIGHT_CENTER;
  }
  return views::BubbleBorder::Arrow::NONE;
}

}  // namespace

namespace ash {

ShelfBubble::ShelfBubble(
    views::View* anchor,
    ShelfAlignment alignment,
    bool for_tooltip,
    std::optional<views::BubbleBorder::Arrow> arrow_position)
    : views::BubbleDialogDelegateView(
          anchor,
          arrow_position.value_or(GetArrow(alignment))),
      for_tooltip_(for_tooltip) {
  SetBackgroundColor(SK_ColorTRANSPARENT);
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));

  // Place the bubble in the same display as the anchor.
  set_parent_window(
      anchor_widget()->GetNativeWindow()->GetRootWindow()->GetChildById(
          kShellWindowId_SettingBubbleContainer));

  // We override the role because the base class sets it to alert dialog,
  // which results in each tooltip title being announced twice on screen
  // readers each time it is shown.
  SetAccessibleWindowRole(ax::mojom::Role::kDialog);
}

ShelfBubble::~ShelfBubble() = default;

void ShelfBubble::CreateBubble() {
  // Actually create the bubble.
  views::BubbleDialogDelegateView::CreateBubble(this);

  // Settings that should only be changed just after bubble creation.
  GetBubbleFrameView()->SetRoundedCorners(gfx::RoundedCornersF(border_radius_));
  GetBubbleFrameView()->SetBackgroundColor(background_color());
}

std::unique_ptr<views::NonClientFrameView>
ShelfBubble::CreateNonClientFrameView(views::Widget* widget) {
  auto frame = for_tooltip_
                   ? std::make_unique<ShelfTooltipBubbleFrameView>()
                   : BubbleDialogDelegateView::CreateNonClientFrameView(widget);
  auto* frame_ptr = static_cast<views::BubbleFrameView*>(frame.get());
  frame_ptr->set_use_anchor_window_bounds(false);

  auto border = std::make_unique<views::BubbleBorder>(arrow(), GetShadow());
  border->SetColor(background_color());
  frame_ptr->SetBubbleBorder(std::move(border));

  return frame;
}

BEGIN_METADATA(ShelfBubble)
END_METADATA

}  // namespace ash
