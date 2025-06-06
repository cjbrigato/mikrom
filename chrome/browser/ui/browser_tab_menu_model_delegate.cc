// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/browser_tab_menu_model_delegate.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_tab_strip_model_delegate.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"

namespace chrome {

BrowserTabMenuModelDelegate::BrowserTabMenuModelDelegate(
    SessionID session_id,
    const Profile* profile,
    const web_app::AppBrowserController* app_controller)
    : session_id_(session_id),
      profile_(profile),
      app_controller_(app_controller) {}

BrowserTabMenuModelDelegate::~BrowserTabMenuModelDelegate() = default;

std::vector<Browser*> BrowserTabMenuModelDelegate::GetOtherBrowserWindows(
    bool is_app) {
  std::vector<Browser*> browsers;

  for (Browser* browser : BrowserList::GetInstance()->OrderedByActivation()) {
    // We can only move into a tabbed view of the same profile, and not the same
    // window we're currently in.
    if (browser->GetSessionID() != session_id_ &&
        browser->profile() == profile_) {
      if (is_app && browser->is_type_app() &&
          browser->app_controller()->app_id() == app_controller_->app_id()) {
        browsers.push_back(browser);
      } else if (!is_app && browser->is_type_normal()) {
        browsers.push_back(browser);
      }
    }
  }
  return browsers;
}

}  // namespace chrome
