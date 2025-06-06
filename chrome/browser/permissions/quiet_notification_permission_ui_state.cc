// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/permissions/quiet_notification_permission_ui_state.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"

// static
void QuietNotificationPermissionUiState::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  // TODO(crbug.com/40097905): Consider making this syncable.
  registry->RegisterBooleanPref(prefs::kEnableQuietNotificationPermissionUi,
                                /*default_value=*/false);
  registry->RegisterBooleanPref(prefs::kEnableQuietGeolocationPermissionUi,
                                /*default_value=*/false);
  registry->RegisterBooleanPref(
      prefs::kHadThreeConsecutiveNotificationPermissionDenies,
      /*default_value=*/false);
  registry->RegisterTimePref(prefs::kQuietNotificationPermissionUiDisabledTime,
                             base::Time());
  registry->RegisterBooleanPref(prefs::kEnableNotificationCPSS,
                                /*default_value=*/true);
  registry->RegisterBooleanPref(prefs::kEnableGeolocationCPSS,
                                /*default_value=*/true);
  registry->RegisterBooleanPref(
      prefs::kDidMigrateAdaptiveNotifiationQuietingToCPSS,
      /*default_value=*/false);
}
