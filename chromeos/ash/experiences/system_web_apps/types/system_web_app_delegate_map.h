// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_ASH_EXPERIENCES_SYSTEM_WEB_APPS_TYPES_SYSTEM_WEB_APP_DELEGATE_MAP_H_
#define CHROMEOS_ASH_EXPERIENCES_SYSTEM_WEB_APPS_TYPES_SYSTEM_WEB_APP_DELEGATE_MAP_H_

#include <memory>

#include "ash/webui/system_apps/public/system_web_app_type.h"
#include "base/containers/flat_map.h"
#include "chromeos/ash/experiences/system_web_apps/types/system_web_app_delegate.h"

namespace ash {

using SystemWebAppDelegateMap =
    base::flat_map<SystemWebAppType, std::unique_ptr<SystemWebAppDelegate>>;

// Returns the System App Delegate for the given App |type|.
const SystemWebAppDelegate* GetSystemWebApp(
    const SystemWebAppDelegateMap& delegates,
    SystemWebAppType type);

}  // namespace ash

#endif  // CHROMEOS_ASH_EXPERIENCES_SYSTEM_WEB_APPS_TYPES_SYSTEM_WEB_APP_DELEGATE_MAP_H_
