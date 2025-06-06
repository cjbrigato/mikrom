// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/ash/settings/pages/privacy/sync_section.h"

#include <array>

#include "base/check_deref.h"
#include "base/containers/span.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/ash/settings/os_settings_features_util.h"
#include "chrome/browser/ui/webui/ash/settings/pages/people/os_sync_handler.h"
#include "chrome/browser/ui/webui/ash/settings/search/search_tag_registry.h"
#include "chrome/browser/ui/webui/settings/shared_settings_localized_strings_provider.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/branded_strings.h"
#include "chrome/grit/generated_resources.h"
#include "chromeos/ash/components/browser_context_helper/browser_context_helper.h"
#include "components/google/core/common/google_util.h"
#include "components/sync/base/features.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/base/l10n/l10n_util.h"

namespace ash::settings {

namespace mojom {
using ::chromeos::settings::mojom::kPrivacyAndSecuritySectionPath;
using ::chromeos::settings::mojom::kSyncControlsSubpagePath;
using ::chromeos::settings::mojom::kSyncDeprecatedAdvancedSubpagePath;
using ::chromeos::settings::mojom::kSyncSetupSubpagePath;
using ::chromeos::settings::mojom::Section;
using ::chromeos::settings::mojom::Setting;
using ::chromeos::settings::mojom::Subpage;
}  // namespace mojom

namespace {

void AddSyncControlsStrings(content::WebUIDataSource* html_source) {
  static constexpr webui::LocalizedString kLocalizedStrings[] = {
      {"syncEverythingCheckboxLabel",
       IDS_SETTINGS_SYNC_EVERYTHING_CHECKBOX_LABEL},
      {"syncAdvancedPageTitle", IDS_SETTINGS_NEW_SYNC_ADVANCED_PAGE_TITLE},
      {"syncEverythingCheckboxLabel",
       IDS_SETTINGS_SYNC_EVERYTHING_CHECKBOX_LABEL},
      {"nonPersonalizedServicesSectionLabel",
       IDS_SETTINGS_NON_PERSONALIZED_SERVICES_SECTION_LABEL},
      {"customizeSyncLabel", IDS_SETTINGS_CUSTOMIZE_SYNC},
      {"syncData", IDS_SETTINGS_SYNC_DATA},
      {"wallpaperCheckboxLabel", IDS_OS_SETTINGS_WALLPAPER_CHECKBOX_LABEL},
      {"osSyncTurnOff", IDS_OS_SETTINGS_SYNC_TURN_OFF},
      {"osSyncSettingsCheckboxLabel",
       IDS_OS_SETTINGS_SYNC_SETTINGS_CHECKBOX_LABEL},
      {"osSyncWifiConfigurationsCheckboxLabel",
       IDS_OS_SETTINGS_WIFI_CONFIGURATIONS_CHECKBOX_LABEL},
      {"osSyncAppsCheckboxLabel", IDS_OS_SETTINGS_SYNC_APPS_CHECKBOX_LABEL},
      {"osSyncTurnOn", IDS_OS_SETTINGS_SYNC_TURN_ON},
      {"osSyncFeatureLabel", IDS_OS_SETTINGS_SYNC_FEATURE_LABEL},
      {"spellingPref", IDS_SETTINGS_SPELLING_PREF},
      {"spellingDescription", IDS_SETTINGS_SPELLING_PREF_DESC},
      {"enablePersonalizationLogging", IDS_SETTINGS_ENABLE_LOGGING_PREF},
      {"enablePersonalizationLoggingDesc",
       IDS_SETTINGS_ENABLE_LOGGING_PREF_DESC},
  };
  html_source->AddLocalizedStrings(kLocalizedStrings);
}

base::span<const SearchConcept> GetCategorizedSyncSearchConcepts() {
  static constexpr auto tags = std::to_array<SearchConcept>({
      {IDS_OS_SETTINGS_TAG_SYNC,
       mojom::kSyncControlsSubpagePath,
       mojom::SearchResultIcon::kSync,
       mojom::SearchResultDefaultRank::kMedium,
       mojom::SearchResultType::kSubpage,
       {.subpage = mojom::Subpage::kSyncControls}},
  });
  return tags;
}

}  // namespace

SyncSection::SyncSection(Profile* profile,
                         SearchTagRegistry* search_tag_registry)
    : OsSettingsSection(profile, search_tag_registry) {
  CHECK(profile);
  CHECK(search_tag_registry);

  // No search tags are registered if in guest mode.
  const auto& user = CHECK_DEREF(
      BrowserContextHelper::Get()->GetUserByBrowserContext(profile));
  if (IsGuestModeActive(user)) {
    return;
  }

  SearchTagRegistry::ScopedTagUpdater updater = registry()->StartUpdate();
  updater.AddSearchTags(GetCategorizedSyncSearchConcepts());
}

SyncSection::~SyncSection() = default;

void SyncSection::AddLoadTimeData(content::WebUIDataSource* html_source) {
  html_source->AddLocalizedString(
      "syncAndNonPersonalizedServices",
      IDS_SETTINGS_SYNC_SYNC_AND_NON_PERSONALIZED_SERVICES);

  static constexpr webui::LocalizedString kSignOutStrings[] = {
      {"syncDisconnect", IDS_SETTINGS_PEOPLE_SIGN_OUT},
      {"syncDisconnectTitle", IDS_SETTINGS_SYNC_DISCONNECT_TITLE},
      {"syncDisconnectConfirm", IDS_SETTINGS_SYNC_DISCONNECT_CONFIRM},
  };
  html_source->AddLocalizedStrings(kSignOutStrings);

  std::string sync_dashboard_url =
      google_util::AppendGoogleLocaleParam(
          GURL(chrome::kSyncGoogleDashboardURL),
          g_browser_process->GetApplicationLocale())
          .spec();

  html_source->AddString(
      "syncDisconnectExplanation",
      l10n_util::GetStringFUTF8(IDS_SETTINGS_SYNC_DISCONNECT_EXPLANATION,
                                base::ASCIIToUTF16(sync_dashboard_url)));
  AddSyncControlsStrings(html_source);
  ::settings::AddSharedSyncPageStrings(html_source);
}

void SyncSection::AddHandlers(content::WebUI* web_ui) {
  web_ui->AddMessageHandler(std::make_unique<OSSyncHandler>(profile()));
}

int SyncSection::GetSectionNameMessageId() const {
  return IDS_SETTINGS_SYNC_SYNC_AND_NON_PERSONALIZED_SERVICES;
}

mojom::Section SyncSection::GetSection() const {
  // Note: This is not a top-level section and does not have a respective
  // declaration in chromeos::settings::mojom::Section.
  return mojom::Section::kPrivacyAndSecurity;
}

mojom::SearchResultIcon SyncSection::GetSectionIcon() const {
  return mojom::SearchResultIcon::kSync;
}

const char* SyncSection::GetSectionPath() const {
  return mojom::kPrivacyAndSecuritySectionPath;
}

bool SyncSection::LogMetric(mojom::Setting setting, base::Value& value) const {
  // No metrics are logged.
  return false;
}

void SyncSection::RegisterHierarchy(HierarchyGenerator* generator) const {
  // Combined browser/OS sync.
  generator->RegisterTopLevelSubpage(
      IDS_SETTINGS_SYNC_SYNC_AND_NON_PERSONALIZED_SERVICES,
      mojom::Subpage::kSyncSetup, mojom::SearchResultIcon::kSync,
      mojom::SearchResultDefaultRank::kMedium, mojom::kSyncSetupSubpagePath);
  static constexpr mojom::Setting kSyncSettings[] = {
      mojom::Setting::kNonSplitSyncEncryptionOptions,
      mojom::Setting::kImproveSearchSuggestions,
      mojom::Setting::kMakeSearchesAndBrowsingBetter,
      mojom::Setting::kGoogleDriveSearchSuggestions,
  };
  RegisterNestedSettingBulk(mojom::Subpage::kSyncSetup, kSyncSettings,
                            generator);

  // TODO(crbug.com/40197769): Remove this.
  generator->RegisterNestedSubpage(
      IDS_SETTINGS_SYNC_ADVANCED_PAGE_TITLE,
      mojom::Subpage::kSyncDeprecatedAdvanced, mojom::Subpage::kSyncSetup,
      mojom::SearchResultIcon::kSync, mojom::SearchResultDefaultRank::kMedium,
      mojom::kSyncDeprecatedAdvancedSubpagePath);

  // Page with OS-specific sync data types.
  generator->RegisterTopLevelSubpage(
      IDS_SETTINGS_SYNC_ADVANCED_PAGE_TITLE, mojom::Subpage::kSyncControls,
      mojom::SearchResultIcon::kSync, mojom::SearchResultDefaultRank::kMedium,
      mojom::kSyncControlsSubpagePath);
  generator->RegisterNestedSetting(mojom::Setting::kSplitSyncOnOff,
                                   mojom::Subpage::kSyncControls);
}

}  // namespace ash::settings
