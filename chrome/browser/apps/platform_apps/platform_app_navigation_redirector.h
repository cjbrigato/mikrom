// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_APPS_PLATFORM_APPS_PLATFORM_APP_NAVIGATION_REDIRECTOR_H_
#define CHROME_BROWSER_APPS_PLATFORM_APPS_PLATFORM_APP_NAVIGATION_REDIRECTOR_H_

namespace content {
class NavigationThrottleRegistry;
}  // namespace content

// This class creates navigation throttles that redirect navigations to URLs for
// which platform apps have a matching URL handler in the 'url_handlers'
// manifest key. Note that this is a UI thread class.
class PlatformAppNavigationRedirector {
 public:
  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry);
  PlatformAppNavigationRedirector(const PlatformAppNavigationRedirector&) =
      delete;
  PlatformAppNavigationRedirector& operator=(
      const PlatformAppNavigationRedirector&) = delete;
};

#endif  // CHROME_BROWSER_APPS_PLATFORM_APPS_PLATFORM_APP_NAVIGATION_REDIRECTOR_H_
