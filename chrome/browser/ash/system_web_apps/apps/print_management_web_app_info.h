// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_SYSTEM_WEB_APPS_APPS_PRINT_MANAGEMENT_WEB_APP_INFO_H_
#define CHROME_BROWSER_ASH_SYSTEM_WEB_APPS_APPS_PRINT_MANAGEMENT_WEB_APP_INFO_H_

#include "chromeos/ash/experiences/system_web_apps/types/system_web_app_delegate.h"
#include "ui/gfx/geometry/size.h"

namespace web_app {
struct WebAppInstallInfo;
}  // namespace web_app

class PrintManagementSystemAppDelegate : public ash::SystemWebAppDelegate {
 public:
  explicit PrintManagementSystemAppDelegate(Profile* profile);

  // ash::SystemWebAppDelegate overrides:
  std::unique_ptr<web_app::WebAppInstallInfo> GetWebAppInfo() const override;
  bool ShouldShowInLauncher() const override;
  gfx::Size GetMinimumWindowSize() const override;
};

#endif  // CHROME_BROWSER_ASH_SYSTEM_WEB_APPS_APPS_PRINT_MANAGEMENT_WEB_APP_INFO_H_
