// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_ASH_COMPONENTS_QUICKOFFICE_QUICKOFFICE_PREFS_H_
#define CHROMEOS_ASH_COMPONENTS_QUICKOFFICE_QUICKOFFICE_PREFS_H_

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace quickoffice {

// Whether to download or open Office files in QuickOffice when they're
// navigated to.
constexpr char kQuickOfficeForceFileDownloadEnabled[] =
    "quickoffice.force_file_download_enabled";

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

}  // namespace quickoffice

#endif  // CHROMEOS_ASH_COMPONENTS_QUICKOFFICE_QUICKOFFICE_PREFS_H_
