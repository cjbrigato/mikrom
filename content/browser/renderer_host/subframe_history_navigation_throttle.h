// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_SUBFRAME_HISTORY_NAVIGATION_THROTTLE_H_
#define CONTENT_BROWSER_RENDERER_HOST_SUBFRAME_HISTORY_NAVIGATION_THROTTLE_H_

#include "base/memory/weak_ptr.h"
#include "content/public/browser/navigation_throttle.h"

namespace content {

// Defers subframe history navigations while waiting for a main-frame
// same-document history navigation. If a main-frame same-document navigation is
// cancelled by the web-exposed Navigation API, all associated subframe history
// navigations should also be cancelled.
class SubframeHistoryNavigationThrottle final : public NavigationThrottle {
 public:
  explicit SubframeHistoryNavigationThrottle(
      NavigationThrottleRegistry& registry);
  SubframeHistoryNavigationThrottle(const SubframeHistoryNavigationThrottle&) =
      delete;
  SubframeHistoryNavigationThrottle& operator=(
      const SubframeHistoryNavigationThrottle&) = delete;
  ~SubframeHistoryNavigationThrottle() final;

  // NavigationThrottle method:
  ThrottleCheckResult WillStartRequest() final;
  ThrottleCheckResult WillCommitWithoutUrlLoader() final;
  const char* GetNameForLogging() final;
  void Resume() final;

  void Cancel();

  static void MaybeCreateAndAdd(NavigationThrottleRegistry& registry);

 private:
  enum class State {
    // The initial state.
    kRunning,
    // This throttle returned DEFER and hasn't call Resume yet.
    kDeferred,
    // This throttle received Resume signal, and should not DEFER anymore.
    kRunningAfterResumeSignal,
  };
  State state_= State::kRunning;

  base::WeakPtrFactory<SubframeHistoryNavigationThrottle> weak_factory_{this};
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_SUBFRAME_HISTORY_NAVIGATION_THROTTLE_H_
