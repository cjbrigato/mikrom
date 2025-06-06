// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/external_install_error.h"

#include <memory>

#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_install_prompt.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/external_install_manager.h"
#include "chrome/browser/extensions/external_provider_manager.h"
#include "chrome/browser/extensions/webstore_data_fetcher.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/global_error/global_error_waiter.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_utils.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/mock_external_provider.h"
#include "extensions/browser/test_extension_registry_observer.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace extensions {

namespace {

constexpr char kMockUserCountString[] = "1000";
constexpr char kMockRatingCountString[] = "100";
constexpr int kMockRatingCount = 100;
constexpr double kMockAverageRating = 4.4;

std::unique_ptr<FetchItemSnippetResponse> CreateMockResponse(
    const ExtensionId& id) {
  auto mock_response = std::make_unique<FetchItemSnippetResponse>();
  mock_response->set_item_id(id);
  mock_response->set_user_count_string(kMockUserCountString);
  mock_response->set_rating_count_string(kMockRatingCountString);
  mock_response->set_rating_count(kMockRatingCount);
  mock_response->set_average_rating(kMockAverageRating);

  return mock_response;
}

}  // namespace

class ExternalInstallErrorTest : public ExtensionBrowserTest {
 public:
  ExternalInstallErrorTest() = default;

 protected:
  void InstallExternalExtension(const char* provided_extension_id,
                                const std::string& version,
                                const std::string& crx_path) {
    ExtensionRegistry* registry = ExtensionRegistry::Get(profile());

    {
      // Wait for an external extension to be installed and a global error about
      // it added.
      test::GlobalErrorWaiter waiter(profile());
      TestExtensionRegistryObserver observer(registry);

      ExternalProviderManager* external_provider_manager =
          ExternalProviderManager::Get(profile());
      auto provider = std::make_unique<MockExternalProvider>(
          external_provider_manager, mojom::ManifestLocation::kExternalPref);
      provider->UpdateOrAddExtension(provided_extension_id, version,
                                     test_data_dir_.AppendASCII(crx_path));
      external_provider_manager->AddProviderForTesting(std::move(provider));
      external_provider_manager->CheckForExternalUpdates();

      auto extension = observer.WaitForExtensionInstalled();
      EXPECT_EQ(extension->id(), provided_extension_id);

      waiter.Wait();
    }

    // Verify the extension is in the expected state (disabled for being
    // unacknowledged).
    EXPECT_FALSE(
        registry->enabled_extensions().Contains(provided_extension_id));
    ExtensionPrefs* prefs = ExtensionPrefs::Get(profile());
    EXPECT_FALSE(prefs->IsExternalExtensionAcknowledged(provided_extension_id));
    EXPECT_THAT(prefs->GetDisableReasons(provided_extension_id),
                testing::UnorderedElementsAre(
                    disable_reason::DISABLE_EXTERNAL_EXTENSION));
  }
};

// Test that global errors don't crash on shutdown. See crbug.com/720081.
IN_PROC_BROWSER_TEST_F(ExternalInstallErrorTest,
                       TestShutdownWithWebstoreExtension) {
  // This relies on prompting for external extensions.
  FeatureSwitch::ScopedOverride feature_override(
      FeatureSwitch::prompt_for_external_extensions(), true);

  const char kId[] = "akjooamlhcgeopfifcmlggaebeocgokj";
  auto mock_response = CreateMockResponse(kId);
  WebstoreDataFetcher::SetMockItemSnippetReponseForTesting(mock_response.get());

  InstallExternalExtension(kId, "1", "update_from_webstore.crx");

  // Verify the external error.
  std::vector<ExternalInstallError*> errors =
      ExternalInstallManager::Get(profile())->GetErrorsForTesting();
  ASSERT_EQ(1u, errors.size());
  EXPECT_EQ(kId, errors[0]->extension_id());

  ExtensionInstallPrompt::Prompt* alert_prompt =
      errors[0]->GetPromptForTesting();
  ASSERT_TRUE(alert_prompt);

  // Validate that the alert prompt's data corresponds to what is returned from
  // the item snippets API.
  EXPECT_TRUE(alert_prompt->has_webstore_data());
  EXPECT_EQ(kMockRatingCountString, alert_prompt->localized_rating_count());
  EXPECT_EQ(kMockRatingCount, alert_prompt->rating_count());
  EXPECT_EQ(kMockAverageRating, alert_prompt->average_rating());
}

// Same as the above test except the extension does not update from the
// webstore, so the prompt should not display any webstore data.
IN_PROC_BROWSER_TEST_F(ExternalInstallErrorTest,
                       TestShutdownWithNonWebstoreExtension) {
  // This relies on prompting for external extensions.
  FeatureSwitch::ScopedOverride feature_override(
      FeatureSwitch::prompt_for_external_extensions(), true);

  const char kId[] = "ldnnhddmnhbkjipkidpdiheffobcpfmf";
  auto mock_response = CreateMockResponse(kId);
  WebstoreDataFetcher::SetMockItemSnippetReponseForTesting(mock_response.get());

  InstallExternalExtension(kId, "1.0.0.0", "good.crx");

  // Verify the external error.
  std::vector<ExternalInstallError*> errors =
      ExternalInstallManager::Get(profile())->GetErrorsForTesting();
  ASSERT_EQ(1u, errors.size());
  EXPECT_EQ(kId, errors[0]->extension_id());

  ExtensionInstallPrompt::Prompt* alert_prompt =
      errors[0]->GetPromptForTesting();
  ASSERT_TRUE(alert_prompt);

  // Validate that the alert prompt's data corresponds to what is returned from
  // the item snippets API.
  EXPECT_FALSE(alert_prompt->has_webstore_data());
}

}  // namespace extensions
