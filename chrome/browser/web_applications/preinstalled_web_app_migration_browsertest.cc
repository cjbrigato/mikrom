// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/auto_reset.h"
#include "base/files/file_path.h"
#include "base/json/json_reader.h"
#include "base/metrics/histogram_base.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/test_future.h"
#include "build/build_config.h"
#include "chrome/browser/apps/app_service/app_registry_cache_waiter.h"
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/external_provider_impl.h"
#include "chrome/browser/extensions/external_provider_manager.h"
#include "chrome/browser/extensions/external_testing_loader.h"
#include "chrome/browser/extensions/launch_util.h"
#include "chrome/browser/extensions/updater/extension_updater.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/web_applications/test/ssl_test_utils.h"
#include "chrome/browser/web_applications/extension_status_utils.h"
#include "chrome/browser/web_applications/mojom/user_display_mode.mojom.h"
#include "chrome/browser/web_applications/preinstalled_app_install_features.h"
#include "chrome/browser/web_applications/preinstalled_web_app_manager.h"
#include "chrome/browser/web_applications/preinstalled_web_apps/preinstalled_web_apps.h"
#include "chrome/browser/web_applications/proto/web_app_install_state.pb.h"
#include "chrome/browser/web_applications/test/os_integration_test_override_impl.h"
#include "chrome/browser/web_applications/test/web_app_install_test_utils.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/browser/web_applications/web_app_helpers.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/services/app_service/public/cpp/app_types.h"
#include "components/services/app_service/public/cpp/types_util.h"
#include "components/webapps/browser/uninstall_result_code.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/url_loader_interceptor.h"
#include "extensions/browser/app_sorting.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/test_extension_registry_observer.h"
#include "extensions/browser/updater/extension_cache_fake.h"
#include "extensions/browser/updater/extension_downloader_test_helper.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "ash/public/cpp/app_list/app_list_types.h"
#include "chrome/browser/ash/app_list/app_list_model_updater.h"
#include "chrome/browser/ash/app_list/app_list_syncable_service.h"
#include "chrome/browser/ash/app_list/app_list_syncable_service_factory.h"
#include "chrome/browser/ash/app_list/chrome_app_list_item.h"
#include "chrome/browser/ui/ash/shelf/chrome_shelf_controller_util.h"
#endif

namespace {

constexpr char kMigrationFlag[] = "MigrationTest";

constexpr char kExtensionUpdatePath[] = "/update_extension";
constexpr char kExtensionId[] = "kbmnembihfiondgfjekmnmcbddelicoi";
constexpr char kExtensionVersion[] = "1.0.0.0";
constexpr char kExtensionCrxPath[] = "/extensions/hosted_app.crx";

constexpr char kWebAppPath[] = "/web_apps/basic.html";

}  // namespace

namespace web_app {

class PreinstalledWebAppMigrationBrowserTest
    : public extensions::ExtensionBrowserTest {
 public:
  PreinstalledWebAppMigrationBrowserTest()
      : enable_chrome_apps_(
            &extensions::testing::g_enable_chrome_apps_for_testing,
            true),
        skip_preinstalled_web_app_startup_(
            PreinstalledWebAppManager::SkipStartupForTesting()),
        bypass_offline_manifest_requirement_(
            PreinstalledWebAppManager::
                BypassOfflineManifestRequirementForTesting()) {
    disable_external_extensions_scope_ =
        extensions::ExternalProviderManager::DisableExternalUpdatesForTesting();
  }
  ~PreinstalledWebAppMigrationBrowserTest() override = default;

  extensions::ExtensionService& extension_service() {
    return *extensions::ExtensionSystem::Get(profile())->extension_service();
  }

  GURL GetWebAppUrl() const {
    return embedded_test_server()->GetURL(kWebAppPath);
  }

  webapps::AppId GetWebAppId() const {
    return GenerateAppId(/*manifest_id=*/std::nullopt, GetWebAppUrl());
  }

  // extensions::ExtensionBrowserTest:
  void SetUp() override {
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        &PreinstalledWebAppMigrationBrowserTest::RequestHandlerOverride,
        base::Unretained(this)));
    ASSERT_TRUE(embedded_test_server()->Start());

    extensions::ExtensionBrowserTest::SetUp();
  }

  void SetUpOnMainThread() override {
    SetUpExtensionTestExternalProvider();

    extensions::ExtensionBrowserTest::SetUpOnMainThread();
    web_app::test::WaitUntilReady(WebAppProvider::GetForTest(profile()));
  }

  void TearDownOnMainThread() override {
    auto* const provider = WebAppProvider::GetForTest(profile());
    const WebAppRegistrar& registrar = provider->registrar_unsafe();
    std::vector<webapps::AppId> app_ids = registrar.GetAppIds();
    for (const auto& app_id : app_ids) {
      if (!registrar.IsInRegistrar(app_id)) {
        continue;
      }
      apps::AppReadinessWaiter(profile(), app_id).Await();

      const WebApp* app = registrar.GetAppById(app_id);
      DCHECK(app->CanUserUninstallWebApp());
      web_app::test::UninstallWebApp(profile(), app_id);
      apps::AppReadinessWaiter(
          profile(), app_id, base::BindRepeating([](apps::Readiness readiness) {
            return !apps_util::IsInstalled(readiness);
          }))
          .Await();
    }

    extensions::ExtensionBrowserTest::TearDownOnMainThread();
  }

  extensions::ExternalProviderManager* external_provider_manager() {
    return extensions::ExternalProviderManager::Get(profile());
  }

  std::unique_ptr<net::test_server::HttpResponse> RequestHandlerOverride(
      const net::test_server::HttpRequest& request) {
    std::string request_path = request.GetURL().path();
    if (request_path == kExtensionUpdatePath) {
      auto response = std::make_unique<net::test_server::BasicHttpResponse>();
      response->set_code(net::HTTP_OK);
      response->set_content(extensions::CreateUpdateManifest(
          {extensions::UpdateManifestItem(kExtensionId)
               .version(kExtensionVersion)
               .codebase(
                   embedded_test_server()->GetURL(kExtensionCrxPath).spec())}));
      response->set_content_type("text/xml");
      return std::move(response);
    }

    return nullptr;
  }

  void SetUpExtensionTestExternalProvider() {
    external_provider_manager()->ClearProvidersForTesting();

    extensions::ExtensionUpdater::Get(profile())->SetExtensionCacheForTesting(
        test_extension_cache_.get());

    std::string external_extension_config = base::ReplaceStringPlaceholders(
        R"({
          "$1": {
            "external_update_url": "$2",
            "web_app_migration_flag": "$3"
          }
        })",
        {kExtensionId,
         embedded_test_server()->GetURL(kExtensionUpdatePath).spec(),
         kMigrationFlag},
        nullptr);

    external_provider_manager()->AddProviderForTesting(
        std::make_unique<extensions::ExternalProviderImpl>(
            external_provider_manager(),
            base::MakeRefCounted<extensions::ExternalTestingLoader>(
                external_extension_config,
                base::FilePath(FILE_PATH_LITERAL("//absolute/path"))),
            profile(), extensions::mojom::ManifestLocation::kExternalPref,
            extensions::mojom::ManifestLocation::kExternalPrefDownload,
            // Matches |bundled_extension_creation_flags| in
            // ExternalProviderImpl::CreateExternalProviders().
            extensions::Extension::WAS_INSTALLED_BY_DEFAULT |
                extensions::Extension::FROM_WEBSTORE));

    disable_external_extensions_scope_.reset();
  }

  void SyncExternalExtensions() {
    base::RunLoop run_loop;
    external_provider_manager()
        ->set_external_updates_finished_callback_for_test(
            run_loop.QuitWhenIdleClosure());
    external_provider_manager()->CheckForExternalUpdates();
    run_loop.Run();
  }

  void SyncExternalWebApps(bool expect_install,
                           std::optional<webapps::UninstallResultCode>
                               expect_uninstall = std::nullopt,
                           bool pass_config = true) {
    base::RunLoop run_loop;

    std::optional<webapps::InstallResultCode> code;

    auto callback = base::BindLambdaForTesting(
        [&](std::map<GURL, ExternallyManagedAppManager::InstallResult>
                install_results,
            std::map<GURL, webapps::UninstallResultCode> uninstall_results) {
          if (expect_install) {
            code = install_results.at(GetWebAppUrl()).code;
            EXPECT_TRUE(
                *code == webapps::InstallResultCode::kSuccessNewInstall ||
                *code == webapps::InstallResultCode::kSuccessAlreadyInstalled ||
                *code ==
                    webapps::InstallResultCode::kSuccessOfflineOnlyInstall);
          } else {
            EXPECT_EQ(install_results.find(GetWebAppUrl()),
                      install_results.end());
          }
          if (!expect_uninstall.has_value()) {
            EXPECT_TRUE(uninstall_results.empty());
          } else {
            EXPECT_EQ(uninstall_results[GetWebAppUrl()], *expect_uninstall);
          }
          run_loop.Quit();
        });

    base::Value::List app_configs;
    if (pass_config) {
      std::string app_config_string = base::ReplaceStringPlaceholders(
          R"({
            "app_url": "$1",
            "launch_container": "window",
            "user_type": ["unmanaged"],
            "feature_name": "$2",
            "uninstall_and_replace": ["$3"]
          })",
          {GetWebAppUrl().spec(), kMigrationFlag, uninstall_and_replace_},
          nullptr);
      app_configs.Append(*base::JSONReader::Read(app_config_string));
    }
    base::AutoReset<const base::Value::List*> configs_for_testing =
        PreinstalledWebAppManager::SetConfigsForTesting(&app_configs);

    WebAppProvider::GetForTest(profile())
        ->preinstalled_web_app_manager()
        .LoadAndSynchronizeForTesting(std::move(callback));

    run_loop.Run();
  }

  bool IsWebAppInstalled() {
    std::optional<proto::InstallState> install_state =
        WebAppProvider::GetForTest(profile())
            ->registrar_unsafe()
            .GetInstallState(GetWebAppId());

    return install_state == proto::INSTALLED_WITHOUT_OS_INTEGRATION ||
           install_state == proto::INSTALLED_WITH_OS_INTEGRATION;
  }

  bool IsExtensionAppInstalled() {
    return extensions::ExtensionRegistry::Get(profile())->GetExtensionById(
        kExtensionId, extensions::ExtensionRegistry::EVERYTHING);
  }

 protected:
  const char* uninstall_and_replace_ = kExtensionId;
  base::test::ScopedFeatureList features_;
  std::optional<base::AutoReset<bool>> disable_external_extensions_scope_;
  std::unique_ptr<extensions::ExtensionCacheFake> test_extension_cache_;

 private:
  web_app::OsIntegrationTestOverrideBlockingRegistration faked_os_integration_;
  base::AutoReset<bool> enable_chrome_apps_;
  base::AutoReset<bool> skip_preinstalled_web_app_startup_;
  base::AutoReset<bool> bypass_offline_manifest_requirement_;
};

IN_PROC_BROWSER_TEST_F(PreinstalledWebAppMigrationBrowserTest,
                       MigrateRevertMigrate) {
#if BUILDFLAG(IS_CHROMEOS)
  // Grab handles to the app list to update shelf/list state for apps later on.
  app_list::AppListSyncableService* app_list_syncable_service =
      app_list::AppListSyncableServiceFactory::GetForProfile(profile());
  AppListModelUpdater* app_list_model_updater =
      app_list_syncable_service->GetModelUpdater();
  app_list_model_updater->SetActive(true);
#endif

  // Set up pre-migration state.
  {
    ASSERT_FALSE(IsPreinstalledAppInstallFeatureEnabled(kMigrationFlag));

    SyncExternalExtensions();
    SyncExternalWebApps(/*expect_install=*/false);

    EXPECT_FALSE(IsWebAppInstalled());
    EXPECT_TRUE(IsExtensionAppInstalled());

#if BUILDFLAG(IS_CHROMEOS)
    app_list_model_updater->SetItemPosition(
        kExtensionId, syncer::StringOrdinal("testapplistposition"));
    app_list_syncable_service->SetPinPosition(
        kExtensionId, syncer::StringOrdinal("testpinposition"));
    EXPECT_EQ(app_list_syncable_service->GetSyncItem(kExtensionId)->ToString(),
              "kbmnembi { Nothing } [testapplistposition] "
              "[testpinposition](INVALID COLOR)");
#endif
  }

  // Migrate extension app to web app.
  {
    base::AutoReset<bool> testing_scope =
        SetPreinstalledAppInstallFeatureAlwaysEnabledForTesting();
    ASSERT_TRUE(IsPreinstalledAppInstallFeatureEnabled(kMigrationFlag));

    SyncExternalExtensions();
    // Extension sticks around to be uninstalled by the replacement web app.
    EXPECT_TRUE(IsExtensionAppInstalled());

    {
      base::HistogramTester histograms;
      extensions::TestExtensionRegistryObserver uninstall_observer(
          extensions::ExtensionRegistry::Get(profile()));

      SyncExternalWebApps(/*expect_install=*/true);
      scoped_refptr<const extensions::Extension> uninstalled_app =
          uninstall_observer.WaitForExtensionUninstalled();
      EXPECT_EQ(uninstalled_app->id(), kExtensionId);

      EXPECT_TRUE(IsWebAppInstalled());
      EXPECT_FALSE(IsExtensionAppInstalled());

      histograms.ExpectUniqueSample(
          PreinstalledWebAppManager::kHistogramInstallResult,
          webapps::InstallResultCode::kSuccessNewInstall, 1);
      histograms.ExpectUniqueSample(
          PreinstalledWebAppManager::kHistogramUninstallAndReplaceCount, 1, 1);

#if BUILDFLAG(IS_CHROMEOS)
      // Chrome OS shelf/list position should migrate.
      EXPECT_EQ(
          app_list_syncable_service->GetSyncItem(GetWebAppId())->ToString(),
          base::StringPrintf("%s { Basic web app } [testapplistposition] "
                             "[testpinposition](INVALID COLOR)",
                             GetWebAppId().substr(0, 8).c_str()));
      // Old Chrome app prefs are retained.
      EXPECT_EQ(
          app_list_syncable_service->GetSyncItem(kExtensionId)->ToString(),
          "kbmnembi { Nothing } [testapplistposition] "
          "[testpinposition](INVALID COLOR)");
#endif
    }
  }

  // Revert migration.
  {
    ASSERT_FALSE(IsPreinstalledAppInstallFeatureEnabled(kMigrationFlag));

    SyncExternalExtensions();
    SyncExternalWebApps(
        /*expect_install=*/false,
        /*expect_uninstall=*/webapps::UninstallResultCode::kAppRemoved);

    EXPECT_TRUE(IsExtensionAppInstalled());
    EXPECT_FALSE(IsWebAppInstalled());
  }

  // Re-run migration.
  {
    base::HistogramTester histograms;
    base::AutoReset<bool> testing_scope =
        SetPreinstalledAppInstallFeatureAlwaysEnabledForTesting();
    ASSERT_TRUE(IsPreinstalledAppInstallFeatureEnabled(kMigrationFlag));

    extensions::TestExtensionRegistryObserver uninstall_observer(
        extensions::ExtensionRegistry::Get(profile()));

    SyncExternalWebApps(/*expect_install=*/true);
    scoped_refptr<const extensions::Extension> uninstalled_app =
        uninstall_observer.WaitForExtensionUninstalled();
    EXPECT_EQ(uninstalled_app->id(), kExtensionId);

    EXPECT_TRUE(IsWebAppInstalled());
    EXPECT_FALSE(IsExtensionAppInstalled());

    histograms.ExpectUniqueSample(
        PreinstalledWebAppManager::kHistogramInstallResult,
        webapps::InstallResultCode::kSuccessNewInstall, 1);
    histograms.ExpectUniqueSample(
        PreinstalledWebAppManager::kHistogramUninstallAndReplaceCount, 1, 1);

#if BUILDFLAG(IS_CHROMEOS)
    // Chrome OS shelf/list position should re-migrate.
    EXPECT_EQ(app_list_syncable_service->GetSyncItem(GetWebAppId())->ToString(),
              base::StringPrintf("%s { Basic web app } [testapplistposition] "
                                 "[testpinposition](INVALID COLOR)",
                                 GetWebAppId().substr(0, 8).c_str()));
    // Old Chrome app prefs are retained.
    EXPECT_EQ(app_list_syncable_service->GetSyncItem(kExtensionId)->ToString(),
              "kbmnembi { Nothing } [testapplistposition] "
              "[testpinposition](INVALID COLOR)");
#endif
  }
}

IN_PROC_BROWSER_TEST_F(PreinstalledWebAppMigrationBrowserTest,
                       MigratePreferences) {
#if BUILDFLAG(IS_CHROMEOS)
  app_list::AppListSyncableService* app_list_syncable_service =
      app_list::AppListSyncableServiceFactory::GetForProfile(profile());
  AppListModelUpdater* app_list_model_updater =
      app_list_syncable_service->GetModelUpdater();
  app_list_model_updater->SetActive(true);
#endif
  extensions::AppSorting* app_sorting =
      extensions::ExtensionSystem::Get(profile())->app_sorting();

  // Set up pre-migration state.
  {
    ASSERT_FALSE(IsPreinstalledAppInstallFeatureEnabled(kMigrationFlag));
    SyncExternalExtensions();
    SyncExternalWebApps(/*expect_install=*/false);

    EXPECT_FALSE(IsWebAppInstalled());
    EXPECT_TRUE(IsExtensionAppInstalled());

#if BUILDFLAG(IS_CHROMEOS)
    app_list_model_updater->SetItemPosition(
        kExtensionId, syncer::StringOrdinal("testapplistposition"));
    app_list_syncable_service->SetPinPosition(
        kExtensionId, syncer::StringOrdinal("testpinposition"));
    EXPECT_EQ(app_list_syncable_service->GetSyncItem(kExtensionId)->ToString(),
              "kbmnembi { Nothing } [testapplistposition] "
              "[testpinposition](INVALID COLOR)");
#endif

    // Set chrome://apps position.
    app_sorting->SetAppLaunchOrdinal(kExtensionId,
                                     syncer::StringOrdinal("testapplaunch"));
    app_sorting->SetPageOrdinal(kExtensionId,
                                syncer::StringOrdinal("testpageordinal"));

    // Set user preference to launch as browser tab.
    extensions::SetLaunchType(profile(), kExtensionId,
                              extensions::LAUNCH_TYPE_REGULAR);
  }

  // Migrate extension app to web app.
  {
    base::AutoReset<bool> testing_scope =
        SetPreinstalledAppInstallFeatureAlwaysEnabledForTesting();
    ASSERT_TRUE(IsPreinstalledAppInstallFeatureEnabled(kMigrationFlag));

    SyncExternalExtensions();
    // Extension sticks around to be uninstalled by the replacement web app.
    EXPECT_TRUE(IsExtensionAppInstalled());

    {
      base::HistogramTester histograms;
      extensions::TestExtensionRegistryObserver uninstall_observer(
          extensions::ExtensionRegistry::Get(profile()));

      SyncExternalWebApps(/*expect_install=*/true);
      EXPECT_TRUE(IsWebAppInstalled());

      scoped_refptr<const extensions::Extension> uninstalled_app =
          uninstall_observer.WaitForExtensionUninstalled();
      EXPECT_EQ(uninstalled_app->id(), kExtensionId);
      EXPECT_FALSE(IsExtensionAppInstalled());
      histograms.ExpectUniqueSample(
          PreinstalledWebAppManager::kHistogramInstallResult,
          webapps::InstallResultCode::kSuccessNewInstall, 1);
      histograms.ExpectUniqueSample(
          PreinstalledWebAppManager::kHistogramUninstallAndReplaceCount, 1, 1);
    }
  }

  // Check UI preferences have migrated across.
  {
    const webapps::AppId web_app_id = GetWebAppId();

#if BUILDFLAG(IS_CHROMEOS)
    // Chrome OS shelf/list position should migrate.
    EXPECT_EQ(app_list_syncable_service->GetSyncItem(GetWebAppId())->ToString(),
              base::StringPrintf("%s { Basic web app } [testapplistposition] "
                                 "[testpinposition](INVALID COLOR)",
                                 GetWebAppId().substr(0, 8).c_str()));
    // Chrome app shelf/list position should be retained.
    EXPECT_EQ(app_list_syncable_service->GetSyncItem(kExtensionId)->ToString(),
              "kbmnembi { Nothing } [testapplistposition] "
              "[testpinposition](INVALID COLOR)");
#endif

    // chrome://apps position should migrate.
    EXPECT_EQ(app_sorting->GetAppLaunchOrdinal(web_app_id).ToDebugString(),
              "testapplaunch");
    EXPECT_EQ(app_sorting->GetPageOrdinal(web_app_id).ToDebugString(),
              "testpageordinal");

    // User launch preference should migrate across and override
    // "launch_container": "window" in the JSON config.
    EXPECT_EQ(WebAppProvider::GetForTest(profile())
                  ->registrar_unsafe()
                  .GetAppUserDisplayMode(web_app_id),
              mojom::UserDisplayMode::kBrowser);
  }
}

static constexpr char kPlatformAppId[] = "dgbbhfbocdphnnabneckobeifilidpmj";

class PreinstalledWebAppMigratePlatformAppBrowserTest
    : public PreinstalledWebAppMigrationBrowserTest {
 public:
  PreinstalledWebAppMigratePlatformAppBrowserTest()
      : enable_chrome_apps_(
            &extensions::testing::g_enable_chrome_apps_for_testing,
            true) {
    uninstall_and_replace_ = kPlatformAppId;
  }

 private:
  base::AutoReset<bool> enable_chrome_apps_;
};

IN_PROC_BROWSER_TEST_F(PreinstalledWebAppMigratePlatformAppBrowserTest,
                       MigratePlatformAppPreferences) {
  // Install platform app to migrate.
  {
    apps::AppReadinessWaiter extension_app_registration_waiter(profile(),
                                                               kPlatformAppId);
    ASSERT_EQ(InstallExtension(
                  test_data_dir_.AppendASCII("platform_apps/app_window_2"), 1)
                  ->id(),
              kPlatformAppId);
    extension_app_registration_waiter.Await();
  }

  // Migrate extension app to web app.
  {
    base::AutoReset<bool> testing_scope =
        SetPreinstalledAppInstallFeatureAlwaysEnabledForTesting();
    extensions::TestExtensionRegistryObserver uninstall_observer(
        extensions::ExtensionRegistry::Get(profile()));

    SyncExternalWebApps(/*expect_install=*/true);
    EXPECT_TRUE(IsWebAppInstalled());

    uninstall_observer.WaitForExtensionUninstalled();
  }

  // Platform apps run in an app window so we must migrate as standalone.
  EXPECT_EQ(WebAppProvider::GetForTest(profile())
                ->registrar_unsafe()
                .GetAppUserDisplayMode(GetWebAppId()),
            mojom::UserDisplayMode::kStandalone);
}

IN_PROC_BROWSER_TEST_F(PreinstalledWebAppMigrationBrowserTest,
                       UserUninstalledExtensionApp) {
  // Set up pre-migration state.
  {
    ASSERT_FALSE(IsPreinstalledAppInstallFeatureEnabled(kMigrationFlag));

    SyncExternalExtensions();
    SyncExternalWebApps(/*expect_install=*/false);

    EXPECT_FALSE(IsWebAppInstalled());
    EXPECT_TRUE(IsExtensionAppInstalled());
  }

  {
    extensions::TestExtensionRegistryObserver uninstall_observer(
        extensions::ExtensionRegistry::Get(profile()), kExtensionId);
    extensions::ExtensionRegistrar::Get(profile())->UninstallExtension(
        kExtensionId, extensions::UNINSTALL_REASON_FOR_TESTING, nullptr);
    uninstall_observer.WaitForExtensionUninstalled();
    EXPECT_FALSE(IsWebAppInstalled());
    EXPECT_FALSE(IsExtensionAppInstalled());
  }

  // Migrate extension app to web app.
  {
    base::AutoReset<bool> testing_scope =
        SetPreinstalledAppInstallFeatureAlwaysEnabledForTesting();
    ASSERT_TRUE(IsPreinstalledAppInstallFeatureEnabled(kMigrationFlag));

    SyncExternalExtensions();
    EXPECT_FALSE(IsExtensionAppInstalled());

    SyncExternalWebApps(/*expect_install=*/false);
    EXPECT_FALSE(IsWebAppInstalled());
  }
}

// Check histogram counts when an app to replace gets installed after the
// preinstalled web app is installed.
IN_PROC_BROWSER_TEST_F(PreinstalledWebAppMigrationBrowserTest,
                       AppToReplaceStillInstalled) {
  base::AutoReset<bool> testing_scope =
      SetPreinstalledAppInstallFeatureAlwaysEnabledForTesting();
  ASSERT_TRUE(IsPreinstalledAppInstallFeatureEnabled(kMigrationFlag));

  // Preinstall web app.
  {
    base::HistogramTester histograms;
    SyncExternalWebApps(/*expect_install=*/true);
    histograms.ExpectUniqueSample(
        PreinstalledWebAppManager::kHistogramAppToReplaceStillInstalledCount, 0,
        1);
    histograms.ExpectUniqueSample(
        PreinstalledWebAppManager::
            kHistogramAppToReplaceStillDefaultInstalledCount,
        0, 1);
    histograms.ExpectUniqueSample(
        PreinstalledWebAppManager::
            kHistogramAppToReplaceStillInstalledInShelfCount,
        0, 1);
  }

  // Manually install Extension app to be replaced.
  LoadExtension(test_data_dir_.AppendASCII("hosted_app.crx"),
                {.ignore_manifest_warnings = true});

  // Re-sync preinstalled web apps.
  {
    base::HistogramTester histograms;
    SyncExternalWebApps(/*expect_install=*/true);
    histograms.ExpectUniqueSample(
        PreinstalledWebAppManager::kHistogramAppToReplaceStillInstalledCount, 1,
        1);
    histograms.ExpectUniqueSample(
        PreinstalledWebAppManager::
            kHistogramAppToReplaceStillDefaultInstalledCount,
        0, 1);

    // Neither app has been added to the shelf.
    histograms.ExpectUniqueSample(
        PreinstalledWebAppManager::
            kHistogramAppToReplaceStillInstalledInShelfCount,
        0, 1);
  }

#if BUILDFLAG(IS_CHROMEOS)
  // Pin both apps to the shelf.
  PinAppWithIDToShelf(GetWebAppId());
  PinAppWithIDToShelf(kExtensionId);

  // Re-sync preinstalled web apps.
  {
    base::HistogramTester histograms;
    SyncExternalWebApps(/*expect_install=*/true);

    // Apps have been added to the shelf.
    histograms.ExpectUniqueSample(
        PreinstalledWebAppManager::
            kHistogramAppToReplaceStillInstalledInShelfCount,
        1, 1);
  }
#endif  // BUILDFLAG(IS_CHROMEOS)
}

// Tests the migration from an extension-app to a preinstalled web app provided
// by the preinstalled apps (rather than an external config).
IN_PROC_BROWSER_TEST_F(PreinstalledWebAppMigrationBrowserTest,
                       MigrateToPreinstalledWebApp) {
  ScopedTestingPreinstalledAppData preinstalled_apps;
  ExternalInstallOptions options(GetWebAppUrl(),
                                 mojom::UserDisplayMode::kBrowser,
                                 ExternalInstallSource::kExternalDefault);
  options.gate_on_feature = kMigrationFlag;
  options.user_type_allowlist = {"unmanaged"};
  options.uninstall_and_replace.push_back(kExtensionId);
  options.only_use_app_info_factory = true;
  options.app_info_factory = base::BindLambdaForTesting([&]() {
    auto info = WebAppInstallInfo::CreateWithStartUrlForTesting(GetWebAppUrl());
    info->title = u"Test app";
    return info;
  });
  preinstalled_apps.apps.push_back(std::move(options));
  EXPECT_EQ(1u, GetPreinstalledWebApps(*profile()).size());
  // Set up pre-migration state.
  {
    base::HistogramTester histograms;

    ASSERT_FALSE(IsPreinstalledAppInstallFeatureEnabled(kMigrationFlag));

    SyncExternalExtensions();
    SyncExternalWebApps(/*expect_install=*/false,
                        /*expect_uninstall=*/std::nullopt,
                        /*pass_config=*/false);

    EXPECT_FALSE(IsWebAppInstalled());
    EXPECT_TRUE(IsExtensionAppInstalled());

    histograms.ExpectUniqueSample(
        PreinstalledWebAppManager::kHistogramEnabledCount, 0, 1);
    histograms.ExpectUniqueSample(
        PreinstalledWebAppManager::kHistogramDisabledCount, 1, 1);
    histograms.ExpectUniqueSample(
        PreinstalledWebAppManager::kHistogramConfigErrorCount, 0, 1);
  }

  // Migrate extension app to web app.
  {
    base::AutoReset<bool> testing_scope =
        SetPreinstalledAppInstallFeatureAlwaysEnabledForTesting();
    ASSERT_TRUE(IsPreinstalledAppInstallFeatureEnabled(kMigrationFlag));

    SyncExternalExtensions();
    // Extension sticks around to be uninstalled by the replacement web app.
    EXPECT_TRUE(IsExtensionAppInstalled());

    {
      base::HistogramTester histograms;

      extensions::TestExtensionRegistryObserver uninstall_observer(
          extensions::ExtensionRegistry::Get(profile()));

      SyncExternalWebApps(/*expect_install=*/true,
                          /*expect_uninstall=*/std::nullopt,
                          /*pass_config=*/false);
      EXPECT_TRUE(IsWebAppInstalled());

      scoped_refptr<const extensions::Extension> uninstalled_app =
          uninstall_observer.WaitForExtensionUninstalled();
      EXPECT_EQ(uninstalled_app->id(), kExtensionId);
      EXPECT_FALSE(IsExtensionAppInstalled());

      histograms.ExpectUniqueSample(
          PreinstalledWebAppManager::kHistogramEnabledCount, 1, 1);
      histograms.ExpectUniqueSample(
          PreinstalledWebAppManager::kHistogramDisabledCount, 0, 1);
      histograms.ExpectUniqueSample(
          PreinstalledWebAppManager::kHistogramConfigErrorCount, 0, 1);
      histograms.ExpectUniqueSample(
          PreinstalledWebAppManager::kHistogramInstallResult,
          webapps::InstallResultCode::kSuccessOfflineOnlyInstall, 1);
      histograms.ExpectUniqueSample(
          PreinstalledWebAppManager::kHistogramUninstallAndReplaceCount, 1, 1);
    }
  }
}

}  // namespace web_app
