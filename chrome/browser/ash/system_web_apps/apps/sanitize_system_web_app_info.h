// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_SYSTEM_WEB_APPS_APPS_SANITIZE_SYSTEM_WEB_APP_INFO_H_
#define CHROME_BROWSER_ASH_SYSTEM_WEB_APPS_APPS_SANITIZE_SYSTEM_WEB_APP_INFO_H_

#include <memory>

#include "chromeos/ash/experiences/system_web_apps/types/system_web_app_delegate.h"
#include "ui/gfx/geometry/rect.h"

namespace web_app {
struct WebAppInstallInfo;
}  // namespace web_app

class SanitizeSystemAppDelegate : public ash::SystemWebAppDelegate {
 public:
  explicit SanitizeSystemAppDelegate(Profile* profile);

  // ash::SystemWebAppDelegate overrides:
  std::unique_ptr<web_app::WebAppInstallInfo> GetWebAppInfo() const override;
  bool ShouldCaptureNavigations() const override;
  bool ShouldAllowResize() const override;
  bool ShouldAllowMaximize() const override;
  bool ShouldShowInLauncher() const override;
  bool ShouldAllowScriptsToCloseWindows() const override;
  bool ShouldShowInSearchAndShelf() const override;
  gfx::Rect GetDefaultBounds(ash::BrowserDelegate* browser) const override;
};

#endif  // CHROME_BROWSER_ASH_SYSTEM_WEB_APPS_APPS_SANITIZE_SYSTEM_WEB_APP_INFO_H_
