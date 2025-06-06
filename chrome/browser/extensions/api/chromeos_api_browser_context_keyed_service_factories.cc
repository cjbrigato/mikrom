// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/chromeos_api_browser_context_keyed_service_factories.h"

#include "chrome/browser/ash/extensions/autotest_private/autotest_private_api.h"
#include "chrome/browser/ash/extensions/install_limiter_factory.h"
#include "chrome/browser/ash/extensions/speech/speech_recognition_private_manager_factory.h"
#include "chrome/browser/ash/extensions/users_private/users_private_delegate_factory.h"
#include "chrome/browser/chromeos/extensions/contact_center_insights/contact_center_insights_extension_manager_factory.h"
#include "chrome/browser/chromeos/extensions/desk_api/desk_api_extension_manager_factory.h"
#include "chrome/browser/chromeos/extensions/file_system_provider/service_worker_lifetime_manager.h"
#include "chrome/browser/chromeos/extensions/login_screen/login/external_logout_done/external_logout_done_event_handler_factory.h"
#include "chrome/browser/chromeos/extensions/login_screen/login/external_logout_request/external_logout_request_event_handler_factory.h"
#include "chrome/browser/chromeos/extensions/login_screen/login_state/session_state_changed_event_dispatcher.h"
#include "chrome/browser/chromeos/extensions/smart_card_provider_private/smart_card_provider_private_api.h"
#include "chrome/browser/chromeos/extensions/vpn_provider/vpn_service_factory.h"
#include "chrome/browser/chromeos/platform_keys/extension_platform_keys_service_factory.h"

namespace chromeos_extensions {

void EnsureBrowserContextKeyedServiceFactoriesBuilt() {
  chromeos::ContactCenterInsightsExtensionManagerFactory::GetInstance();
  chromeos::DeskApiExtensionManagerFactory::GetInstance();
  chromeos::ExtensionPlatformKeysServiceFactory::GetInstance();
  chromeos::VpnServiceFactory::GetInstance();
  extensions::AutotestPrivateAPI::GetFactoryInstance();
  extensions::ExternalLogoutDoneEventHandlerFactory::GetInstance();
  extensions::ExternalLogoutRequestEventHandlerFactory::GetInstance();
  extensions::file_system_provider::ServiceWorkerLifetimeManagerFactory::
      GetInstance();
  extensions::InstallLimiterFactory::GetInstance();
  extensions::SessionStateChangedEventDispatcher::GetFactoryInstance();
  extensions::SmartCardProviderPrivateAPI::GetFactoryInstance();
  extensions::SpeechRecognitionPrivateManagerFactory::GetInstance();
  extensions::UsersPrivateDelegateFactory::GetInstance();
}

}  // namespace chromeos_extensions
