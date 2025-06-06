// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WEB_APPLICATIONS_PREINSTALLED_WEB_APP_CONFIG_UTILS_H_
#define CHROME_BROWSER_WEB_APPLICATIONS_PREINSTALLED_WEB_APP_CONFIG_UTILS_H_

#include <optional>

#include "base/auto_reset.h"
#include "base/files/file_path.h"

class Profile;

namespace web_app {
namespace test {

std::optional<base::FilePath> GetPreinstalledWebAppConfigDirForTesting();

using ConfigDirAutoReset = base::AutoReset<std::optional<base::FilePath>>;
ConfigDirAutoReset SetPreinstalledWebAppConfigDirForTesting(
    const base::FilePath& config_dir);

}  // namespace test

// The directory where default web app configs are stored.
// Empty if not applicable.
base::FilePath GetPreinstalledWebAppConfigDirFromCommandLine(Profile* profile);

// The directory where additional web app configs are stored. This allows a
// single Chrome OS system image to have device-specific apps for multiple
// devices. Empty if not applicable.
base::FilePath GetPreinstalledWebAppExtraConfigDirFromCommandLine(
    Profile* profile);

// The directory where default web app configs are stored.
// Empty if not applicable.
// As of mid 2018, only Chrome OS has default/external web apps.
base::FilePath GetPreinstalledWebAppConfigDir(Profile* profile);

// The directory where additional web app configs are stored.
// Empty if not applicable.
base::FilePath GetPreinstalledWebAppExtraConfigDir(Profile* profile);

}  // namespace web_app

#endif  // CHROME_BROWSER_WEB_APPLICATIONS_PREINSTALLED_WEB_APP_CONFIG_UTILS_H_
