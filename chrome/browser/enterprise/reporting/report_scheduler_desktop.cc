// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/reporting/report_scheduler_desktop.h"

#include <optional>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/metrics/field_trial_params.h"
#include "base/notreached.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/enterprise/reporting/prefs.h"
#include "chrome/browser/profiles/reporting_util.h"
#include "chrome/browser/upgrade_detector/build_state.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/pref_names.h"
#include "components/device_signals/core/common/signals_features.h"
#include "components/enterprise/browser/reporting/report_scheduler.h"
#include "components/policy/core/common/cloud/dm_token.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/storage_partition.h"

namespace em = enterprise_management;

namespace enterprise_reporting {

namespace {

// Returns true if this build should generate basic reports when an update is
// detected.
// TODO(crbug.com/40703888): Get rid of this function after Chrome OS reporting
// logic has been split to its own delegates.
constexpr bool ShouldReportUpdates() {
#if BUILDFLAG(IS_CHROMEOS)
  return false;
#else
  return true;
#endif
}

PrefService* LocalState() {
  return g_browser_process->local_state();
}

}  // namespace

ReportSchedulerDesktop::ReportSchedulerDesktop()
    : profile_(nullptr), prefs_(LocalState()) {}

ReportSchedulerDesktop::ReportSchedulerDesktop(Profile* profile)
    : profile_(profile), prefs_(profile->GetPrefs()) {
  if (profile) {
#if BUILDFLAG(IS_CHROMEOS)
    NOTREACHED();
#else
    if (enterprise_signals::features::IsProfileSignalsReportingEnabled()) {
      user_security_signals_service_ =
          std::make_unique<UserSecuritySignalsService>(prefs_, this);
    }
#endif
  }
}

ReportSchedulerDesktop::~ReportSchedulerDesktop() {
  // If new profiles have been added since the last report was sent, they won't
  // be reported until the next launch, since Chrome is shutting down. However,
  // the (now obsolete) Enterprise.CloudReportingStaleProfileCount metric has
  // shown that this very rarely happens, with 99.23% of samples reporting no
  // stale profiles and 0.72% reporting a single stale profile.
  if (ShouldReportUpdates())
    g_browser_process->GetBuildState()->RemoveObserver(this);
}

PrefService* ReportSchedulerDesktop::GetPrefService() {
  return prefs_;
}

void ReportSchedulerDesktop::OnInitializationCompleted() {
  if (user_security_signals_service_) {
    user_security_signals_service_->Start();
  }
}

void ReportSchedulerDesktop::StartWatchingUpdatesIfNeeded(
    base::Time last_upload,
    base::TimeDelta upload_interval) {
  if (!ShouldReportUpdates())
    return;

  auto* build_state = g_browser_process->GetBuildState();
  if (build_state->HasObserver(this))
    // Already watching browser updates.
    return;

  build_state->AddObserver(this);

  // Generate and upload a basic report immediately if the version has
  // changed since the last upload and the last upload was less than 24h
  // ago.
  if (GetPrefService()->GetString(kLastUploadVersion) !=
          chrome::kChromeVersion &&
      last_upload + upload_interval > base::Time::Now() &&
      !trigger_report_callback_.is_null()) {
    trigger_report_callback_.Run(ReportTrigger::kTriggerNewVersion);
  }
}

void ReportSchedulerDesktop::StopWatchingUpdates() {
  if (ShouldReportUpdates()) {
    g_browser_process->GetBuildState()->RemoveObserver(this);
  }
}

void ReportSchedulerDesktop::OnBrowserVersionUploaded() {
  if (ShouldReportUpdates()) {
    // Remember what browser version made this upload.
    GetPrefService()->SetString(kLastUploadVersion, chrome::kChromeVersion);
  }
}

policy::DMToken ReportSchedulerDesktop::GetProfileDMToken() {
  std::optional<std::string> dm_token = reporting::GetUserDmToken(profile_);
  if (!dm_token || dm_token->empty())
    return policy::DMToken::CreateEmptyToken();
  return policy::DMToken::CreateValidToken(*dm_token);
}

std::string ReportSchedulerDesktop::GetProfileClientId() {
  return reporting::GetUserClientId(profile_).value_or(std::string());
}

bool ReportSchedulerDesktop::AreSecurityReportsEnabled() {
  return user_security_signals_service_ &&
         user_security_signals_service_->IsSecuritySignalsReportingEnabled();
}

bool ReportSchedulerDesktop::UseCookiesInUploads() {
  return user_security_signals_service_ &&
         user_security_signals_service_->ShouldUseCookies();
}

void ReportSchedulerDesktop::OnSecuritySignalsUploaded() {
  if (user_security_signals_service_) {
    user_security_signals_service_->OnReportUploaded();
  }
}

void ReportSchedulerDesktop::OnReportEventTriggered(
    SecurityReportTrigger trigger) {
  if (!trigger_report_callback_.is_null()) {
    trigger_report_callback_.Run(ReportTrigger::kTriggerSecurity);
  }
}

network::mojom::CookieManager* ReportSchedulerDesktop::GetCookieManager() {
  return profile_->GetDefaultStoragePartition()
      ->GetCookieManagerForBrowserProcess();
}

void ReportSchedulerDesktop::OnUpdate(const BuildState* build_state) {
  DCHECK(ShouldReportUpdates());
  // A new version has been detected on the machine and a restart is now needed
  // for it to take effect. Send a basic report (without profile info)
  // immediately.
  if (!trigger_report_callback_.is_null()) {
    trigger_report_callback_.Run(ReportTrigger::kTriggerUpdate);
  }
}

}  // namespace enterprise_reporting
