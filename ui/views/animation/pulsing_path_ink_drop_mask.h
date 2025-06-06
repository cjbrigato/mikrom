// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_ANIMATION_PULSING_PATH_INK_DROP_MASK_H_
#define UI_VIEWS_ANIMATION_PULSING_PATH_INK_DROP_MASK_H_

#include "ui/gfx/animation/throb_animation.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/views/animation/animation_delegate_views.h"
#include "ui/views/animation/ink_drop_mask.h"

namespace views {

// An InkDropMask that animates a highlight path into pulsing effect.
class VIEWS_EXPORT PulsingPathInkDropMask
    : public views::AnimationDelegateViews,
      public views::InkDropMask {
 public:
  explicit PulsingPathInkDropMask(views::View* layer_container, SkPath path);
  ~PulsingPathInkDropMask() override;

 private:
  // views::InkDropMask:
  void OnPaintLayer(const ui::PaintContext& context) override;

  // views::AnimationDelegateViews:
  void AnimationProgressed(const gfx::Animation* animation) override;

  // The View that contains the InkDrop layer we're masking. This must outlive
  // our instance.
  const raw_ptr<views::View> layer_container_;
  const SkPath path_;
  const SkRect initial_rect_;
  const double throb_inset_;

  gfx::ThrobAnimation throb_animation_;
};

}  // namespace views

#endif  // UI_VIEWS_ANIMATION_PULSING_PATH_INK_DROP_MASK_H_
