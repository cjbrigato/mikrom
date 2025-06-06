// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_SYSTEM_WEB_APPS_APPS_TERMINAL_SYSTEM_WEB_APP_INFO_H_
#define CHROME_BROWSER_ASH_SYSTEM_WEB_APPS_APPS_TERMINAL_SYSTEM_WEB_APP_INFO_H_

#include <memory>

#include "chrome/common/webui_url_constants.h"
#include "chromeos/ash/experiences/system_web_apps/types/system_web_app_delegate.h"
#include "ui/gfx/geometry/rect.h"

namespace web_app {
struct WebAppInstallInfo;
}  // namespace web_app

class TerminalSystemAppDelegate : public ash::SystemWebAppDelegate {
 public:
  explicit TerminalSystemAppDelegate(Profile* profile);

  // ash::SystemWebAppDelegate overrides:
  std::unique_ptr<web_app::WebAppInstallInfo> GetWebAppInfo() const override;
  ash::BrowserDelegate* GetWindowForLaunch(Profile* profile,
                                           const GURL& url) const override;
  bool ShouldShowNewWindowMenuOption() const override;
  bool ShouldShowInLauncher() const override;
  bool IsAppEnabled() const override;
  bool ShouldHaveTabStrip() const override;
  gfx::Rect GetDefaultBounds(ash::BrowserDelegate* browser) const override;
  bool HasCustomTabMenuModel() const override;
  std::unique_ptr<ui::SimpleMenuModel> GetTabMenuModel(
      ui::SimpleMenuModel::Delegate* delegate) const override;
  bool ShouldShowTabContextMenuShortcut(Profile* profile,
                                        int command_id) const override;
  // TODO(crbug.com/1308961): Migrate to use PWA pinned home tab when ready.
  bool ShouldPinTab(GURL url) const override;
  bool UseSystemThemeColor() const override;
};

#endif  // CHROME_BROWSER_ASH_SYSTEM_WEB_APPS_APPS_TERMINAL_SYSTEM_WEB_APP_INFO_H_
