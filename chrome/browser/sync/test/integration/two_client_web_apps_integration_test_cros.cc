// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/test/integration/two_client_web_apps_integration_test_base.h"
#include "content/public/test/browser_test.h"

namespace web_app::integration_tests {

// This test is a part of the web app integration test suite, which is
// documented in //chrome/browser/ui/views/web_apps/README.md. For information
// about diagnosing, debugging and/or disabling tests, please look to the
// README file.

namespace {

using WebAppIntegration = TwoClientWebAppsIntegrationTestBase;

IN_PROC_BROWSER_TEST_F(
    WebAppIntegration,
    WAI_31Standalone_79StandaloneStandaloneOriginal_24_12Standalone_7Standalone_112StandaloneNotShown_40Client2_7Standalone_12Standalone_10Standalone_15Standalone_40Client1_15Standalone) {
  // Test contents are generated by script. Please do not modify!
  // See `docs/webapps/why-is-this-test-failing.md` or
  // `docs/webapps/integration-testing-framework` for more info.
  // Gardeners: Disabling this test is supported.
  helper_.InstallOmniboxIcon(InstallableSite::kStandalone);
  helper_.CheckAppTitle(Site::kStandalone, Title::kStandaloneOriginal);
  helper_.CheckWindowCreated();
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckWindowControlsOverlayToggle(Site::kStandalone,
                                           IsShown::kNotShown);
  helper_.SwitchProfileClients(ProfileClient::kClient2);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.UninstallFromList(Site::kStandalone);
  helper_.CheckAppNotInList(Site::kStandalone);
  helper_.SwitchProfileClients(ProfileClient::kClient1);
  helper_.CheckAppNotInList(Site::kStandalone);
}

IN_PROC_BROWSER_TEST_F(
    WebAppIntegration,
    WAI_31Standalone_79StandaloneStandaloneOriginal_24_12Standalone_7Standalone_112StandaloneNotShown_40Client2_7Standalone_12Standalone_37Standalone_17_20) {
  // Test contents are generated by script. Please do not modify!
  // See `docs/webapps/why-is-this-test-failing.md` or
  // `docs/webapps/integration-testing-framework` for more info.
  // Gardeners: Disabling this test is supported.
  helper_.InstallOmniboxIcon(InstallableSite::kStandalone);
  helper_.CheckAppTitle(Site::kStandalone, Title::kStandaloneOriginal);
  helper_.CheckWindowCreated();
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckWindowControlsOverlayToggle(Site::kStandalone,
                                           IsShown::kNotShown);
  helper_.SwitchProfileClients(ProfileClient::kClient2);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.NavigateBrowser(Site::kStandalone);
  helper_.CheckInstallIconNotShown();
  helper_.CheckLaunchIconShown();
}

IN_PROC_BROWSER_TEST_F(
    WebAppIntegration,
    WAI_29StandaloneBrowser_79StandaloneStandaloneOriginal_11Standalone_7Standalone_40Client2_7Standalone_11Standalone_34Standalone_22One_163Standalone) {
  // Test contents are generated by script. Please do not modify!
  // See `docs/webapps/why-is-this-test-failing.md` or
  // `docs/webapps/integration-testing-framework` for more info.
  // Gardeners: Disabling this test is supported.
  helper_.CreateShortcut(Site::kStandalone, WindowOptions::kBrowser);
  helper_.CheckAppTitle(Site::kStandalone, Title::kStandaloneOriginal);
  helper_.CheckAppInListTabbed(Site::kStandalone);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.SwitchProfileClients(ProfileClient::kClient2);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckAppInListTabbed(Site::kStandalone);
  helper_.LaunchFromChromeApps(Site::kStandalone);
  helper_.CheckTabCreated(Number::kOne);
  helper_.CheckAppLoadedInTab(Site::kStandalone);
}

IN_PROC_BROWSER_TEST_F(
    WebAppIntegration,
    WAI_29StandaloneBrowser_79StandaloneStandaloneOriginal_11Standalone_7Standalone_40Client2_7Standalone_11Standalone_10Standalone_15Standalone_40Client1_15Standalone) {
  // Test contents are generated by script. Please do not modify!
  // See `docs/webapps/why-is-this-test-failing.md` or
  // `docs/webapps/integration-testing-framework` for more info.
  // Gardeners: Disabling this test is supported.
  helper_.CreateShortcut(Site::kStandalone, WindowOptions::kBrowser);
  helper_.CheckAppTitle(Site::kStandalone, Title::kStandaloneOriginal);
  helper_.CheckAppInListTabbed(Site::kStandalone);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.SwitchProfileClients(ProfileClient::kClient2);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckAppInListTabbed(Site::kStandalone);
  helper_.UninstallFromList(Site::kStandalone);
  helper_.CheckAppNotInList(Site::kStandalone);
  helper_.SwitchProfileClients(ProfileClient::kClient1);
  helper_.CheckAppNotInList(Site::kStandalone);
}

IN_PROC_BROWSER_TEST_F(
    WebAppIntegration,
    WAI_InstallOmniboxIconStandalone_SwitchProfileClientsClient2_SyncTurnOff_SyncTurnOn) {
  // Test contents are generated by script. Please do not modify!
  // See `docs/webapps/why-is-this-test-failing.md` or
  // `docs/webapps/integration-testing-framework` for more info.
  // Gardeners: Disabling this test is supported.
  helper_.InstallOmniboxIcon(InstallableSite::kStandalone);
  helper_.CheckAppTitle(Site::kStandalone, Title::kStandaloneOriginal);
  helper_.CheckWindowCreated();
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckWindowControlsOverlayToggle(Site::kStandalone,
                                           IsShown::kNotShown);
  helper_.SwitchProfileClients(ProfileClient::kClient2);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.SyncTurnOff();
  helper_.CheckAppNotInList(Site::kStandalone);
  helper_.SyncTurnOn();
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
}

IN_PROC_BROWSER_TEST_F(
    WebAppIntegration,
    WAI_CreateShortcutStandaloneBrowser_SwitchProfileClientsClient2_SyncTurnOff_SyncTurnOn) {
  // Test contents are generated by script. Please do not modify!
  // See `docs/webapps/why-is-this-test-failing.md` or
  // `docs/webapps/integration-testing-framework` for more info.
  // Gardeners: Disabling this test is supported.
  helper_.CreateShortcut(Site::kStandalone, WindowOptions::kBrowser);
  helper_.CheckAppTitle(Site::kStandalone, Title::kStandaloneOriginal);
  helper_.CheckAppInListTabbed(Site::kStandalone);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.SwitchProfileClients(ProfileClient::kClient2);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckAppInListTabbed(Site::kStandalone);
  helper_.SyncTurnOff();
  helper_.CheckAppNotInList(Site::kStandalone);
  helper_.SyncTurnOn();
  helper_.CheckAppInListTabbed(Site::kStandalone);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
}

IN_PROC_BROWSER_TEST_F(
    WebAppIntegration,
    WAI_InstallMenuStandalone_SwitchProfileClientsClient2_UninstallFromListStandalone_SwitchProfileClientsClient1) {
  // Test contents are generated by script. Please do not modify!
  // See `docs/webapps/why-is-this-test-failing.md` or
  // `docs/webapps/integration-testing-framework` for more info.
  // Gardeners: Disabling this test is supported.
  helper_.InstallMenuOption(Site::kStandalone);
  helper_.CheckAppTitle(Site::kStandalone, Title::kStandaloneOriginal);
  helper_.CheckWindowCreated();
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckWindowControlsOverlayToggle(Site::kStandalone,
                                           IsShown::kNotShown);
  helper_.SwitchProfileClients(ProfileClient::kClient2);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.UninstallFromList(Site::kStandalone);
  helper_.CheckAppNotInList(Site::kStandalone);
  helper_.SwitchProfileClients(ProfileClient::kClient1);
  helper_.CheckAppNotInList(Site::kStandalone);
}

IN_PROC_BROWSER_TEST_F(
    WebAppIntegration,
    WAI_InstallMenuStandalone_SwitchProfileClientsClient2_NavigateBrowserStandalone) {
  // Test contents are generated by script. Please do not modify!
  // See `docs/webapps/why-is-this-test-failing.md` or
  // `docs/webapps/integration-testing-framework` for more info.
  // Gardeners: Disabling this test is supported.
  helper_.InstallMenuOption(Site::kStandalone);
  helper_.CheckAppTitle(Site::kStandalone, Title::kStandaloneOriginal);
  helper_.CheckWindowCreated();
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckWindowControlsOverlayToggle(Site::kStandalone,
                                           IsShown::kNotShown);
  helper_.SwitchProfileClients(ProfileClient::kClient2);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.NavigateBrowser(Site::kStandalone);
  helper_.CheckInstallIconNotShown();
  helper_.CheckLaunchIconShown();
}

IN_PROC_BROWSER_TEST_F(
    WebAppIntegration,
    WAI_InstallMenuStandalone_SwitchProfileClientsClient2_SyncTurnOff_SyncTurnOn) {
  // Test contents are generated by script. Please do not modify!
  // See `docs/webapps/why-is-this-test-failing.md` or
  // `docs/webapps/integration-testing-framework` for more info.
  // Gardeners: Disabling this test is supported.
  helper_.InstallMenuOption(Site::kStandalone);
  helper_.CheckAppTitle(Site::kStandalone, Title::kStandaloneOriginal);
  helper_.CheckWindowCreated();
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckWindowControlsOverlayToggle(Site::kStandalone,
                                           IsShown::kNotShown);
  helper_.SwitchProfileClients(ProfileClient::kClient2);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.SyncTurnOff();
  helper_.CheckAppNotInList(Site::kStandalone);
  helper_.SyncTurnOn();
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
}

IN_PROC_BROWSER_TEST_F(
    WebAppIntegration,
    WAI_SyncTurnOff_CreateShortcutStandaloneWindowed_SyncTurnOn_SwitchProfileClientsClient2) {
  // Test contents are generated by script. Please do not modify!
  // See `docs/webapps/why-is-this-test-failing.md` or
  // `docs/webapps/integration-testing-framework` for more info.
  // Gardeners: Disabling this test is supported.
  helper_.SyncTurnOff();
  helper_.CreateShortcut(Site::kStandalone, WindowOptions::kWindowed);
  helper_.SyncTurnOn();
  helper_.SwitchProfileClients(ProfileClient::kClient2);
  helper_.CheckAppNotInList(Site::kStandalone);
}

IN_PROC_BROWSER_TEST_F(
    WebAppIntegration,
    WAI_SyncTurnOff_CreateShortcutStandaloneBrowser_SyncTurnOn_SwitchProfileClientsClient2) {
  // Test contents are generated by script. Please do not modify!
  // See `docs/webapps/why-is-this-test-failing.md` or
  // `docs/webapps/integration-testing-framework` for more info.
  // Gardeners: Disabling this test is supported.
  helper_.SyncTurnOff();
  helper_.CreateShortcut(Site::kStandalone, WindowOptions::kBrowser);
  helper_.SyncTurnOn();
  helper_.SwitchProfileClients(ProfileClient::kClient2);
  helper_.CheckAppNotInList(Site::kStandalone);
}

IN_PROC_BROWSER_TEST_F(
    WebAppIntegration,
    WAI_CreateShortcutStandaloneWindowed_SwitchProfileClientsClient2_UninstallFromListStandalone_SwitchProfileClientsClient1) {
  // Test contents are generated by script. Please do not modify!
  // See `docs/webapps/why-is-this-test-failing.md` or
  // `docs/webapps/integration-testing-framework` for more info.
  // Gardeners: Disabling this test is supported.
  helper_.CreateShortcut(Site::kStandalone, WindowOptions::kWindowed);
  helper_.CheckWindowCreated();
  helper_.CheckAppTitle(Site::kStandalone, Title::kStandaloneOriginal);
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckWindowControlsOverlayToggle(Site::kStandalone,
                                           IsShown::kNotShown);
  helper_.SwitchProfileClients(ProfileClient::kClient2);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.UninstallFromList(Site::kStandalone);
  helper_.CheckAppNotInList(Site::kStandalone);
  helper_.SwitchProfileClients(ProfileClient::kClient1);
  helper_.CheckAppNotInList(Site::kStandalone);
}

IN_PROC_BROWSER_TEST_F(
    WebAppIntegration,
    WAI_CreateShortcutStandaloneWindowed_SwitchProfileClientsClient2_NavigateBrowserStandalone) {
  // Test contents are generated by script. Please do not modify!
  // See `docs/webapps/why-is-this-test-failing.md` or
  // `docs/webapps/integration-testing-framework` for more info.
  // Gardeners: Disabling this test is supported.
  helper_.CreateShortcut(Site::kStandalone, WindowOptions::kWindowed);
  helper_.CheckWindowCreated();
  helper_.CheckAppTitle(Site::kStandalone, Title::kStandaloneOriginal);
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckWindowControlsOverlayToggle(Site::kStandalone,
                                           IsShown::kNotShown);
  helper_.SwitchProfileClients(ProfileClient::kClient2);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.NavigateBrowser(Site::kStandalone);
  helper_.CheckInstallIconNotShown();
  helper_.CheckLaunchIconShown();
}

IN_PROC_BROWSER_TEST_F(
    WebAppIntegration,
    WAI_CreateShortcutStandaloneWindowed_SwitchProfileClientsClient2_SyncTurnOff_SyncTurnOn) {
  // Test contents are generated by script. Please do not modify!
  // See `docs/webapps/why-is-this-test-failing.md` or
  // `docs/webapps/integration-testing-framework` for more info.
  // Gardeners: Disabling this test is supported.
  helper_.CreateShortcut(Site::kStandalone, WindowOptions::kWindowed);
  helper_.CheckWindowCreated();
  helper_.CheckAppTitle(Site::kStandalone, Title::kStandaloneOriginal);
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckWindowControlsOverlayToggle(Site::kStandalone,
                                           IsShown::kNotShown);
  helper_.SwitchProfileClients(ProfileClient::kClient2);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.SyncTurnOff();
  helper_.CheckAppNotInList(Site::kStandalone);
  helper_.SyncTurnOn();
  helper_.CheckAppInListWindowed(Site::kStandalone);
  helper_.CheckPlatformShortcutAndIcon(Site::kStandalone);
}

}  // namespace
}  // namespace web_app::integration_tests
