// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/safe_browsing/android/services_delegate_android.h"

#include "base/check_op.h"
#include "base/memory/ptr_util.h"
#include "base/notreached.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/safe_browsing/download_protection/download_protection_delegate_android.h"
#include "chrome/browser/safe_browsing/download_protection/download_protection_service.h"
#include "chrome/browser/safe_browsing/telemetry/android/android_telemetry_service.h"
#include "components/safe_browsing/android/remote_database_manager.h"
#include "components/safe_browsing/buildflags.h"
#include "components/safe_browsing/core/common/features.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_manager.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/preferences/public/mojom/tracked_preference_validation_delegate.mojom.h"

namespace safe_browsing {

// static
std::unique_ptr<ServicesDelegate> ServicesDelegate::Create(
    SafeBrowsingServiceImpl* safe_browsing_service) {
  return base::WrapUnique(new ServicesDelegateAndroid(safe_browsing_service));
}

// static
std::unique_ptr<ServicesDelegate> ServicesDelegate::CreateForTest(
    SafeBrowsingServiceImpl* safe_browsing_service,
    ServicesDelegate::ServicesCreator* services_creator) {
  return base::WrapUnique(
      new ServicesDelegateAndroid(safe_browsing_service, services_creator));
}

ServicesDelegateAndroid::ServicesDelegateAndroid(
    SafeBrowsingServiceImpl* safe_browsing_service)
    : ServicesDelegate(safe_browsing_service, /*services_creator=*/nullptr) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
}

ServicesDelegateAndroid::ServicesDelegateAndroid(
    SafeBrowsingServiceImpl* safe_browsing_service,
    ServicesDelegate::ServicesCreator* services_creator)
    : ServicesDelegate(safe_browsing_service, services_creator) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
}

ServicesDelegateAndroid::~ServicesDelegateAndroid() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
}

const scoped_refptr<SafeBrowsingDatabaseManager>&
ServicesDelegateAndroid::database_manager() const {
  return database_manager_;
}

void ServicesDelegateAndroid::Initialize() {
  if (!database_manager_set_for_tests_) {
    database_manager_ =
        base::WrapRefCounted(new RemoteSafeBrowsingDatabaseManager());
  }

  download_service_.reset(
      (services_creator_ &&
       services_creator_->CanCreateDownloadProtectionService())
          ? services_creator_->CreateDownloadProtectionService()
          : CreateDownloadProtectionService());
}

void ServicesDelegateAndroid::SetDatabaseManagerForTest(
    SafeBrowsingDatabaseManager* database_manager) {
  database_manager_set_for_tests_ = true;
  database_manager_ = database_manager;
}

void ServicesDelegateAndroid::ShutdownServices() {
  telemetry_service_.reset();
  download_service_.reset();
  ServicesDelegate::ShutdownServices();
}

void ServicesDelegateAndroid::RefreshState(bool enable) {
  if (download_service_) {
    download_service_->SetEnabled(enable);
  }
}

std::unique_ptr<prefs::mojom::TrackedPreferenceValidationDelegate>
ServicesDelegateAndroid::CreatePreferenceValidationDelegate(Profile* profile) {
  return nullptr;
}

void ServicesDelegateAndroid::RegisterDelayedAnalysisCallback(
    DelayedAnalysisCallback callback) {}

void ServicesDelegateAndroid::AddDownloadManager(
    content::DownloadManager* download_manager) {}

DownloadProtectionService* ServicesDelegateAndroid::GetDownloadService() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return download_service_.get();
}

void ServicesDelegateAndroid::StartOnUIThread(
    scoped_refptr<network::SharedURLLoaderFactory> browser_url_loader_factory,
    const V4ProtocolConfig& v4_config) {
  database_manager_->StartOnUIThread(browser_url_loader_factory, v4_config);
}

void ServicesDelegateAndroid::StopOnUIThread(bool shutdown) {
  database_manager_->StopOnUIThread(shutdown);
}

void ServicesDelegateAndroid::CreateTelemetryService(Profile* profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(profile);

  if (profile->IsOffTheRecord())
    return;

  DCHECK(!telemetry_service_);
  telemetry_service_ = std::make_unique<AndroidTelemetryService>(profile);
}

void ServicesDelegateAndroid::RemoveTelemetryService(Profile* profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (telemetry_service_ && telemetry_service_->profile() == profile)
    telemetry_service_.reset();
}

DownloadProtectionService*
ServicesDelegateAndroid::CreateDownloadProtectionService() {
  auto delegate = std::make_unique<DownloadProtectionDelegateAndroid>();
  auto download_service = std::make_unique<DownloadProtectionService>(
      safe_browsing_service_, std::move(delegate));
  return download_service.release();
}

void ServicesDelegateAndroid::OnProfileWillBeDestroyed(Profile* profile) {
  if (download_service_) {
    download_service_->RemovePendingDownloadRequests(profile);
  }
}

}  // namespace safe_browsing
