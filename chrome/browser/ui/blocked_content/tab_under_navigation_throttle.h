// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_BLOCKED_CONTENT_TAB_UNDER_NAVIGATION_THROTTLE_H_
#define CHROME_BROWSER_UI_BLOCKED_CONTENT_TAB_UNDER_NAVIGATION_THROTTLE_H_

#include "base/feature_list.h"
#include "content/public/browser/navigation_throttle.h"

namespace content {
class NavigationThrottleRegistry;
}

inline constexpr char kBlockTabUnderFormatMessage[] =
    "Chrome stopped this site from navigating to %s, see "
    "https://www.chromestatus.com/feature/5675755719622656 for more details.";

// This class blocks navigations that we've classified as tab-unders. It does so
// by communicating with the popup opener tab helper.
//
// Currently, navigations are considered tab-unders if:
// 1. It is a navigation that is "suspicious"
//    a. It has no user gesture.
//    b. It is renderer-initiated.
//    c. It is cross site to the last committed URL in the tab.
//    d. The navigation started in the background.
// 2. The tab has opened a popup and hasn't received a user gesture since then.
//    This information is tracked by the PopupOpenerTabHelper.
//
//  TODO(csharrison): Unfortunately, the provision that a navigation must start
//  in the background to be considered a tab-under restricts the scope of the
//  intervention. For instance, popups that do not completely hide the original
//  page may cause subsequent tab-under navigations to occur while visible (not
//  compeltely backgrounded). See https://crbug.com/733736.
//
//  For now, we allow these tab-unders because this pattern seems to be
//  legitimate for some cases (like auth).
class TabUnderNavigationThrottle : public content::NavigationThrottle {
 public:
  // This enum backs a histogram. Update enums.xml if you make any updates, and
  // put new entries before |kLast|.
  enum class Action {
    // Logged at WillStartRequest.
    kStarted,

    // Logged when a navigation is blocked.
    kBlocked,

    // Logged at the same time as kBlocked, but will additionally be logged even
    // if the experiment is turned off.
    kDidTabUnder,

    // The user clicked through to navigate to the blocked redirect.
    kClickedThrough,

    // The user did not navigate to the blocked redirect and closed the message.
    // This only gets logged when the user takes action on the UI, not when it
    // gets automatically dismissed by a navigation for example.
    kAcceptedIntervention,

    kCount
  };

  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry);

  TabUnderNavigationThrottle(const TabUnderNavigationThrottle&) = delete;
  TabUnderNavigationThrottle& operator=(const TabUnderNavigationThrottle&) =
      delete;

  ~TabUnderNavigationThrottle() override;

 private:
  explicit TabUnderNavigationThrottle(
      content::NavigationThrottleRegistry& registry);

  // This method is described at the top of this file.
  //
  // Note: This method must be called before navigation commit.
  bool IsSuspiciousClientRedirect() const;

  content::NavigationThrottle::ThrottleCheckResult MaybeBlockNavigation();
  void ShowUI();

  bool HasOpenedPopupSinceLastUserGesture() const;

  // content::NavigationThrottle:
  content::NavigationThrottle::ThrottleCheckResult WillStartRequest() override;
  content::NavigationThrottle::ThrottleCheckResult WillRedirectRequest()
      override;
  const char* GetNameForLogging() override;

  // Tracks whether this WebContents has opened a popup since the last user
  // gesture, at the time this navigation is starting.
  const bool has_opened_popup_since_last_user_gesture_at_start_ = false;

  // Whether this object was created when the hosting WebContents had visibility
  // content::Visibility::VISIBLE.
  const bool started_in_foreground_ = false;

  // True if the throttle has seen a tab under.
  bool seen_tab_under_ = false;
};

#endif  // CHROME_BROWSER_UI_BLOCKED_CONTENT_TAB_UNDER_NAVIGATION_THROTTLE_H_
