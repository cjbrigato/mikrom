// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_BLOCKED_SCHEME_NAVIGATION_THROTTLE_H_
#define CONTENT_BROWSER_RENDERER_HOST_BLOCKED_SCHEME_NAVIGATION_THROTTLE_H_

#include "content/public/browser/navigation_throttle.h"

namespace content {

// Blocks renderer-initiated top-frame navigations to certain URL schemes
// (currently data: and filesystem:).
class BlockedSchemeNavigationThrottle : public NavigationThrottle {
 public:
  explicit BlockedSchemeNavigationThrottle(
      NavigationThrottleRegistry& registry);

  BlockedSchemeNavigationThrottle(const BlockedSchemeNavigationThrottle&) =
      delete;
  BlockedSchemeNavigationThrottle& operator=(
      const BlockedSchemeNavigationThrottle&) = delete;

  ~BlockedSchemeNavigationThrottle() override;

  // NavigationThrottle method:
  ThrottleCheckResult WillStartRequest() override;
  ThrottleCheckResult WillProcessResponse() override;
  const char* GetNameForLogging() override;

  static void MaybeCreateAndAdd(NavigationThrottleRegistry& registry);
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_BLOCKED_SCHEME_NAVIGATION_THROTTLE_H_
