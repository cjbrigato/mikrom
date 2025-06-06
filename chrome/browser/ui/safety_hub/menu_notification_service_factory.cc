// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/safety_hub/menu_notification_service_factory.h"

#include "base/feature_list.h"
#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/safety_hub/menu_notification_service.h"
#include "chrome/browser/ui/safety_hub/notification_permission_review_service.h"
#include "chrome/browser/ui/safety_hub/notification_permission_review_service_factory.h"
#include "chrome/browser/ui/safety_hub/revoked_permissions_service.h"
#include "chrome/browser/ui/safety_hub/revoked_permissions_service_factory.h"
#include "chrome/browser/ui/safety_hub/safety_hub_service.h"
#include "chrome/common/chrome_features.h"

#if !BUILDFLAG(IS_ANDROID)
#include "chrome/browser/ui/safety_hub/password_status_check_service_factory.h"
#include "chrome/browser/ui/safety_hub/safety_hub_hats_service_factory.h"
#include "extensions/browser/extension_prefs.h"          // nogncheck
#include "extensions/browser/extension_prefs_factory.h"  // nogncheck
#include "extensions/browser/extension_registry.h"       // nogncheck
#endif  // BUILDFLAG(IS_ANDROID)

// static
SafetyHubMenuNotificationServiceFactory*
SafetyHubMenuNotificationServiceFactory::GetInstance() {
  static base::NoDestructor<SafetyHubMenuNotificationServiceFactory> instance;
  return instance.get();
}

// static
SafetyHubMenuNotificationService*
SafetyHubMenuNotificationServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<SafetyHubMenuNotificationService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

SafetyHubMenuNotificationServiceFactory::
    SafetyHubMenuNotificationServiceFactory()
    : ProfileKeyedServiceFactory(
          "SafetyHubMenuNotificationService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              // TODO(crbug.com/41488885): Check if this service is needed for
              // Ash Internals.
              .WithAshInternals(ProfileSelection::kOriginalOnly)
              .Build()) {
  DependsOn(RevokedPermissionsServiceFactory::GetInstance());
  DependsOn(NotificationPermissionsReviewServiceFactory::GetInstance());
#if !BUILDFLAG(IS_ANDROID)
  DependsOn(PasswordStatusCheckServiceFactory::GetInstance());
  DependsOn(SafetyHubHatsServiceFactory::GetInstance());
  DependsOn(extensions::ExtensionPrefsFactory::GetInstance());
#endif  // BUIDFLAG(IS_ANDROID)
}

SafetyHubMenuNotificationServiceFactory::
    ~SafetyHubMenuNotificationServiceFactory() = default;

std::unique_ptr<KeyedService>
SafetyHubMenuNotificationServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  auto* profile = Profile::FromBrowserContext(context);
  RevokedPermissionsService* revoked_permissions_service =
      RevokedPermissionsServiceFactory::GetForProfile(profile);
  NotificationPermissionsReviewService* notification_permission_review_service =
      NotificationPermissionsReviewServiceFactory::GetForProfile(profile);
#if BUILDFLAG(IS_ANDROID)
  return std::make_unique<SafetyHubMenuNotificationService>(
      profile->GetPrefs(), revoked_permissions_service,
      notification_permission_review_service, profile);
#else
  PasswordStatusCheckService* password_check_service =
      PasswordStatusCheckServiceFactory::GetForProfile(profile);
  SafetyHubHatsService* safety_hub_hats_service =
      SafetyHubHatsServiceFactory::GetForProfile(profile);
  return std::make_unique<SafetyHubMenuNotificationService>(
      profile->GetPrefs(), revoked_permissions_service,
      notification_permission_review_service, password_check_service,
      safety_hub_hats_service, profile);
#endif  // BUILDFLAG(IS_ANDROID)
}
