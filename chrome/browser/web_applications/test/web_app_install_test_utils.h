// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WEB_APPLICATIONS_TEST_WEB_APP_INSTALL_TEST_UTILS_H_
#define CHROME_BROWSER_WEB_APPLICATIONS_TEST_WEB_APP_INSTALL_TEST_UTILS_H_

#include <memory>
#include <string>
#include <vector>

#include "build/build_config.h"
#include "chrome/browser/web_applications/web_app_install_info.h"
#include "chrome/browser/web_applications/web_app_install_params.h"
#include "chrome/common/buildflags.h"
#include "components/webapps/browser/installable/installable_metrics.h"
#include "components/webapps/browser/uninstall_result_code.h"
#include "components/webapps/common/web_app_id.h"

class GURL;
class Profile;

namespace web_app {

class WebAppProvider;

namespace test {

// Start the WebAppProvider and subsystems, and wait for startup to complete.
// Disables auto-install of system web apps and default web apps. Intended for
// unit tests, not browser tests.
void AwaitStartWebAppProviderAndSubsystems(Profile* profile);

// Wait until the provided WebAppProvider is ready.
void WaitUntilReady(WebAppProvider* provider);

// Wait until the provided WebAppProvider is ready and its subsystems startup
// is complete.
void WaitUntilWebAppProviderAndSubsystemsReady(WebAppProvider* provider);

webapps::AppId InstallDummyWebApp(
    Profile* profile,
    const std::string& app_name,
    const GURL& app_url,
    const webapps::WebappInstallSource install_source =
        webapps::WebappInstallSource::OMNIBOX_INSTALL_ICON);

// Synchronous version of
// WebAppCommandScheduler::InstallFromInfoWithParams. Will automatically choose
// the proto::InstallState based on if the test is is handling os integration
// using an OsIntegrationTestOverrideBlockingRegistration. May be used in unit
// tests and browser tests.
webapps::AppId InstallWebApp(
    Profile* profile,
    std::unique_ptr<WebAppInstallInfo> web_app_info,
    bool overwrite_existing_manifest_fields = false,
    webapps::WebappInstallSource install_source =
        webapps::WebappInstallSource::OMNIBOX_INSTALL_ICON);

// Synchronous version of
// WebAppCommandScheduler::InstallFromInfoNoIntegrationForTesting. May be used
// in unit tests and browser tests.
webapps::AppId InstallWebAppWithoutOsIntegration(
    Profile* profile,
    std::unique_ptr<WebAppInstallInfo> web_app_info,
    bool overwrite_existing_manifest_fields = false,
    webapps::WebappInstallSource install_source =
        webapps::WebappInstallSource::OMNIBOX_INSTALL_ICON);

// Synchronously uninstall a web app. May be used in unit tests and browser
// tests. Emulates a user uninstall - if the web app cannot be uninstalled by
// the user, then this will fail.
void UninstallWebApp(Profile* profile,
                     const webapps::AppId& app_id,
                     webapps::WebappUninstallSource uninstall_source =
                         webapps::WebappUninstallSource::kAppMenu);

// Synchronously uninstall all web apps for the given profile. May be used in
// unit tests and browser tests. Returns `false` if there was a failure.
bool UninstallAllWebApps(Profile* profile);

// Fetches the manifest for the given web contents and installs the app that
// exists there. Unit tests should use this in combination with the
// FakeWebContentsManager to set the manifest & page state for the current web
// contents page.
webapps::AppId InstallForWebContents(
    Profile* profile,
    content::WebContents* web_contents,
    webapps::WebappInstallSource install_surface);

}  // namespace test
}  // namespace web_app

#endif  // CHROME_BROWSER_WEB_APPLICATIONS_TEST_WEB_APP_INSTALL_TEST_UTILS_H_
