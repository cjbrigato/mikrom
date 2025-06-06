// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/extension_registrar.h"

#include <memory>
#include <optional>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/scoped_command_line.h"
#include "build/chromeos_buildflags.h"
#include "content/public/browser/browser_context.h"
#include "extensions/browser/blocklist_extension_prefs.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extensions_test.h"
#include "extensions/browser/management_policy.h"
#include "extensions/browser/test_extension_registry_observer.h"
#include "extensions/browser/test_extensions_browser_client.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/switches.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "base/test/scoped_feature_list.h"
#include "chrome/common/pref_names.h"  // nogncheck
#include "components/account_id/account_id.h"  // nogncheck
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "components/user_manager/fake_user_manager.h"
#include "components/user_manager/scoped_user_manager.h"
#endif  // BUILDFLAG(IS_CHROMEOS)

namespace extensions {

namespace {

using testing::Return;
using testing::_;

// Supplies dependencies needed by the tests. Specifically,
// ExtensionRegistrar::CanBlockExtension() depends on ManagementPolicy.
class TestExtensionSystem : public MockExtensionSystem {
 public:
  explicit TestExtensionSystem(content::BrowserContext* context)
      : MockExtensionSystem(context) {}

  TestExtensionSystem(const TestExtensionSystem&) = delete;
  TestExtensionSystem& operator=(const TestExtensionSystem&) = delete;

  ~TestExtensionSystem() override {}

  // ExtensionSystem:
  ManagementPolicy* management_policy() override { return &policy_; }

 private:
  ManagementPolicy policy_;
};

class TestExtensionRegistrarDelegate : public ExtensionRegistrar::Delegate {
 public:
  TestExtensionRegistrarDelegate() = default;

  TestExtensionRegistrarDelegate(const TestExtensionRegistrarDelegate&) =
      delete;
  TestExtensionRegistrarDelegate& operator=(
      const TestExtensionRegistrarDelegate&) = delete;

  ~TestExtensionRegistrarDelegate() override = default;

  // ExtensionRegistrar::Delegate:
  MOCK_METHOD2(PreAddExtension,
               void(const Extension* extension,
                    const Extension* old_extension));
  MOCK_METHOD1(OnAddNewOrUpdatedExtension, void(const Extension* extension));
  MOCK_METHOD1(PostActivateExtension,
               void(scoped_refptr<const Extension> extension));
  MOCK_METHOD1(PostDeactivateExtension,
               void(scoped_refptr<const Extension> extension));
  MOCK_METHOD1(PreUninstallExtension,
               void(scoped_refptr<const Extension> extension));
  MOCK_METHOD2(PostUninstallExtension,
               void(scoped_refptr<const Extension> extension,
                    base::OnceClosure done_callback));
  MOCK_METHOD2(LoadExtensionForReload,
               void(const ExtensionId& extension_id,
                    const base::FilePath& path));
  MOCK_METHOD2(LoadExtensionForReloadWithQuietFailure,
               void(const ExtensionId& extension_id,
                    const base::FilePath& path));
  MOCK_METHOD2(ShowExtensionDisabledError, void(const Extension*, bool));
  MOCK_METHOD1(CanEnableExtension, bool(const Extension* extension));
  MOCK_METHOD1(CanDisableExtension, bool(const Extension* extension));
  MOCK_METHOD1(ShouldBlockExtension, bool(const Extension* extension));
  MOCK_METHOD1(GrantActivePermissions, void(const Extension* extension));
  MOCK_METHOD0(UpdateExternalExtensionAlert, void());
  MOCK_METHOD4(OnExtensionInstalled,
               void(const Extension* extension,
                    const syncer::StringOrdinal& page_ordinal,
                    int install_flags,
                    base::Value::Dict ruleset_install_prefs));
};

}  // namespace

class ExtensionRegistrarTest : public ExtensionsTest {
 public:
  ExtensionRegistrarTest() = default;

  ExtensionRegistrarTest(const ExtensionRegistrarTest&) = delete;
  ExtensionRegistrarTest& operator=(const ExtensionRegistrarTest&) = delete;

  ~ExtensionRegistrarTest() override = default;

  void SetUp() override {
    ExtensionsTest::SetUp();
    extensions_browser_client()->set_extension_system_factory(&factory_);
    extension_ = ExtensionBuilder("extension").Build();
    registrar_ = std::make_unique<ExtensionRegistrar>(browser_context());
    registrar_->Init(delegate(), /*extensions_enabled=*/true,
                     base::CommandLine::ForCurrentProcess(), base::FilePath(),
                     base::FilePath());

    // Mock defaults.
    ON_CALL(delegate_, CanEnableExtension(extension_.get()))
        .WillByDefault(Return(true));
    ON_CALL(delegate_, CanDisableExtension(extension_.get()))
        .WillByDefault(Return(true));
    EXPECT_CALL(delegate_, PostActivateExtension(_)).Times(0);
    EXPECT_CALL(delegate_, PostDeactivateExtension(_)).Times(0);
  }

  void TearDown() override {
    registrar_.reset();
    ExtensionsTest::TearDown();
  }

 protected:
  // Boilerplate to verify the mock's expected calls. With a SCOPED_TRACE at the
  // call site, this includes the caller's function in the Gtest trace on
  // failure. Otherwise, the failures are unhelpfully listed at the end of the
  // test.
  void VerifyMock() {
    EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(&delegate_));

    // Re-add the expectations for functions that must not be called.
    EXPECT_CALL(delegate_, PostActivateExtension(_)).Times(0);
    EXPECT_CALL(delegate_, PostDeactivateExtension(_)).Times(0);
  }

  // Adds the extension as enabled and verifies the result.
  void AddEnabledExtension() {
    SCOPED_TRACE("AddEnabledExtension");
    EXPECT_CALL(delegate_, PostActivateExtension(extension_));
    registrar_->AddExtension(extension_);
    ExpectInSet(ExtensionRegistry::ENABLED);
    EXPECT_TRUE(IsExtensionReady());

    EXPECT_TRUE(ExtensionPrefs::Get(browser_context())
                    ->GetDisableReasons(extension()->id())
                    .empty());

    VerifyMock();
  }

  // Adds the extension as disabled and verifies the result.
  void AddDisabledExtension() {
    SCOPED_TRACE("AddDisabledExtension");
    ExtensionPrefs::Get(browser_context())
        ->AddDisableReason(extension_->id(),
                           disable_reason::DISABLE_USER_ACTION);
    registrar_->AddExtension(extension_);
    ExpectInSet(ExtensionRegistry::DISABLED);
    EXPECT_FALSE(IsExtensionReady());
  }

  // Adds the extension as blocklisted and verifies the result.
  void AddBlocklistedExtension() {
    SCOPED_TRACE("AddBlocklistedExtension");
    blocklist_prefs::SetSafeBrowsingExtensionBlocklistState(
        extension_->id(), BitMapBlocklistState::BLOCKLISTED_MALWARE,
        ExtensionPrefs::Get(browser_context()));
    registrar_->AddExtension(extension_);
    ExpectInSet(ExtensionRegistry::BLOCKLISTED);
    EXPECT_FALSE(IsExtensionReady());
  }

  // Adds the extension as blocked and verifies the result.
  void AddBlockedExtension() {
    SCOPED_TRACE("AddBlockedExtension");
    registrar_->AddExtension(extension_);
    ExpectInSet(ExtensionRegistry::BLOCKED);
    EXPECT_FALSE(IsExtensionReady());
  }

  // Removes an enabled extension and verifies the result.
  void RemoveEnabledExtension() {
    SCOPED_TRACE("RemoveEnabledExtension");
    // Calling RemoveExtension removes its entry from the enabled list and
    // removes the extension.
    EXPECT_CALL(delegate_, PostDeactivateExtension(extension_));
    registrar_->RemoveExtension(extension_->id(),
                                UnloadedExtensionReason::UNINSTALL);
    ExpectInSet(ExtensionRegistry::NONE);

    VerifyMock();
  }

  // Removes a disabled extension and verifies the result.
  void RemoveDisabledExtension() {
    SCOPED_TRACE("RemoveDisabledExtension");
    // Calling RemoveExtension removes its entry from the disabled list and
    // removes the extension.
    registrar_->RemoveExtension(extension_->id(),
                                UnloadedExtensionReason::UNINSTALL);
    ExpectInSet(ExtensionRegistry::NONE);

    ExtensionPrefs::Get(browser_context())
        ->DeleteExtensionPrefs(extension_->id());
  }

  // Removes a blocklisted extension and verifies the result.
  void RemoveBlocklistedExtension() {
    SCOPED_TRACE("RemoveBlocklistedExtension");
    EXPECT_CALL(delegate_, PostDeactivateExtension(extension_)).Times(0);
    registrar_->RemoveExtension(extension_->id(),
                                UnloadedExtensionReason::UNINSTALL);

    // RemoveExtension does not un-blocklist the extension.
    ExpectInSet(ExtensionRegistry::BLOCKLISTED);

    VerifyMock();
  }

  // Removes a blocked extension and verifies the result.
  void RemoveBlockedExtension() {
    SCOPED_TRACE("RemoveBlockedExtension");
    EXPECT_CALL(delegate_, PostDeactivateExtension(extension_)).Times(0);
    registrar_->RemoveExtension(extension_->id(),
                                UnloadedExtensionReason::UNINSTALL);

    // RemoveExtension does not un-block the extension.
    ExpectInSet(ExtensionRegistry::BLOCKED);

    VerifyMock();
  }

  void EnableExtension() {
    SCOPED_TRACE("EnableExtension");
    EXPECT_CALL(delegate_, PostActivateExtension(extension_));
    registrar_->EnableExtension(extension_->id());
    ExpectInSet(ExtensionRegistry::ENABLED);
    EXPECT_TRUE(IsExtensionReady());

    VerifyMock();
  }

  void DisableEnabledExtension() {
    SCOPED_TRACE("DisableEnabledExtension");
    EXPECT_CALL(delegate_, PostDeactivateExtension(extension_));
    registrar_->DisableExtension(extension_->id(),
                                 {disable_reason::DISABLE_USER_ACTION});
    ExpectInSet(ExtensionRegistry::DISABLED);
    EXPECT_FALSE(IsExtensionReady());

    VerifyMock();
  }

  void DisableTerminatedExtension() {
    SCOPED_TRACE("DisableTerminatedExtension");
    // PostDeactivateExtension should not be called.
    registrar_->DisableExtension(extension_->id(),
                                 {disable_reason::DISABLE_USER_ACTION});
    ExpectInSet(ExtensionRegistry::DISABLED);
    EXPECT_FALSE(IsExtensionReady());
  }

  void TerminateExtension() {
    SCOPED_TRACE("TerminateExtension");
    EXPECT_CALL(delegate_, PostDeactivateExtension(extension_));
    registrar_->TerminateExtension(extension_->id());
    ExpectInSet(ExtensionRegistry::TERMINATED);
    EXPECT_FALSE(IsExtensionReady());
    VerifyMock();
  }

  void UntrackTerminatedExtension() {
    SCOPED_TRACE("UntrackTerminatedExtension");
    registrar()->UntrackTerminatedExtension(extension()->id());
    ExpectInSet(ExtensionRegistry::NONE);
  }

  // Directs ExtensionRegistrar to reload the extension and verifies the
  // delegate is invoked correctly.
  void ReloadEnabledExtension() {
    SCOPED_TRACE("ReloadEnabledExtension");
    EXPECT_CALL(delegate_, PostDeactivateExtension(extension()));
    EXPECT_CALL(delegate_,
                LoadExtensionForReload(extension()->id(), extension()->path()));
    registrar()->ReloadExtension(extension()->id());
    VerifyMock();

    // ExtensionRegistrar should have disabled the extension in preparation for
    // a reload.
    ExpectInSet(ExtensionRegistry::DISABLED);
    EXPECT_THAT(ExtensionPrefs::Get(browser_context())
                    ->GetDisableReasons(extension()->id()),
                testing::UnorderedElementsAre(disable_reason::DISABLE_RELOAD));
  }

  // Directs ExtensionRegistrar to reload the terminated extension and verifies
  // the delegate is invoked correctly.
  void ReloadTerminatedExtension() {
    SCOPED_TRACE("ReloadTerminatedExtension");
    EXPECT_CALL(delegate_,
                LoadExtensionForReload(extension()->id(), extension()->path()));
    registrar()->ReloadExtension(extension()->id());
    VerifyMock();

    // The extension should remain in the terminated set until the reload
    // completes successfully.
    ExpectInSet(ExtensionRegistry::TERMINATED);
    // Unlike when reloading an enabled extension, the extension hasn't been
    // disabled and shouldn't have the DISABLE_RELOAD disable reason.
    EXPECT_TRUE(ExtensionPrefs::Get(browser_context())
                    ->GetDisableReasons(extension()->id())
                    .empty());
  }

  // Verifies that the extension is in the given set in the ExtensionRegistry
  // and not in other sets.
  void ExpectInSet(ExtensionRegistry::IncludeFlag set_id) {
    ExtensionRegistry* registry = ExtensionRegistry::Get(browser_context());

    EXPECT_EQ(set_id == ExtensionRegistry::ENABLED,
              registry->enabled_extensions().Contains(extension_->id()));

    EXPECT_EQ(set_id == ExtensionRegistry::DISABLED,
              registry->disabled_extensions().Contains(extension_->id()));

    EXPECT_EQ(set_id == ExtensionRegistry::TERMINATED,
              registry->terminated_extensions().Contains(extension_->id()));

    EXPECT_EQ(set_id == ExtensionRegistry::BLOCKLISTED,
              registry->blocklisted_extensions().Contains(extension_->id()));

    EXPECT_EQ(set_id == ExtensionRegistry::BLOCKED,
              registry->blocked_extensions().Contains(extension_->id()));
  }

  bool IsExtensionReady() {
    return ExtensionRegistry::Get(browser_context())
        ->ready_extensions()
        .Contains(extension_->id());
  }

  ExtensionRegistrar* registrar() { return registrar_.get(); }
  TestExtensionRegistrarDelegate* delegate() { return &delegate_; }

  scoped_refptr<const Extension> extension() const { return extension_; }

 private:
  MockExtensionSystemFactory<TestExtensionSystem> factory_;
  // Use NiceMock to allow uninteresting calls, so the delegate can be queried
  // any number of times. We will explicitly disallow unexpected calls to
  // PostActivateExtension/PostDeactivateExtension with EXPECT_CALL statements.
  testing::NiceMock<TestExtensionRegistrarDelegate> delegate_;
  scoped_refptr<const Extension> extension_;

  // Initialized in SetUp().
  std::unique_ptr<ExtensionRegistrar> registrar_;
};

TEST_F(ExtensionRegistrarTest, Basic) {
  AddEnabledExtension();
  RemoveEnabledExtension();
}

TEST_F(ExtensionRegistrarTest, AlreadyEnabled) {
  AddEnabledExtension();

  // As the extension is already enabled, this is a no-op.
  registrar()->EnableExtension(extension()->id());
  ExpectInSet(ExtensionRegistry::ENABLED);
  EXPECT_TRUE(IsExtensionReady());

  RemoveEnabledExtension();
}

TEST_F(ExtensionRegistrarTest, Disable) {
  AddEnabledExtension();

  // Disable the extension before removing it.
  DisableEnabledExtension();
  RemoveDisabledExtension();
}

TEST_F(ExtensionRegistrarTest, DisableAndEnable) {
  AddEnabledExtension();

  // Disable then enable the extension.
  DisableEnabledExtension();
  EnableExtension();

  RemoveEnabledExtension();
}

TEST_F(ExtensionRegistrarTest, AddDisabled) {
  // An extension can be added as disabled, then removed.
  AddDisabledExtension();
  RemoveDisabledExtension();

  // An extension can be added as disabled, then enabled.
  AddDisabledExtension();
  EnableExtension();
  RemoveEnabledExtension();
}

TEST_F(ExtensionRegistrarTest, AddForceEnabled) {
  // Prevent the extension from being disabled.
  ON_CALL(*delegate(), CanDisableExtension(extension().get()))
      .WillByDefault(Return(false));
  AddEnabledExtension();

  // Extension cannot be disabled.
  registrar()->DisableExtension(extension()->id(),
                                {disable_reason::DISABLE_USER_ACTION});
  ExpectInSet(ExtensionRegistry::ENABLED);
}

TEST_F(ExtensionRegistrarTest, AddForceDisabled) {
  // Prevent the extension from being enabled.
  ON_CALL(*delegate(), CanEnableExtension(extension().get()))
      .WillByDefault(Return(false));
  AddDisabledExtension();

  // Extension cannot be enabled.
  registrar()->EnableExtension(extension()->id());
  ExpectInSet(ExtensionRegistry::DISABLED);
}

TEST_F(ExtensionRegistrarTest, AddBlocklisted) {
  AddBlocklistedExtension();

  // A blocklisted extension cannot be enabled/disabled/reloaded.
  registrar()->EnableExtension(extension()->id());
  ExpectInSet(ExtensionRegistry::BLOCKLISTED);
  registrar()->DisableExtension(extension()->id(),
                                {disable_reason::DISABLE_USER_ACTION});
  ExpectInSet(ExtensionRegistry::BLOCKLISTED);
  registrar()->ReloadExtensionWithQuietFailure(extension()->id());
  ExpectInSet(ExtensionRegistry::BLOCKLISTED);

  RemoveBlocklistedExtension();
}

TEST_F(ExtensionRegistrarTest, AddBlocked) {
  // Block extensions.
  registrar()->BlockAllExtensions();

  // A blocked extension can be added.
  AddBlockedExtension();

  // Extension cannot be enabled/disabled.
  registrar()->EnableExtension(extension()->id());
  ExpectInSet(ExtensionRegistry::BLOCKED);
  registrar()->DisableExtension(extension()->id(),
                                {disable_reason::DISABLE_USER_ACTION});
  ExpectInSet(ExtensionRegistry::BLOCKED);

  RemoveBlockedExtension();
}

TEST_F(ExtensionRegistrarTest, TerminateExtension) {
  AddEnabledExtension();
  TerminateExtension();

  // Calling RemoveExtension removes its entry from the terminated list.
  registrar()->RemoveExtension(extension()->id(),
                               UnloadedExtensionReason::UNINSTALL);
  ExpectInSet(ExtensionRegistry::NONE);
}

TEST_F(ExtensionRegistrarTest, DisableTerminatedExtension) {
  AddEnabledExtension();
  TerminateExtension();
  DisableTerminatedExtension();
  RemoveDisabledExtension();
}

TEST_F(ExtensionRegistrarTest, EnableTerminatedExtension) {
  AddEnabledExtension();
  TerminateExtension();

  // Enable the terminated extension.
  UntrackTerminatedExtension();
  AddEnabledExtension();

  RemoveEnabledExtension();
}

TEST_F(ExtensionRegistrarTest, ReloadExtension) {
  AddEnabledExtension();
  ReloadEnabledExtension();

  // Add the now-reloaded extension back into the registrar.
  AddEnabledExtension();
}

TEST_F(ExtensionRegistrarTest, RemoveReloadedExtension) {
  AddEnabledExtension();
  ReloadEnabledExtension();

  // Simulate the delegate failing to load the extension and removing it
  // instead.
  RemoveDisabledExtension();

  // Attempting to reload it silently fails.
  registrar()->ReloadExtensionWithQuietFailure(extension()->id());
  ExpectInSet(ExtensionRegistry::NONE);
}

TEST_F(ExtensionRegistrarTest, ReloadTerminatedExtension) {
  AddEnabledExtension();
  TerminateExtension();

  // Reload the terminated extension.
  ReloadTerminatedExtension();

  // Complete the reload by adding the extension. Expect the extension to be
  // enabled once re-added to the registrar, since ExtensionPrefs shouldn't say
  // it's disabled.
  AddEnabledExtension();
}

TEST_F(ExtensionRegistrarTest, AddDisableFlagExemptedExtension) {
  // Disable extensions but exempt the test extension.
  registrar()->set_extensions_enabled_for_test(false);
  registrar()->AddDisableFlagExemptedExtension(extension()->id());

  // Add the test extension.
  AddEnabledExtension();

  // The extension is enabled because it was exempted from disablement.
  ExpectInSet(ExtensionRegistry::ENABLED);
}

TEST_F(ExtensionRegistrarTest, AddAndRemoveComponentExtension) {
  EXPECT_CALL(*delegate(), PostActivateExtension(extension()));
  registrar()->AddComponentExtension(extension().get());
  ExpectInSet(ExtensionRegistry::ENABLED);

  EXPECT_CALL(*delegate(), PostDeactivateExtension(extension()));
  registrar()->RemoveComponentExtension(extension()->id());
  ExpectInSet(ExtensionRegistry::NONE);
}

TEST_F(ExtensionRegistrarTest, Enabledness) {
  base::FilePath install_dir =
      browser_context()->GetPath().AppendASCII(kInstallDirectoryName);
  base::FilePath unpacked_install_dir =
      browser_context()->GetPath().AppendASCII(kUnpackedInstallDirectoryName);

  // The profile lifetimes must not overlap: services may use global variables.
  {
    // By default, we are enabled.
    base::test::ScopedCommandLine command_line;

    ExtensionRegistrar* registrar = ExtensionRegistrar::Get(browser_context());
    registrar->Init(delegate(), true, command_line.GetProcessCommandLine(),
                    install_dir, unpacked_install_dir);
    EXPECT_TRUE(registrar->extensions_enabled());
  }

  {
    base::test::ScopedCommandLine command_line;
    command_line.GetProcessCommandLine()->AppendSwitch(
        switches::kDisableExtensions);

    ExtensionRegistrar* registrar = ExtensionRegistrar::Get(browser_context());
    registrar->Init(delegate(), true, command_line.GetProcessCommandLine(),
                    install_dir, unpacked_install_dir);
    EXPECT_FALSE(registrar->extensions_enabled());
  }

  // TODO(crbug.com/406544103): Test disabling an extension in a profile here.
  // TODO(crbug.com/406544103): Test enabling an extension in a profile here.
}

}  // namespace extensions
