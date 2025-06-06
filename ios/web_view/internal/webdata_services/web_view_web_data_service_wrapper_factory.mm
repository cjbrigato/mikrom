// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web_view/internal/webdata_services/web_view_web_data_service_wrapper_factory.h"

#import "base/check.h"
#import "base/files/file_path.h"
#import "base/functional/bind.h"
#import "base/functional/callback.h"
#import "base/functional/callback_helpers.h"
#import "base/no_destructor.h"
#import "components/autofill/core/browser/webdata/autofill_webdata_service.h"
#import "components/keyed_service/core/service_access_type.h"
#import "components/keyed_service/ios/browser_state_dependency_manager.h"
#import "components/signin/public/webdata/token_web_data.h"
#import "components/sync/model/syncable_service.h"
#import "components/webdata_services/web_data_service_wrapper.h"
#import "ios/web/public/thread/web_task_traits.h"
#import "ios/web/public/thread/web_thread.h"
#import "ios/web_view/internal/app/application_context.h"
#import "ios/web_view/internal/web_view_browser_state.h"

namespace ios_web_view {

// static
WebDataServiceWrapper* WebViewWebDataServiceWrapperFactory::GetForBrowserState(
    WebViewBrowserState* browser_state,
    ServiceAccessType access_type) {
  DCHECK(access_type == ServiceAccessType::EXPLICIT_ACCESS ||
         !browser_state->IsOffTheRecord());
  return static_cast<WebDataServiceWrapper*>(
      GetInstance()->GetServiceForBrowserState(browser_state, true));
}

// static
scoped_refptr<autofill::AutofillWebDataService>
WebViewWebDataServiceWrapperFactory::GetAutofillWebDataForBrowserState(
    WebViewBrowserState* browser_state,
    ServiceAccessType access_type) {
  WebDataServiceWrapper* wrapper =
      GetForBrowserState(browser_state, access_type);
  return wrapper ? wrapper->GetProfileAutofillWebData() : nullptr;
}

// static
scoped_refptr<autofill::AutofillWebDataService>
WebViewWebDataServiceWrapperFactory::GetAutofillWebDataForAccount(
    WebViewBrowserState* browser_state,
    ServiceAccessType access_type) {
  WebDataServiceWrapper* wrapper =
      GetForBrowserState(browser_state, access_type);
  return wrapper ? wrapper->GetAccountAutofillWebData() : nullptr;
}

// static
scoped_refptr<TokenWebData>
WebViewWebDataServiceWrapperFactory::GetTokenWebDataForBrowserState(
    WebViewBrowserState* browser_state,
    ServiceAccessType access_type) {
  WebDataServiceWrapper* wrapper =
      GetForBrowserState(browser_state, access_type);
  return wrapper ? wrapper->GetTokenWebData() : nullptr;
}

// static
WebViewWebDataServiceWrapperFactory*
WebViewWebDataServiceWrapperFactory::GetInstance() {
  static base::NoDestructor<WebViewWebDataServiceWrapperFactory> instance;
  return instance.get();
}

WebViewWebDataServiceWrapperFactory::WebViewWebDataServiceWrapperFactory()
    : BrowserStateKeyedServiceFactory(
          "WebDataService",
          BrowserStateDependencyManager::GetInstance()) {}

WebViewWebDataServiceWrapperFactory::~WebViewWebDataServiceWrapperFactory() {}

std::unique_ptr<KeyedService>
WebViewWebDataServiceWrapperFactory::BuildServiceInstanceFor(
    web::BrowserState* context) const {
  const base::FilePath& browser_state_path = context->GetStatePath();
  return std::make_unique<WebDataServiceWrapper>(
      browser_state_path,
      ApplicationContext::GetInstance()->GetApplicationLocale(),
      web::GetUIThreadTaskRunner({}), base::DoNothing(),
      ApplicationContext::GetInstance()->GetOSCryptAsync(),
      /*use_in_memory_autofill_account_database=*/false);
}

bool WebViewWebDataServiceWrapperFactory::ServiceIsNULLWhileTesting() const {
  return true;
}

}  // namespace ios_web_view
