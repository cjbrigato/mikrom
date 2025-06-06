// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/constants/ash_features.h"
#include "ash/constants/web_app_id_constants.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "base/values.h"
#include "chrome/browser/apps/app_service/app_icon/app_icon_factory.h"
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/ash/crostini/fake_crostini_features.h"
#include "chrome/browser/ash/guest_os/guest_os_terminal.h"
#include "chrome/browser/ash/system_web_apps/system_web_app_manager.h"
#include "chrome/browser/ash/test/public_account_logged_in_browser_test_mixin.h"
#include "chrome/browser/ash/test/regular_logged_in_browser_test_mixin.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/policy/policy_test_utils.h"
#include "chrome/browser/policy/system_features_disable_list_policy_handler.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/web_applications/test/web_app_browsertest_util.h"
#include "chrome/browser/web_applications/test/web_app_install_test_utils.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/mixin_based_in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chromeos/constants/chromeos_features.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/common/policy_pref_names.h"
#include "components/policy/core/common/system_features_disable_list_constants.h"
#include "components/policy/policy_constants.h"
#include "components/services/app_service/public/cpp/app_types.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/constants.h"
#include "ui/base/l10n/l10n_util.h"

namespace policy {

namespace {
const char kCanvasAppURL[] = "https://canvas.apps.chrome/";
const char kCanvasAppTitle[] = "canvas.apps.chrome";
const char kWebStoreExtensionURL[] = "https://chrome.google.com/webstore/";
const char kWebStoreExtensionTitle[] = "chrome.google.com";

constexpr GaiaId::Literal kFakeGaiaId("1234567890");
constexpr char kUserEmail[] = "test@example.com";
constexpr char kPublicAccountId[] = "public-account";

struct VisibilityFlags {
  bool show_in_search;
  bool show_in_launcher;
  bool show_in_shelf;
};

}  // namespace

class SystemFeaturesPolicyTestBase : public MixinBasedInProcessBrowserTest {
 public:
  SystemFeaturesPolicyTestBase() {
    scoped_feature_list_.InitWithFeatures(
        /*enabled_features=*/{ash::features::kEcheSWA, ash::features::kConch,
                              chromeos::features::
                                  kSystemFeaturesDisableListHidden},
        /*disabled_features=*/{});

    fake_crostini_features_.set_is_allowed_now(true);
  }

  SystemFeaturesPolicyTestBase(const SystemFeaturesPolicyTestBase&) = delete;
  SystemFeaturesPolicyTestBase& operator=(const SystemFeaturesPolicyTestBase&) =
      delete;

  ~SystemFeaturesPolicyTestBase() override = default;

  void SetUpInProcessBrowserTestFixture() override {
    MixinBasedInProcessBrowserTest::SetUpInProcessBrowserTestFixture();
    provider_.SetDefaultReturns(
        /*is_initialization_complete_return=*/true,
        /*is_first_policy_load_complete_return=*/true);

    policy::BrowserPolicyConnector::SetPolicyProviderForTesting(&provider_);
  }

 protected:
  std::u16string GetWebUITitle(const GURL& url,
                               bool using_navigation_throttle) {
    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
    if (using_navigation_throttle) {
      content::WaitForLoadStopWithoutSuccessCheck(web_contents);
    } else {
      EXPECT_TRUE(content::WaitForLoadStop(web_contents));
    }
    return web_contents->GetTitle();
  }

  void EnableExtensions(bool skip_session_components) {
    auto* profile = browser()->profile();
    extensions::ComponentLoader::EnableBackgroundExtensionsForTesting();
    extensions::ComponentLoader::Get(profile)->AddDefaultComponentExtensions(
        skip_session_components);
    base::RunLoop().RunUntilIdle();
  }

  // Disables specified system features or enables all if system_features is
  // empty. Updates disabled mode for disabled system features.
  void UpdateSystemFeaturesDisableList(base::Value system_features,
                                       const char* disabled_mode) {
    PolicyMap policies;
    policies.Set(key::kSystemFeaturesDisableList, POLICY_LEVEL_MANDATORY,
                 POLICY_SCOPE_USER, POLICY_SOURCE_CLOUD,
                 std::move(system_features), nullptr);
    if (disabled_mode) {
      policies.Set(key::kSystemFeaturesDisableMode, POLICY_LEVEL_MANDATORY,
                   POLICY_SCOPE_USER, POLICY_SOURCE_CLOUD,
                   base::Value(disabled_mode), nullptr);
    }
    // TODO(280518509): Remove this workaround once multidevice code
    // supports runtime policy updates.
    policies.Set(key::kPhoneHubAllowed, POLICY_LEVEL_MANDATORY,
                 POLICY_SCOPE_USER, POLICY_SOURCE_CLOUD, base::Value(true),
                 nullptr);
    provider_.UpdateChromePolicy(policies);
  }

  // Convenience overload of UpdateSystemFeaturesDisableList() that allows
  // callers to provide a base::Value::List instead of a base::Value.
  void UpdateSystemFeaturesDisableList(base::Value::List system_features,
                                       const char* disabled_mode) {
    UpdateSystemFeaturesDisableList(base::Value(std::move(system_features)),
                                    disabled_mode);
  }

  void VerifyExtensionAppState(const char* app_id,
                               apps::Readiness expected_readiness,
                               bool blocked_icon,
                               const VisibilityFlags& expected_visibility) {
    auto* profile = browser()->profile();
    extensions::ExtensionRegistry* registry =
        extensions::ExtensionRegistry::Get(profile);
    ASSERT_TRUE(registry->enabled_extensions().GetByID(app_id));
    VerifyAppState(app_id, expected_readiness, blocked_icon,
                   expected_visibility);
  }

  void VerifyAppState(const char* app_id,
                      apps::Readiness expected_readiness,
                      bool blocked_icon,
                      const VisibilityFlags& expected_visibility) {
    auto* profile = browser()->profile();
    auto* proxy = apps::AppServiceProxyFactory::GetForProfile(profile);

    bool exist = proxy->AppRegistryCache().ForOneApp(
        app_id, [&expected_readiness, &blocked_icon,
                 &expected_visibility](const apps::AppUpdate& update) {
          EXPECT_EQ(expected_readiness, update.Readiness());
          if (blocked_icon) {
            EXPECT_TRUE(apps::IconEffects::kBlocked &
                        update.IconKey()->icon_effects);
          } else {
            EXPECT_FALSE(apps::IconEffects::kBlocked &
                         update.IconKey()->icon_effects);
          }
          EXPECT_EQ(expected_visibility.show_in_launcher,
                    update.ShowInLauncher());
          EXPECT_EQ(expected_visibility.show_in_search, update.ShowInSearch());
          EXPECT_EQ(expected_visibility.show_in_shelf, update.ShowInShelf());
        });
    EXPECT_TRUE(exist);
  }

  void InstallSWAs() {
    ash::SystemWebAppManager::GetForTest(browser()->profile())
        ->InstallSystemAppsForTesting();
  }

  void InstallPWA(const GURL& app_url, const char* app_id) {
    auto web_app_info =
        web_app::WebAppInstallInfo::CreateWithStartUrlForTesting(app_url);
    web_app_info->scope = app_url.GetWithoutFilename();
    webapps::AppId installed_app_id = web_app::test::InstallWebApp(
        browser()->profile(), std::move(web_app_info));
    EXPECT_EQ(app_id, installed_app_id);
  }

  VisibilityFlags GetVisibilityFlags(bool is_hidden) {
    VisibilityFlags flags;
    if (is_hidden) {
      flags.show_in_launcher = false;
      flags.show_in_search = false;
      flags.show_in_shelf = false;
      return flags;
    }
    flags.show_in_launcher = true;
    flags.show_in_search = true;
    flags.show_in_shelf = true;
    return flags;
  }

  virtual void VerifyAppDisableMode(const char* app_id,
                                    const char* feature) = 0;

  // Used for non-link-capturing PWAs.
  void VerifyIsAppURLDisabled(const char* app_id,
                              const char* feature,
                              const char* url,
                              const char* app_title) {
    const GURL& app_url = GURL(url);

    // The URL navigation is still allowed because the app is not installed,
    // though it is disabled by policy.
    base::Value::List system_features;
    system_features.Append(feature);
    UpdateSystemFeaturesDisableList(std::move(system_features), nullptr);
    EXPECT_EQ(base::UTF8ToUTF16(app_title), GetWebUITitle(app_url, true));

    // Install the app.
    InstallPWA(app_url, app_id);
    EXPECT_EQ(l10n_util::GetStringUTF16(IDS_CHROME_URLS_DISABLED_PAGE_HEADER),
              GetWebUITitle(app_url, true));

    // Enable the app by removing it from the policy of disabled apps.
    UpdateSystemFeaturesDisableList(base::Value(), nullptr);
    EXPECT_EQ(base::UTF8ToUTF16(app_title), GetWebUITitle(app_url, true));
  }

  void VerifyIsExtensionAppURLAccessible(const char* url,
                                         const char* app_title) {
    const GURL& app_url = GURL(url);
    EXPECT_EQ(base::UTF8ToUTF16(app_title), GetWebUITitle(app_url, true));
  }

 private:
  testing::NiceMock<policy::MockConfigurationPolicyProvider> provider_;

  base::test::ScopedFeatureList scoped_feature_list_;

  // Fake the Crostini feature to have the Terminal app icon show in the
  // launcher when installed.
  crostini::FakeCrostiniFeatures fake_crostini_features_;
};

// Tests the behavior of SystemFeaturesDisableList and SystemFeaturesDisableMode
// policies in regular user sessions.
class SystemFeaturesPolicyTest : public SystemFeaturesPolicyTestBase {
 public:
  SystemFeaturesPolicyTest()
      : account_id_(AccountId::FromUserEmailGaiaId(kUserEmail, kFakeGaiaId)) {}

 protected:
  void VerifyAppDisableMode(const char* app_id, const char* feature) override {
    base::Value::List system_features;
    system_features.Append(feature);
    VisibilityFlags expected_visibility =
        GetVisibilityFlags(true /* is_hidden */);
    // Disable app with default mode (hidden).
    UpdateSystemFeaturesDisableList(system_features.Clone(), nullptr);
    VerifyAppState(app_id, apps::Readiness::kDisabledByPolicy, true,
                   expected_visibility);
    // Disable and block app.
    expected_visibility = GetVisibilityFlags(false /* is_hidden */);
    UpdateSystemFeaturesDisableList(system_features.Clone(),
                                    kSystemFeaturesDisableModeBlocked);
    VerifyAppState(app_id, apps::Readiness::kDisabledByPolicy, true,
                   expected_visibility);
    // Disable and hide app.
    expected_visibility = GetVisibilityFlags(true /* is_hidden */);
    UpdateSystemFeaturesDisableList(system_features.Clone(),
                                    kSystemFeaturesDisableModeHidden);
    VerifyAppState(app_id, apps::Readiness::kDisabledByPolicy, true,
                   expected_visibility);
    // Enable app.
    UpdateSystemFeaturesDisableList(base::Value(), nullptr);
    expected_visibility = GetVisibilityFlags(false /* is_hidden */);
    VerifyAppState(app_id, apps::Readiness::kReady, false, expected_visibility);
  }

  // Used for non-link-capturing PWAs.
  void VerifyIsAppURLDisabled(const char* app_id,
                              const char* feature,
                              const char* url,
                              const char* app_title) {
    const GURL& app_url = GURL(url);

    // The URL navigation is still allowed because the app is not installed,
    // though it is disabled by policy.
    base::Value::List system_features;
    system_features.Append(feature);
    UpdateSystemFeaturesDisableList(std::move(system_features), nullptr);
    EXPECT_EQ(base::UTF8ToUTF16(app_title), GetWebUITitle(app_url, true));

    // Install the app.
    InstallPWA(app_url, app_id);
    EXPECT_EQ(l10n_util::GetStringUTF16(IDS_CHROME_URLS_DISABLED_PAGE_HEADER),
              GetWebUITitle(app_url, true));

    // Enable the app by removing it from the policy of disabled apps.
    UpdateSystemFeaturesDisableList(base::Value(), nullptr);
    EXPECT_EQ(base::UTF8ToUTF16(app_title), GetWebUITitle(app_url, true));
  }

  const AccountId account_id_;
  ash::RegularLoggedInBrowserTestMixin logged_in_mixin_{&mixin_host_,
                                                        account_id_};
};

IN_PROC_BROWSER_TEST_F(SystemFeaturesPolicyTest, DisableWebStoreBeforeInstall) {
  base::Value::List system_features;
  system_features.Append(kWebStoreFeature);
  VisibilityFlags expected_visibility =
      GetVisibilityFlags(true /* is_hidden */);
  UpdateSystemFeaturesDisableList(std::move(system_features), nullptr);
  EnableExtensions(true);
  VerifyExtensionAppState(extensions::kWebStoreAppId,
                          apps::Readiness::kDisabledByPolicy, true,
                          expected_visibility);
  // The URL navigation should still be possible
  // even if the app is disabled by policy.
  VerifyIsExtensionAppURLAccessible(kWebStoreExtensionURL,
                                    kWebStoreExtensionTitle);

  UpdateSystemFeaturesDisableList(base::Value(), nullptr);
  expected_visibility = GetVisibilityFlags(false /* is_hidden */);
  VerifyExtensionAppState(extensions::kWebStoreAppId, apps::Readiness::kReady,
                          false, expected_visibility);
  VerifyIsExtensionAppURLAccessible(kWebStoreExtensionURL,
                                    kWebStoreExtensionTitle);
}

IN_PROC_BROWSER_TEST_F(SystemFeaturesPolicyTest,
                       DisableWebStoreAfterInstallWithModes) {
  EnableExtensions(false);
  base::Value::List system_features;
  system_features.Append(kWebStoreFeature);
  VisibilityFlags expected_visibility =
      GetVisibilityFlags(true /* is_hidden */);
  // Disable app with default mode (hidden).
  // The URL navigation should still be possible
  // even if the app is disabled by policy.
  UpdateSystemFeaturesDisableList(system_features.Clone(), nullptr);
  VerifyExtensionAppState(extensions::kWebStoreAppId,
                          apps::Readiness::kDisabledByPolicy, true,
                          expected_visibility);
  VerifyIsExtensionAppURLAccessible(kWebStoreExtensionURL,
                                    kWebStoreExtensionTitle);
  // Disable and block app.
  expected_visibility = GetVisibilityFlags(false /* is_hidden */);
  UpdateSystemFeaturesDisableList(system_features.Clone(),
                                  kSystemFeaturesDisableModeBlocked);
  VerifyExtensionAppState(extensions::kWebStoreAppId,
                          apps::Readiness::kDisabledByPolicy, true,
                          expected_visibility);
  VerifyIsExtensionAppURLAccessible(kWebStoreExtensionURL,
                                    kWebStoreExtensionTitle);

  // Disable and hide app.
  expected_visibility = GetVisibilityFlags(true /* is_hidden */);
  UpdateSystemFeaturesDisableList(system_features.Clone(),
                                  kSystemFeaturesDisableModeHidden);
  VerifyExtensionAppState(extensions::kWebStoreAppId,
                          apps::Readiness::kDisabledByPolicy, true,
                          expected_visibility);
  VerifyIsExtensionAppURLAccessible(kWebStoreExtensionURL,
                                    kWebStoreExtensionTitle);
  // Enable app
  expected_visibility = GetVisibilityFlags(false /* is_hidden */);
  UpdateSystemFeaturesDisableList(base::Value(), nullptr);
  VerifyExtensionAppState(extensions::kWebStoreAppId, apps::Readiness::kReady,
                          false, expected_visibility);
  VerifyIsExtensionAppURLAccessible(kWebStoreExtensionURL,
                                    kWebStoreExtensionTitle);
}

IN_PROC_BROWSER_TEST_F(SystemFeaturesPolicyTest, DisableSWAs) {
  InstallSWAs();

  // Disable Camera app.
  VerifyAppDisableMode(ash::kCameraAppId, kCameraFeature);

  // Disable Explore app.
  VerifyAppDisableMode(ash::kHelpAppId, kExploreFeature);

  // Disable Gallery app.
  VerifyAppDisableMode(ash::kMediaAppId, kGalleryFeature);

  // Disable Terminal app.
  VerifyAppDisableMode(guest_os::kTerminalSystemAppId, kTerminalFeature);

  // Disable Print Jobs app.
  VerifyAppDisableMode(ash::kPrintManagementAppId, kPrintJobsFeature);

  // Disable Key Shortcuts app.
  VerifyAppDisableMode(ash::kShortcutCustomizationAppId, kKeyShortcutsFeature);

  // Disable Recorder app.
  VerifyAppDisableMode(ash::kRecorderAppId, kRecorderFeature);
}

IN_PROC_BROWSER_TEST_F(SystemFeaturesPolicyTest,
                       DisableMultipleAppsWithBlockedModeAfterInstall) {
  InstallSWAs();
  InstallPWA(GURL(kCanvasAppURL), ash::kCanvasAppId);

  // Disable app with hidden mode.
  const base::Value::List system_features = base::Value::List()
                                                .Append(kCameraFeature)
                                                .Append(kScanningFeature)
                                                .Append(kWebStoreFeature)
                                                .Append(kCanvasFeature)
                                                .Append(kCroshFeature)
                                                .Append(kGalleryFeature)
                                                .Append(kTerminalFeature)
                                                .Append(kPrintJobsFeature)
                                                .Append(kKeyShortcutsFeature)
                                                .Append(kRecorderFeature);
  UpdateSystemFeaturesDisableList(system_features.Clone(),
                                  kSystemFeaturesDisableModeHidden);

  VisibilityFlags expected_visibility =
      GetVisibilityFlags(true /* is_hidden */);
  VerifyAppState(ash::kCameraAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(ash::kScanningAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyExtensionAppState(extensions::kWebStoreAppId,
                          apps::Readiness::kDisabledByPolicy, true,
                          expected_visibility);
  VerifyAppState(ash::kCanvasAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(ash::kCroshAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(ash::kMediaAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(guest_os::kTerminalSystemAppId,
                 apps::Readiness::kDisabledByPolicy, true, expected_visibility);
  VerifyAppState(ash::kPrintManagementAppId, apps::Readiness::kDisabledByPolicy,
                 true, expected_visibility);
  VerifyAppState(ash::kShortcutCustomizationAppId,
                 apps::Readiness::kDisabledByPolicy, true, expected_visibility);
  VerifyAppState(ash::kRecorderAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);

  // Disable and block apps.
  expected_visibility = GetVisibilityFlags(false /* is_hidden */);
  // Crosh is never shown.
  VisibilityFlags crosh_expected_visibility =
      GetVisibilityFlags(true /* is_hidden */);
  UpdateSystemFeaturesDisableList(system_features.Clone(),
                                  kSystemFeaturesDisableModeBlocked);

  VerifyAppState(ash::kCameraAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(ash::kScanningAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyExtensionAppState(extensions::kWebStoreAppId,
                          apps::Readiness::kDisabledByPolicy, true,
                          expected_visibility);
  VerifyAppState(ash::kCanvasAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(ash::kCroshAppId, apps::Readiness::kDisabledByPolicy, true,
                 crosh_expected_visibility);
  VerifyAppState(ash::kMediaAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(guest_os::kTerminalSystemAppId,
                 apps::Readiness::kDisabledByPolicy, true, expected_visibility);
  VerifyAppState(ash::kPrintManagementAppId, apps::Readiness::kDisabledByPolicy,
                 true, expected_visibility);
  VerifyAppState(ash::kShortcutCustomizationAppId,
                 apps::Readiness::kDisabledByPolicy, true, expected_visibility);
  VerifyAppState(ash::kRecorderAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);

  // Enable apps.
  UpdateSystemFeaturesDisableList(base::Value(), nullptr);
  VerifyAppState(ash::kCameraAppId, apps::Readiness::kReady, false,
                 expected_visibility);
  VerifyAppState(ash::kScanningAppId, apps::Readiness::kReady, false,
                 expected_visibility);
  VerifyExtensionAppState(extensions::kWebStoreAppId, apps::Readiness::kReady,
                          false, expected_visibility);
  VerifyAppState(ash::kCanvasAppId, apps::Readiness::kReady, false,
                 expected_visibility);
  VerifyAppState(ash::kCroshAppId, apps::Readiness::kReady, false,
                 crosh_expected_visibility);
  VerifyAppState(ash::kMediaAppId, apps::Readiness::kReady, false,
                 expected_visibility);
  VerifyAppState(guest_os::kTerminalSystemAppId, apps::Readiness::kReady, false,
                 expected_visibility);
  VerifyAppState(ash::kPrintManagementAppId, apps::Readiness::kReady, false,
                 expected_visibility);
  VerifyAppState(ash::kShortcutCustomizationAppId, apps::Readiness::kReady,
                 false, expected_visibility);
  VerifyAppState(ash::kRecorderAppId, apps::Readiness::kReady, false,
                 expected_visibility);
}

IN_PROC_BROWSER_TEST_F(SystemFeaturesPolicyTest,
                       DisableMultipleAppsWithBlockedModeBeforeInstall) {
  const base::Value::List system_features = base::Value::List()
                                                .Append(kCameraFeature)
                                                .Append(kScanningFeature)
                                                .Append(kWebStoreFeature)
                                                .Append(kCanvasFeature)
                                                .Append(kCroshFeature)
                                                .Append(kGalleryFeature)
                                                .Append(kTerminalFeature)
                                                .Append(kPrintJobsFeature)
                                                .Append(kKeyShortcutsFeature)
                                                .Append(kRecorderFeature);
  UpdateSystemFeaturesDisableList(system_features.Clone(),
                                  kSystemFeaturesDisableModeHidden);

  InstallSWAs();
  InstallPWA(GURL(kCanvasAppURL), ash::kCanvasAppId);

  VisibilityFlags expected_visibility =
      GetVisibilityFlags(true /* is_hidden */);

  // Disable app with hidden mode.
  VerifyAppState(ash::kCameraAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(ash::kScanningAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyExtensionAppState(extensions::kWebStoreAppId,
                          apps::Readiness::kDisabledByPolicy, true,
                          expected_visibility);
  VerifyAppState(ash::kCanvasAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(ash::kCroshAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(ash::kMediaAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(guest_os::kTerminalSystemAppId,
                 apps::Readiness::kDisabledByPolicy, true, expected_visibility);
  VerifyAppState(ash::kPrintManagementAppId, apps::Readiness::kDisabledByPolicy,
                 true, expected_visibility);
  VerifyAppState(ash::kShortcutCustomizationAppId,
                 apps::Readiness::kDisabledByPolicy, true, expected_visibility);
  VerifyAppState(ash::kRecorderAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
}

IN_PROC_BROWSER_TEST_F(SystemFeaturesPolicyTest, RedirectChromeSettingsURL) {
  base::Value::List system_features;
  system_features.Append(kBrowserSettingsFeature);
  UpdateSystemFeaturesDisableList(std::move(system_features), nullptr);

  GURL settings_url = GURL(chrome::kChromeUISettingsURL);
  EXPECT_EQ(l10n_util::GetStringUTF16(IDS_CHROME_URLS_DISABLED_PAGE_HEADER),
            GetWebUITitle(settings_url, false));

  UpdateSystemFeaturesDisableList(base::Value(), nullptr);
  EXPECT_EQ(l10n_util::GetStringUTF16(IDS_SETTINGS_SETTINGS),
            GetWebUITitle(settings_url, false));
}

IN_PROC_BROWSER_TEST_F(SystemFeaturesPolicyTest, RedirectCroshURL) {
  base::Value::List system_features;
  system_features.Append(kCroshFeature);
  UpdateSystemFeaturesDisableList(std::move(system_features), nullptr);

  GURL crosh_url = GURL(chrome::kChromeUIUntrustedCroshURL);
  EXPECT_EQ(l10n_util::GetStringUTF16(IDS_CHROME_URLS_DISABLED_PAGE_HEADER),
            GetWebUITitle(crosh_url, false));

  UpdateSystemFeaturesDisableList(base::Value(), nullptr);
  // Title is empty for untrusted URLs.
  EXPECT_EQ(std::u16string(), GetWebUITitle(crosh_url, false));
}

IN_PROC_BROWSER_TEST_F(SystemFeaturesPolicyTest, DisablePWAs) {
  // Disable Canvas app.
  VerifyIsAppURLDisabled(ash::kCanvasAppId, kCanvasFeature, kCanvasAppURL,
                         kCanvasAppTitle);
  VerifyAppDisableMode(ash::kCanvasAppId, kCanvasFeature);
}

// Tests the behavior of SystemFeaturesDisableList and SystemFeaturesDisableMode
// policies in MGS sessions.
class MgsSystemFeaturesPolicyTest : public SystemFeaturesPolicyTestBase {
 public:
  MgsSystemFeaturesPolicyTest() = default;

  MgsSystemFeaturesPolicyTest(const MgsSystemFeaturesPolicyTest&) = delete;
  MgsSystemFeaturesPolicyTest& operator=(const MgsSystemFeaturesPolicyTest&) =
      delete;

  ~MgsSystemFeaturesPolicyTest() override = default;

  void VerifyAppDisableMode(const char* app_id, const char* feature) override {
    base::Value::List system_features;
    system_features.Append(feature);
    VisibilityFlags expected_visibility =
        GetVisibilityFlags(false /* is_hidden */);
    // Disable app with default mode (hidden).
    UpdateSystemFeaturesDisableList(system_features.Clone(), nullptr);
    VerifyAppState(app_id, apps::Readiness::kDisabledByPolicy, true,
                   expected_visibility);
    // Disable and hide app.
    expected_visibility = GetVisibilityFlags(true /* is_hidden */);
    UpdateSystemFeaturesDisableList(system_features.Clone(),
                                    kSystemFeaturesDisableModeHidden);
    VerifyAppState(app_id, apps::Readiness::kDisabledByPolicy, true,
                   expected_visibility);
    // Disable and block app.
    expected_visibility = GetVisibilityFlags(false /* is_hidden */);
    UpdateSystemFeaturesDisableList(system_features.Clone(),
                                    kSystemFeaturesDisableModeBlocked);
    VerifyAppState(app_id, apps::Readiness::kDisabledByPolicy, true,
                   expected_visibility);
    // Enable app.
    UpdateSystemFeaturesDisableList(base::Value(), nullptr);
    VerifyAppState(app_id, apps::Readiness::kReady, false, expected_visibility);
  }

 private:
  ash::PublicAccountLoggedInBrowserTestMixin logged_in_mixin_{&mixin_host_,
                                                              kPublicAccountId};
};

IN_PROC_BROWSER_TEST_F(MgsSystemFeaturesPolicyTest,
                       DisableWebStoreBeforeInstall) {
  base::Value::List system_features;
  system_features.Append(kWebStoreFeature);
  VisibilityFlags expected_visibility =
      GetVisibilityFlags(false /* is_hidden */);
  UpdateSystemFeaturesDisableList(std::move(system_features), nullptr);
  EnableExtensions(true);
  VerifyExtensionAppState(extensions::kWebStoreAppId,
                          apps::Readiness::kDisabledByPolicy, true,
                          expected_visibility);
  // The URL navigation should still be possible
  // even if the app is disabled by policy.
  VerifyIsExtensionAppURLAccessible(kWebStoreExtensionURL,
                                    kWebStoreExtensionTitle);

  UpdateSystemFeaturesDisableList(base::Value(), nullptr);
  // expected_visibility = GetVisibilityFlags(false /* is_hidden */);
  VerifyExtensionAppState(extensions::kWebStoreAppId, apps::Readiness::kReady,
                          false, expected_visibility);
  VerifyIsExtensionAppURLAccessible(kWebStoreExtensionURL,
                                    kWebStoreExtensionTitle);
}

IN_PROC_BROWSER_TEST_F(MgsSystemFeaturesPolicyTest,
                       DisableWebStoreAfterInstallWithModes) {
  EnableExtensions(false);
  base::Value::List system_features;
  system_features.Append(kWebStoreFeature);
  VisibilityFlags expected_visibility =
      GetVisibilityFlags(false /* is_hidden */);
  // Disable app with default mode (blocked).
  // The URL navigation should still be possible
  // even if the app is disabled by policy.
  UpdateSystemFeaturesDisableList(system_features.Clone(), nullptr);
  VerifyExtensionAppState(extensions::kWebStoreAppId,
                          apps::Readiness::kDisabledByPolicy, true,
                          expected_visibility);
  VerifyIsExtensionAppURLAccessible(kWebStoreExtensionURL,
                                    kWebStoreExtensionTitle);

  // Disable and hide app.
  expected_visibility = GetVisibilityFlags(true /* is_hidden */);
  UpdateSystemFeaturesDisableList(system_features.Clone(),
                                  kSystemFeaturesDisableModeHidden);
  VerifyExtensionAppState(extensions::kWebStoreAppId,
                          apps::Readiness::kDisabledByPolicy, true,
                          expected_visibility);
  VerifyIsExtensionAppURLAccessible(kWebStoreExtensionURL,
                                    kWebStoreExtensionTitle);

  // Disable and block app.
  expected_visibility = GetVisibilityFlags(false /* is_hidden */);
  UpdateSystemFeaturesDisableList(system_features.Clone(),
                                  kSystemFeaturesDisableModeBlocked);
  VerifyExtensionAppState(extensions::kWebStoreAppId,
                          apps::Readiness::kDisabledByPolicy, true,
                          expected_visibility);
  VerifyIsExtensionAppURLAccessible(kWebStoreExtensionURL,
                                    kWebStoreExtensionTitle);

  // Enable app
  UpdateSystemFeaturesDisableList(base::Value(), nullptr);
  VerifyExtensionAppState(extensions::kWebStoreAppId, apps::Readiness::kReady,
                          false, expected_visibility);
  VerifyIsExtensionAppURLAccessible(kWebStoreExtensionURL,
                                    kWebStoreExtensionTitle);
}

IN_PROC_BROWSER_TEST_F(MgsSystemFeaturesPolicyTest, DisableSWAs) {
  InstallSWAs();

  // Disable Camera app.
  VerifyAppDisableMode(ash::kCameraAppId, kCameraFeature);

  // Disable Explore app.
  VerifyAppDisableMode(ash::kHelpAppId, kExploreFeature);

  // Disable Gallery app.
  VerifyAppDisableMode(ash::kMediaAppId, kGalleryFeature);

  // Disable Terminal app.
  VerifyAppDisableMode(guest_os::kTerminalSystemAppId, kTerminalFeature);

  // Disable Print Jobs app.
  VerifyAppDisableMode(ash::kPrintManagementAppId, kPrintJobsFeature);

  // Disable Key Shortcuts app.
  VerifyAppDisableMode(ash::kShortcutCustomizationAppId, kKeyShortcutsFeature);

  // Disable Recorder app.
  VerifyAppDisableMode(ash::kRecorderAppId, kRecorderFeature);
}

IN_PROC_BROWSER_TEST_F(MgsSystemFeaturesPolicyTest,
                       DisableMultipleAppsWithHiddenModeAfterInstall) {
  InstallSWAs();
  InstallPWA(GURL(kCanvasAppURL), ash::kCanvasAppId);

  // Disable app with hidden mode.
  const base::Value::List system_features = base::Value::List()
                                                .Append(kCameraFeature)
                                                .Append(kScanningFeature)
                                                .Append(kWebStoreFeature)
                                                .Append(kCanvasFeature)
                                                .Append(kCroshFeature)
                                                .Append(kGalleryFeature)
                                                .Append(kTerminalFeature)
                                                .Append(kPrintJobsFeature)
                                                .Append(kKeyShortcutsFeature)
                                                .Append(kRecorderFeature);
  UpdateSystemFeaturesDisableList(system_features.Clone(),
                                  kSystemFeaturesDisableModeHidden);

  VisibilityFlags expected_visibility =
      GetVisibilityFlags(true /* is_hidden */);
  VerifyAppState(ash::kCameraAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(ash::kScanningAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyExtensionAppState(extensions::kWebStoreAppId,
                          apps::Readiness::kDisabledByPolicy, true,
                          expected_visibility);
  VerifyAppState(ash::kCanvasAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(ash::kCroshAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(ash::kMediaAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(guest_os::kTerminalSystemAppId,
                 apps::Readiness::kDisabledByPolicy, true, expected_visibility);
  VerifyAppState(ash::kPrintManagementAppId, apps::Readiness::kDisabledByPolicy,
                 true, expected_visibility);
  VerifyAppState(ash::kShortcutCustomizationAppId,
                 apps::Readiness::kDisabledByPolicy, true, expected_visibility);
  VerifyAppState(ash::kRecorderAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);

  // Disable and block apps.
  expected_visibility = GetVisibilityFlags(false /* is_hidden */);
  // Crosh is never shown.
  VisibilityFlags crosh_expected_visibility =
      GetVisibilityFlags(true /* is_hidden */);
  UpdateSystemFeaturesDisableList(system_features.Clone(),
                                  kSystemFeaturesDisableModeBlocked);

  VerifyAppState(ash::kCameraAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(ash::kScanningAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyExtensionAppState(extensions::kWebStoreAppId,
                          apps::Readiness::kDisabledByPolicy, true,
                          expected_visibility);
  VerifyAppState(ash::kCanvasAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(ash::kCroshAppId, apps::Readiness::kDisabledByPolicy, true,
                 crosh_expected_visibility);
  VerifyAppState(ash::kMediaAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(guest_os::kTerminalSystemAppId,
                 apps::Readiness::kDisabledByPolicy, true, expected_visibility);
  VerifyAppState(ash::kPrintManagementAppId, apps::Readiness::kDisabledByPolicy,
                 true, expected_visibility);
  VerifyAppState(ash::kShortcutCustomizationAppId,
                 apps::Readiness::kDisabledByPolicy, true, expected_visibility);
  VerifyAppState(ash::kRecorderAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);

  // Enable apps.
  UpdateSystemFeaturesDisableList(base::Value(), nullptr);
  VerifyAppState(ash::kCameraAppId, apps::Readiness::kReady, false,
                 expected_visibility);
  VerifyAppState(ash::kScanningAppId, apps::Readiness::kReady, false,
                 expected_visibility);
  VerifyExtensionAppState(extensions::kWebStoreAppId, apps::Readiness::kReady,
                          false, expected_visibility);
  VerifyAppState(ash::kCanvasAppId, apps::Readiness::kReady, false,
                 expected_visibility);
  VerifyAppState(ash::kCroshAppId, apps::Readiness::kReady, false,
                 crosh_expected_visibility);
  VerifyAppState(ash::kMediaAppId, apps::Readiness::kReady, false,
                 expected_visibility);
  VerifyAppState(guest_os::kTerminalSystemAppId, apps::Readiness::kReady, false,
                 expected_visibility);
  VerifyAppState(ash::kPrintManagementAppId, apps::Readiness::kReady, false,
                 expected_visibility);
  VerifyAppState(ash::kShortcutCustomizationAppId, apps::Readiness::kReady,
                 false, expected_visibility);
  VerifyAppState(ash::kRecorderAppId, apps::Readiness::kReady, false,
                 expected_visibility);
}

IN_PROC_BROWSER_TEST_F(MgsSystemFeaturesPolicyTest,
                       DisableMultipleAppsWithHiddenModeBeforeInstall) {
  const base::Value::List system_features = base::Value::List()
                                                .Append(kCameraFeature)
                                                .Append(kScanningFeature)
                                                .Append(kWebStoreFeature)
                                                .Append(kCanvasFeature)
                                                .Append(kCroshFeature)
                                                .Append(kGalleryFeature)
                                                .Append(kTerminalFeature)
                                                .Append(kPrintJobsFeature)
                                                .Append(kKeyShortcutsFeature)
                                                .Append(kRecorderFeature);
  UpdateSystemFeaturesDisableList(system_features.Clone(),
                                  kSystemFeaturesDisableModeHidden);

  InstallSWAs();
  InstallPWA(GURL(kCanvasAppURL), ash::kCanvasAppId);

  VisibilityFlags expected_visibility =
      GetVisibilityFlags(true /* is_hidden */);

  // Disable app with hidden mode.
  VerifyAppState(ash::kCameraAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(ash::kScanningAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyExtensionAppState(extensions::kWebStoreAppId,
                          apps::Readiness::kDisabledByPolicy, true,
                          expected_visibility);
  VerifyAppState(ash::kCanvasAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(ash::kCroshAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(ash::kMediaAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
  VerifyAppState(guest_os::kTerminalSystemAppId,
                 apps::Readiness::kDisabledByPolicy, true, expected_visibility);
  VerifyAppState(ash::kPrintManagementAppId, apps::Readiness::kDisabledByPolicy,
                 true, expected_visibility);
  VerifyAppState(ash::kShortcutCustomizationAppId,
                 apps::Readiness::kDisabledByPolicy, true, expected_visibility);
  VerifyAppState(ash::kRecorderAppId, apps::Readiness::kDisabledByPolicy, true,
                 expected_visibility);
}

}  // namespace policy
