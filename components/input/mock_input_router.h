// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_INPUT_MOCK_INPUT_ROUTER_H_
#define COMPONENTS_INPUT_MOCK_INPUT_ROUTER_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/input/touch_action.h"
#include "components/input/event_with_latency_info.h"
#include "components/input/input_router.h"
#include "third_party/blink/public/mojom/input/touch_event.mojom.h"

namespace input {
class InputRouterClient;

class MockInputRouter : public InputRouter {
 public:
  explicit MockInputRouter(InputRouterClient* client) : client_(client) {
    MakeActive();
  }

  MockInputRouter(const MockInputRouter&) = delete;
  MockInputRouter& operator=(const MockInputRouter&) = delete;

  ~MockInputRouter() override = default;

  // InputRouter:
  void SendMouseEvent(const MouseEventWithLatencyInfo& mouse_event,
                      MouseEventCallback event_result_callback,
                      DispatchToRendererCallback& dispatch_callback) override;
  void SendWheelEvent(const MouseWheelEventWithLatencyInfo& wheel_event,
                      DispatchToRendererCallback& dispatch_callback) override;
  void SendKeyboardEvent(
      const NativeWebKeyboardEventWithLatencyInfo& key_event,
      KeyboardEventCallback event_result_callback,
      DispatchToRendererCallback& dispatch_callback) override;
  void SendGestureEvent(const GestureEventWithLatencyInfo& gesture_event,
                        DispatchToRendererCallback& dispatch_callback) override;
  void SendTouchEvent(const TouchEventWithLatencyInfo& touch_event,
                      DispatchToRendererCallback& dispatch_callback) override;
  void NotifySiteIsMobileOptimized(bool is_mobile_optimized) override {}
  bool HasPendingEvents() const override;
  void SetDeviceScaleFactor(float device_scale_factor) override {}
  std::optional<cc::TouchAction> AllowedTouchAction() override;
  std::optional<cc::TouchAction> ActiveTouchAction() override;
  void SetForceEnableZoom(bool enabled) override {}
  mojo::PendingRemote<blink::mojom::WidgetInputHandlerHost> BindNewHost(
      scoped_refptr<base::SequencedTaskRunner> task_runner) override;
  void StopFling() override {}
  void ForceSetTouchActionAuto() override {}
  void OnHasTouchEventConsumers(
      blink::mojom::TouchEventConsumersPtr consumers) override;
  void WaitForInputProcessed(base::OnceClosure callback) override {}

  bool sent_mouse_event_ = false;
  bool sent_wheel_event_ = false;
  bool sent_keyboard_event_ = false;
  bool sent_gesture_event_ = false;
  bool send_touch_event_not_cancelled_ = false;
  bool has_handlers_ = false;

 private:
  raw_ptr<InputRouterClient> client_ = nullptr;
};

}  // namespace input

#endif  // COMPONENTS_INPUT_MOCK_INPUT_ROUTER_H_
