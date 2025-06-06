// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web_view/internal/signin/web_view_identity_manager_factory.h"

#import <memory>

#import "base/no_destructor.h"
#import "components/image_fetcher/ios/ios_image_decoder_impl.h"
#import "components/keyed_service/core/keyed_service.h"
#import "components/keyed_service/ios/browser_state_dependency_manager.h"
#import "components/pref_registry/pref_registry_syncable.h"
#import "components/prefs/pref_service.h"
#import "components/signin/public/base/account_consistency_method.h"
#import "components/signin/public/base/signin_client.h"
#import "components/signin/public/base/signin_pref_names.h"
#import "components/signin/public/identity_manager/identity_manager.h"
#import "components/signin/public/identity_manager/identity_manager_builder.h"
#import "ios/web_view/internal/app/application_context.h"
#import "ios/web_view/internal/signin/account_capabilities_fetcher_factory_ios_web_view.h"
#import "ios/web_view/internal/signin/ios_web_view_signin_client.h"
#import "ios/web_view/internal/signin/web_view_device_accounts_provider_impl.h"
#import "ios/web_view/internal/signin/web_view_signin_client_factory.h"
#import "ios/web_view/internal/web_view_browser_state.h"

namespace ios_web_view {

void WebViewIdentityManagerFactory::RegisterBrowserStatePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  signin::IdentityManager::RegisterProfilePrefs(registry);
}

WebViewIdentityManagerFactory::WebViewIdentityManagerFactory()
    : BrowserStateKeyedServiceFactory(
          "IdentityManager",
          BrowserStateDependencyManager::GetInstance()) {
  DependsOn(WebViewSigninClientFactory::GetInstance());
}

WebViewIdentityManagerFactory::~WebViewIdentityManagerFactory() {}

// static
signin::IdentityManager* WebViewIdentityManagerFactory::GetForBrowserState(
    WebViewBrowserState* browser_state) {
  return static_cast<signin::IdentityManager*>(
      GetInstance()->GetServiceForBrowserState(browser_state, true));
}

// static
WebViewIdentityManagerFactory* WebViewIdentityManagerFactory::GetInstance() {
  static base::NoDestructor<WebViewIdentityManagerFactory> instance;
  return instance.get();
}

std::unique_ptr<KeyedService>
WebViewIdentityManagerFactory::BuildServiceInstanceFor(
    web::BrowserState* context) const {
  WebViewBrowserState* browser_state =
      WebViewBrowserState::FromBrowserState(context);

  IOSWebViewSigninClient* client =
      WebViewSigninClientFactory::GetForBrowserState(browser_state);

  signin::IdentityManagerBuildParams params;
  params.account_consistency = signin::AccountConsistencyMethod::kDisabled;
  params.device_accounts_provider =
      std::make_unique<WebViewDeviceAccountsProviderImpl>();
  params.image_decoder = image_fetcher::CreateIOSImageDecoder();
  params.local_state = ApplicationContext::GetInstance()->GetLocalState();
  params.pref_service = browser_state->GetPrefs();
  params.profile_path = base::FilePath();
  params.signin_client = client;
  params.require_sync_consent_for_scope_verification = false;
  params.account_capabilities_fetcher_factory = std::make_unique<
      ios_web_view::AccountCapabilitiesFetcherFactoryIOSWebView>();

  return signin::BuildIdentityManager(&params);
}

}  // namespace ios_web_view
