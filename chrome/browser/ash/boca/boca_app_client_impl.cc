// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/boca/boca_app_client_impl.h"

#include "ash/webui/system_apps/public/system_web_app_type.h"
#include "chrome/browser/ash/settings/device_settings_service.h"
#include "chrome/browser/feedback/show_feedback_page.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/ui/ash/system_web_apps/system_web_app_ui_utils.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace ash::boca {
namespace {
// Used for testing Boca producer via emulator.
inline static constexpr char kDummyDeviceId[] = "kDummyDeviceId";
}  // namespace

BocaAppClientImpl::BocaAppClientImpl() = default;

BocaAppClientImpl::~BocaAppClientImpl() = default;

signin::IdentityManager* BocaAppClientImpl::GetIdentityManager() {
  Profile* profile = ProfileManager::GetActiveUserProfile();
  return IdentityManagerFactory::GetForProfile(profile);
}

scoped_refptr<network::SharedURLLoaderFactory>
BocaAppClientImpl::GetURLLoaderFactory() {
  Profile* profile = ProfileManager::GetActiveUserProfile();
  return profile->GetURLLoaderFactory();
}

std::string BocaAppClientImpl::GetDeviceId() {
  if (!ash::DeviceSettingsService::IsInitialized()) {
    return std::string();
  }
  if (auto* policy = ash::DeviceSettingsService::Get()->policy_data()) {
    return policy->device_id().empty() ? kDummyDeviceId : policy->device_id();
  }
  return std::string();
}

void BocaAppClientImpl::LaunchApp() {
  ash::LaunchSystemWebAppAsync(ProfileManager::GetActiveUserProfile(),
                               SystemWebAppType::BOCA);
}

bool BocaAppClientImpl::HasApp() {
  auto* const browser = ash::FindSystemWebAppBrowser(
      ProfileManager::GetActiveUserProfile(), SystemWebAppType::BOCA);
  return browser &&
         !BrowserList::GetInstance()->currently_closing_browsers().contains(
             browser);
}

void BocaAppClientImpl::OpenFeedbackDialog() {
  Profile* profile = ProfileManager::GetActiveUserProfile();
  constexpr char kBocaAppFeedbackCategoryTag[] = "Boca";
  chrome::ShowFeedbackPage(GURL("chrome-untrusted://boca-app/"), profile,
                           feedback::kFeedbackSourceBocaApp,
                           /*description_template=*/std::string(),
                           /*description_placeholder_text=*/std::string(),
                           kBocaAppFeedbackCategoryTag,
                           /*extra_diagnostics=*/std::string());
}
}  // namespace ash::boca
