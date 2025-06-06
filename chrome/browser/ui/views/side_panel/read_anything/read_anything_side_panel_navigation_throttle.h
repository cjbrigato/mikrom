// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_SIDE_PANEL_READ_ANYTHING_READ_ANYTHING_SIDE_PANEL_NAVIGATION_THROTTLE_H_
#define CHROME_BROWSER_UI_VIEWS_SIDE_PANEL_READ_ANYTHING_READ_ANYTHING_SIDE_PANEL_NAVIGATION_THROTTLE_H_

#include "content/public/browser/navigation_throttle.h"

namespace content {
class NavigationThrottleRegistry;
}  // namespace content

// This class prevents users from opening reading mode in the main content area
// by intercepting a request for the read anything page and instead showing the
// side panel.
class ReadAnythingSidePanelNavigationThrottle
    : public content::NavigationThrottle {
 public:
  static void CreateAndAdd(content::NavigationThrottleRegistry& registry);

  // NavigationThrottle overrides:
  ThrottleCheckResult WillStartRequest() override;
  const char* GetNameForLogging() override;

 private:
  explicit ReadAnythingSidePanelNavigationThrottle(
      content::NavigationThrottleRegistry& registry);

  ThrottleCheckResult HandleSidePanelRequest();
};

#endif  // CHROME_BROWSER_UI_VIEWS_SIDE_PANEL_READ_ANYTHING_READ_ANYTHING_SIDE_PANEL_NAVIGATION_THROTTLE_H_
