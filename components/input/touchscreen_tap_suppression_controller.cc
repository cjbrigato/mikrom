// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/input/touchscreen_tap_suppression_controller.h"

#include <utility>

using blink::WebInputEvent;

namespace input {

TouchscreenTapSuppressionController::TouchscreenTapSuppressionController(
    const TapSuppressionController::Config& config)
    : TapSuppressionController(config) {}

TouchscreenTapSuppressionController::~TouchscreenTapSuppressionController() =
    default;

bool TouchscreenTapSuppressionController::FilterTapEvent(
    const GestureEventWithLatencyInfo& event) {
  switch (event.event.GetType()) {
    case WebInputEvent::Type::kGestureTapDown:
      return ShouldSuppressTapDown();

    case WebInputEvent::Type::kGestureShowPress:
    case WebInputEvent::Type::kGestureShortPress:
    case WebInputEvent::Type::kGestureLongPress:
    case WebInputEvent::Type::kGestureTapUnconfirmed:
    case WebInputEvent::Type::kGestureTapCancel:
    case WebInputEvent::Type::kGestureTap:
    case WebInputEvent::Type::kGestureDoubleTap:
    case WebInputEvent::Type::kGestureLongTap:
    case WebInputEvent::Type::kGestureTwoFingerTap:
      return ShouldSuppressTapEnd();

    default:
      break;
  }
  return false;
}

}  // namespace input
