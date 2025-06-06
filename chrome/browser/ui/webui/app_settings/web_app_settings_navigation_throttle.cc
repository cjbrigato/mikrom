// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/app_settings/web_app_settings_navigation_throttle.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/web_applications/proto/web_app_install_state.pb.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"

namespace {

bool g_disable_throttle_for_testing_ = false;

}  // namespace

// static
void WebAppSettingsNavigationThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  // Check the current url scheme is chrome://
  content::NavigationHandle& handle = registry.GetNavigationHandle();
  if (!handle.GetURL().SchemeIs(content::kChromeUIScheme)) {
    return;
  }
  // Check the current url is chrome://app-settings
  if (handle.GetURL().host_piece() != chrome::kChromeUIWebAppSettingsHost) {
    return;
  }

  registry.AddThrottle(
      std::make_unique<WebAppSettingsNavigationThrottle>(registry));
}

// static
void WebAppSettingsNavigationThrottle::DisableForTesting() {
  g_disable_throttle_for_testing_ = true;
}

WebAppSettingsNavigationThrottle::WebAppSettingsNavigationThrottle(
    content::NavigationThrottleRegistry& registry)
    : content::NavigationThrottle(registry) {}

WebAppSettingsNavigationThrottle::~WebAppSettingsNavigationThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult
WebAppSettingsNavigationThrottle::WillStartRequest() {
  if (g_disable_throttle_for_testing_) {
    return content::NavigationThrottle::PROCEED;
  }

  const webapps::AppId app_id =
      web_app::GetAppIdFromAppSettingsUrl(navigation_handle()->GetURL());
  if (app_id.empty()) {
    return content::NavigationThrottle::BLOCK_REQUEST;
  }

  content::WebContents* web_contents = navigation_handle()->GetWebContents();
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  auto* provider = web_app::WebAppProvider::GetForWebApps(profile);
  if (!provider) {
    return content::NavigationThrottle::BLOCK_REQUEST;
  }

  if (!provider->on_registry_ready().is_signaled()) {
    provider->on_registry_ready().Post(
        FROM_HERE,
        base::BindOnce(&WebAppSettingsNavigationThrottle::ContinueCheckForApp,
                       weak_factory_.GetWeakPtr(), app_id));
    return content::NavigationThrottle::DEFER;
  }

  if (!provider->registrar_unsafe().IsInstallState(
          app_id, {web_app::proto::INSTALLED_WITHOUT_OS_INTEGRATION,
                   web_app::proto::INSTALLED_WITH_OS_INTEGRATION})) {
    return content::NavigationThrottle::BLOCK_REQUEST;
  }

  return content::NavigationThrottle::PROCEED;
}

const char* WebAppSettingsNavigationThrottle::GetNameForLogging() {
  return "WebAppSettingsNavigationThrottle";
}

void WebAppSettingsNavigationThrottle::ContinueCheckForApp(
    const webapps::AppId& app_id) {
  content::WebContents* web_contents = navigation_handle()->GetWebContents();
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  auto* provider = web_app::WebAppProvider::GetForWebApps(profile);
  CHECK(provider);
  if (provider->registrar_unsafe().IsInstallState(
          app_id, {web_app::proto::INSTALLED_WITHOUT_OS_INTEGRATION,
                   web_app::proto::INSTALLED_WITH_OS_INTEGRATION})) {
    Resume();
  } else {
    CancelDeferredNavigation(content::NavigationThrottle::BLOCK_REQUEST);
  }
}
