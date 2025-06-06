// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_prefs_unittest.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/extensions/install_tracker.h"
#include "chrome/common/chrome_paths.h"
#include "components/content_settings/core/browser/content_settings_registry.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/mock_pref_change_callback.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/sync/model/string_ordinal.h"
#include "components/sync_preferences/pref_service_syncable.h"
#include "extensions/browser/blocklist_extension_prefs.h"
#include "extensions/browser/blocklist_state.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_pref_value_map.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_prefs_observer.h"
#include "extensions/browser/install_flag.h"
#include "extensions/browser/install_prefs_helper.h"
#include "extensions/browser/pref_names.h"
#include "extensions/browser/pref_types.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/extension_id.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/permissions/permission_set.h"
#include "extensions/common/permissions/permissions_info.h"
#include "testing/gmock/include/gmock/gmock.h"

using base::Time;
using extensions::mojom::APIPermissionID;
using extensions::mojom::ManifestLocation;

namespace extensions {

static void AddPattern(URLPatternSet* extent, const std::string& pattern) {
  int schemes = URLPattern::SCHEME_ALL;
  extent->AddPattern(URLPattern(schemes, pattern));
}

ExtensionPrefsTest::ExtensionPrefsTest()
    : prefs_(base::SingleThreadTaskRunner::GetCurrentDefault()) {}

ExtensionPrefsTest::~ExtensionPrefsTest() = default;

void ExtensionPrefsTest::RegisterPreferences(
    user_prefs::PrefRegistrySyncable* registry) {}

void ExtensionPrefsTest::SetUp() {
  RegisterPreferences(prefs_.pref_registry().get());
  Initialize();
}

void ExtensionPrefsTest::TearDown() {
  Verify();

  // Shutdown the InstallTracker early, which is a dependency on some
  // ExtensionPrefTests (and depends on PrefService being available in
  // shutdown).
  InstallTracker::Get(prefs_.profile())->Shutdown();

  // Reset ExtensionPrefs, and re-verify.
  prefs_.ResetPrefRegistry();
  RegisterPreferences(prefs_.pref_registry().get());
  prefs_.RecreateExtensionPrefs();
  Verify();
  prefs_.pref_service()->CommitPendingWrite();
  base::RunLoop().RunUntilIdle();
}

// Tests the LastPingDay/SetLastPingDay functions.
class ExtensionPrefsLastPingDay : public ExtensionPrefsTest {
 public:
  ExtensionPrefsLastPingDay()
      : extension_time_(Time::Now() - base::Hours(4)),
        blocklist_time_(Time::Now() - base::Hours(2)) {}

  void Initialize() override {
    extension_id_ = prefs_.AddExtensionAndReturnId("last_ping_day");
    EXPECT_TRUE(prefs()->LastPingDay(extension_id_).is_null());
    prefs()->SetLastPingDay(extension_id_, extension_time_);
    prefs()->SetBlocklistLastPingDay(blocklist_time_);
  }

  void Verify() override {
    Time result = prefs()->LastPingDay(extension_id_);
    EXPECT_FALSE(result.is_null());
    EXPECT_TRUE(result == extension_time_);
    result = prefs()->BlocklistLastPingDay();
    EXPECT_FALSE(result.is_null());
    EXPECT_TRUE(result == blocklist_time_);
  }

 private:
  Time extension_time_;
  Time blocklist_time_;
  ExtensionId extension_id_;
};
TEST_F(ExtensionPrefsLastPingDay, LastPingDay) {}

// Tests the migration of a deprecated disable reason.
class ExtensionPrefsDeprecatedDisableReason : public ExtensionPrefsTest {
 public:
  void Initialize() override {
    extension1_ = prefs_.AddExtension("test1");
    DisableReasonSet disable_reasons = {
        disable_reason::DEPRECATED_DISABLE_UNKNOWN_FROM_SYNC};
    prefs()->AddDisableReasons(extension1_->id(), disable_reasons);
    extension2_ = prefs_.AddExtension("test2");
    disable_reasons.insert(disable_reason::DISABLE_PERMISSIONS_INCREASE);
    prefs()->AddDisableReasons(extension2_->id(), disable_reasons);
    prefs()->MigrateDeprecatedDisableReasons();
  }

  void Verify() override {
    EXPECT_THAT(
        prefs()->GetDisableReasons(extension1_->id()),
        testing::UnorderedElementsAre(disable_reason::DISABLE_USER_ACTION));
    // Verify that if an extension has a disable reason in addition to the
    // deprecated reason, we don't add the user action disable reason.
    EXPECT_THAT(prefs()->GetDisableReasons(extension2_->id()),
                testing::UnorderedElementsAre(
                    disable_reason::DISABLE_PERMISSIONS_INCREASE));
  }

 private:
  scoped_refptr<Extension> extension1_;
  scoped_refptr<Extension> extension2_;
};

TEST_F(ExtensionPrefsDeprecatedDisableReason, MigrateExtensionState) {}

class ExtensionPrefsDisableReasonsBitflagToListMigration
    : public ExtensionPrefsTest {
 public:
  void Initialize() override {
    extension_1_ = prefs_.AddExtension("test1");
    prefs()->AddDisableReasons(extension_1_->id(),
                               extension_1_disable_reasons_);

    extension_2_ = prefs_.AddExtension("test2");
    prefs()->AddDisableReasons(extension_2_->id(),
                               extension_2_disable_reasons_);
  }

  void Verify() override {
    // Verify that the disable reasons are returned correctly.
    EXPECT_EQ(prefs()->GetDisableReasons(extension_1_->id()),
              extension_1_disable_reasons_);
    EXPECT_EQ(prefs()->GetDisableReasons(extension_2_->id()),
              extension_2_disable_reasons_);

    // Verify() is called twice.
    // In the first execution, we have the modern state. We wipe out this state
    // and simulate the legacy state. In the second execution, `ExtensionPrefs`
    // is re-constructed. It should re-construct the modern state from the
    // simulated legacy state.
    SimulateLegacyState();
  }

 private:
  void SimulateLegacyState() {
    // Write the disable reasons to the preference as a bitflag.
    constexpr const char kPrefDisableReasons[] = "disable_reasons";
    prefs()->UpdateExtensionPref(
        extension_1_->id(), kPrefDisableReasons,
        base::Value(DisableReasonSetToBitflag(extension_1_disable_reasons_)));
    prefs()->UpdateExtensionPref(
        extension_2_->id(), kPrefDisableReasons,
        base::Value(DisableReasonSetToBitflag(extension_2_disable_reasons_)));
  }

  int DisableReasonSetToBitflag(const DisableReasonSet& set) {
    int flag = 0;
    for (disable_reason::DisableReason reason : set) {
      flag |= reason;
    }
    return flag;
  }

  scoped_refptr<Extension> extension_1_;
  const DisableReasonSet extension_1_disable_reasons_ = {
      disable_reason::DISABLE_USER_ACTION,
      disable_reason::DISABLE_BLOCKED_BY_POLICY};

  scoped_refptr<Extension> extension_2_;
  const DisableReasonSet extension_2_disable_reasons_ = {
      disable_reason::DISABLE_PERMISSIONS_INCREASE,
      disable_reason::DISABLE_NOT_VERIFIED,
      disable_reason::DISABLE_USER_ACTION};
};

TEST_F(ExtensionPrefsDisableReasonsBitflagToListMigration, TestPrefMigration) {}

class ExtensionPrefsEscalatePermissions : public ExtensionPrefsTest {
 public:
  void Initialize() override {
    extension = prefs_.AddExtension("test");
    prefs()->AddDisableReasons(extension->id(),
                               {disable_reason::DISABLE_PERMISSIONS_INCREASE});
  }

  void Verify() override {
    EXPECT_TRUE(prefs()->DidExtensionEscalatePermissions(extension->id()));
  }

 private:
  scoped_refptr<Extension> extension;
};
TEST_F(ExtensionPrefsEscalatePermissions, EscalatePermissions) {}

// Tests the AddGrantedPermissions / GetGrantedPermissions functions.
class ExtensionPrefsGrantedPermissions : public ExtensionPrefsTest {
 public:
  void Initialize() override {
    const APIPermissionInfo* permission_info =
        PermissionsInfo::GetInstance()->GetByID(
            mojom::APIPermissionID::kSocket);

    extension_id_ = prefs_.AddExtensionAndReturnId("test");

    api_perm_set1_.insert(APIPermissionID::kTab);
    api_perm_set1_.insert(APIPermissionID::kBookmark);
    std::unique_ptr<APIPermission> permission(
        permission_info->CreateAPIPermission());
    {
      base::Value::List list;
      list.Append("tcp-connect:*.example.com:80");
      list.Append("udp-bind::8080");
      list.Append("udp-send-to::8888");
      base::Value value(std::move(list));
      ASSERT_TRUE(permission->FromValue(&value, nullptr, nullptr));
    }
    api_perm_set1_.insert(std::move(permission));

    api_perm_set2_.insert(APIPermissionID::kHistory);

    AddPattern(&ehost_perm_set1_, "http://*.google.com/*");
    AddPattern(&ehost_perm_set1_, "http://example.com/*");
    AddPattern(&ehost_perm_set1_, "chrome://favicon/*");

    AddPattern(&ehost_perm_set2_, "https://*.google.com/*");
    // with duplicate:
    AddPattern(&ehost_perm_set2_, "http://*.google.com/*");

    AddPattern(&shost_perm_set1_, "http://reddit.com/r/test/*");
    AddPattern(&shost_perm_set2_, "http://reddit.com/r/test/*");
    AddPattern(&shost_perm_set2_, "http://somesite.com/*");
    AddPattern(&shost_perm_set2_, "http://example.com/*");

    APIPermissionSet expected_apis = api_perm_set1_.Clone();

    AddPattern(&ehost_permissions_, "http://*.google.com/*");
    AddPattern(&ehost_permissions_, "http://example.com/*");
    AddPattern(&ehost_permissions_, "chrome://favicon/*");
    AddPattern(&ehost_permissions_, "https://*.google.com/*");

    AddPattern(&shost_permissions_, "http://reddit.com/r/test/*");
    AddPattern(&shost_permissions_, "http://somesite.com/*");
    AddPattern(&shost_permissions_, "http://example.com/*");

    // Make sure both granted api and host permissions start empty.
    EXPECT_TRUE(prefs()->GetGrantedPermissions(extension_id_)->IsEmpty());

    {
      // Add part of the api permissions.
      prefs()->AddGrantedPermissions(
          extension_id_,
          PermissionSet(api_perm_set1_.Clone(), ManifestPermissionSet(),
                        URLPatternSet(), URLPatternSet()));
      std::unique_ptr<const PermissionSet> granted_permissions =
          prefs()->GetGrantedPermissions(extension_id_);
      EXPECT_TRUE(granted_permissions.get());
      EXPECT_FALSE(granted_permissions->IsEmpty());
      EXPECT_EQ(expected_apis, granted_permissions->apis());
      EXPECT_TRUE(granted_permissions->effective_hosts().is_empty());
    }

    {
      // Add part of the explicit host permissions.
      prefs()->AddGrantedPermissions(
          extension_id_,
          PermissionSet(APIPermissionSet(), ManifestPermissionSet(),
                        ehost_perm_set1_.Clone(), URLPatternSet()));
      std::unique_ptr<const PermissionSet> granted_permissions =
          prefs()->GetGrantedPermissions(extension_id_);
      EXPECT_FALSE(granted_permissions->IsEmpty());
      EXPECT_EQ(expected_apis, granted_permissions->apis());
      EXPECT_EQ(ehost_perm_set1_, granted_permissions->explicit_hosts());
      EXPECT_EQ(ehost_perm_set1_, granted_permissions->effective_hosts());
    }

    {
      // Add part of the scriptable host permissions.
      prefs()->AddGrantedPermissions(
          extension_id_,
          PermissionSet(APIPermissionSet(), ManifestPermissionSet(),
                        URLPatternSet(), shost_perm_set1_.Clone()));
      std::unique_ptr<const PermissionSet> granted_permissions =
          prefs()->GetGrantedPermissions(extension_id_);
      EXPECT_FALSE(granted_permissions->IsEmpty());
      EXPECT_EQ(expected_apis, granted_permissions->apis());
      EXPECT_EQ(ehost_perm_set1_, granted_permissions->explicit_hosts());
      EXPECT_EQ(shost_perm_set1_, granted_permissions->scriptable_hosts());

      effective_permissions_ =
          URLPatternSet::CreateUnion(ehost_perm_set1_, shost_perm_set1_);
      EXPECT_EQ(effective_permissions_, granted_permissions->effective_hosts());
    }

    {
      // Add the rest of the permissions.
      APIPermissionSet::Union(expected_apis, api_perm_set2_, &api_permissions_);
      prefs()->AddGrantedPermissions(
          extension_id_,
          PermissionSet(api_perm_set2_.Clone(), ManifestPermissionSet(),
                        ehost_perm_set2_.Clone(), shost_perm_set2_.Clone()));

      std::unique_ptr<const PermissionSet> granted_permissions =
          prefs()->GetGrantedPermissions(extension_id_);
      EXPECT_TRUE(granted_permissions.get());
      EXPECT_FALSE(granted_permissions->IsEmpty());
      EXPECT_EQ(api_permissions_, granted_permissions->apis());
      EXPECT_EQ(ehost_permissions_, granted_permissions->explicit_hosts());
      EXPECT_EQ(shost_permissions_, granted_permissions->scriptable_hosts());
      effective_permissions_ =
          URLPatternSet::CreateUnion(ehost_permissions_, shost_permissions_);
      EXPECT_EQ(effective_permissions_, granted_permissions->effective_hosts());
    }
  }

  void Verify() override {
    std::unique_ptr<const PermissionSet> permissions =
        prefs()->GetGrantedPermissions(extension_id_);
    EXPECT_TRUE(permissions.get());
    EXPECT_EQ(api_permissions_, permissions->apis());
    EXPECT_EQ(ehost_permissions_, permissions->explicit_hosts());
    EXPECT_EQ(shost_permissions_, permissions->scriptable_hosts());
  }

 private:
  ExtensionId extension_id_;
  APIPermissionSet api_perm_set1_;
  APIPermissionSet api_perm_set2_;
  URLPatternSet ehost_perm_set1_;
  URLPatternSet ehost_perm_set2_;
  URLPatternSet shost_perm_set1_;
  URLPatternSet shost_perm_set2_;

  APIPermissionSet api_permissions_;
  URLPatternSet ehost_permissions_;
  URLPatternSet shost_permissions_;
  URLPatternSet effective_permissions_;
};
TEST_F(ExtensionPrefsGrantedPermissions, GrantedPermissions) {}

// Tests the SetDesiredActivePermissions / GetDesiredActivePermissions
// functions.
class ExtensionPrefsActivePermissions : public ExtensionPrefsTest {
 public:
  void Initialize() override {
    extension_id_ = prefs_.AddExtensionAndReturnId("test");

    {
      APIPermissionSet api_perms;
      api_perms.insert(APIPermissionID::kTab);
      api_perms.insert(APIPermissionID::kBookmark);
      api_perms.insert(APIPermissionID::kHistory);

      URLPatternSet ehosts;
      AddPattern(&ehosts, "http://*.google.com/*");
      AddPattern(&ehosts, "http://example.com/*");
      AddPattern(&ehosts, "chrome://favicon/*");

      URLPatternSet shosts;
      AddPattern(&shosts, "https://*.google.com/*");
      AddPattern(&shosts, "http://reddit.com/r/test/*");

      active_perms_ = std::make_unique<PermissionSet>(
          std::move(api_perms), ManifestPermissionSet(), std::move(ehosts),
          std::move(shosts));
    }

    // Make sure the active permissions start empty.
    std::unique_ptr<const PermissionSet> active =
        prefs()->GetDesiredActivePermissions(extension_id_);
    EXPECT_TRUE(active->IsEmpty());

    // Set the desired active permissions.
    prefs()->SetDesiredActivePermissions(extension_id_, *active_perms_);
    active = prefs()->GetDesiredActivePermissions(extension_id_);
    EXPECT_EQ(active_perms_->apis(), active->apis());
    EXPECT_EQ(active_perms_->explicit_hosts(), active->explicit_hosts());
    EXPECT_EQ(active_perms_->scriptable_hosts(), active->scriptable_hosts());
    EXPECT_EQ(*active_perms_, *active);

    // Reset the desired active permissions.
    active_perms_ = std::make_unique<PermissionSet>();
    prefs()->SetDesiredActivePermissions(extension_id_, *active_perms_);
    active = prefs()->GetDesiredActivePermissions(extension_id_);
    EXPECT_EQ(*active_perms_, *active);
  }

  void Verify() override {
    std::unique_ptr<const PermissionSet> permissions =
        prefs()->GetDesiredActivePermissions(extension_id_);
    EXPECT_EQ(*active_perms_, *permissions);
  }

 private:
  ExtensionId extension_id_;
  std::unique_ptr<const PermissionSet> active_perms_;
};
TEST_F(ExtensionPrefsActivePermissions, SetAndGetDesiredActivePermissions) {}

// Tests the GetVersionString function.
class ExtensionPrefsVersionString : public ExtensionPrefsTest {
 public:
  void Initialize() override {
    extension = prefs_.AddExtension("test");
    EXPECT_EQ("0.1", prefs()->GetVersionString(extension->id()));
    prefs()->OnExtensionUninstalled(extension->id(),
                                    ManifestLocation::kInternal, false);
  }

  void Verify() override {
    EXPECT_EQ("", prefs()->GetVersionString(extension->id()));
  }

 private:
  scoped_refptr<Extension> extension;
};
TEST_F(ExtensionPrefsVersionString, VersionString) {}

class ExtensionPrefsAcknowledgment : public ExtensionPrefsTest {
 public:
  void Initialize() override {
    not_installed_id_ = "pghjnghklobnfoidcldiidjjjhkeeaoi";

    // Install some extensions.
    for (int i = 0; i < 5; i++) {
      std::string name = "test" + base::NumberToString(i);
      extensions_.push_back(prefs_.AddExtension(name));
    }
    EXPECT_EQ(std::nullopt,
              prefs()->GetInstalledExtensionInfo(not_installed_id_));

    ExtensionList::const_iterator iter;
    for (iter = extensions_.begin(); iter != extensions_.end(); ++iter) {
      std::string id = (*iter)->id();
      EXPECT_FALSE(prefs()->IsExternalExtensionAcknowledged(id));
      EXPECT_FALSE(prefs()->IsBlocklistedExtensionAcknowledged(id));
      if (external_id_.empty()) {
        external_id_ = id;
        continue;
      }
      if (blocklisted_id_.empty()) {
        blocklisted_id_ = id;
        continue;
      }
    }
    // For each type of acknowledgment, acknowledge one installed and one
    // not-installed extension id.
    prefs()->AcknowledgeExternalExtension(external_id_);
    prefs()->AcknowledgeBlocklistedExtension(blocklisted_id_);
    prefs()->AcknowledgeExternalExtension(not_installed_id_);
    prefs()->AcknowledgeBlocklistedExtension(not_installed_id_);
  }

  void Verify() override {
    ExtensionList::const_iterator iter;
    for (iter = extensions_.begin(); iter != extensions_.end(); ++iter) {
      std::string id = (*iter)->id();
      if (id == external_id_) {
        EXPECT_TRUE(prefs()->IsExternalExtensionAcknowledged(id));
      } else {
        EXPECT_FALSE(prefs()->IsExternalExtensionAcknowledged(id));
      }
      if (id == blocklisted_id_) {
        EXPECT_TRUE(prefs()->IsBlocklistedExtensionAcknowledged(id));
      } else {
        EXPECT_FALSE(prefs()->IsBlocklistedExtensionAcknowledged(id));
      }
    }
    EXPECT_TRUE(prefs()->IsExternalExtensionAcknowledged(not_installed_id_));
    EXPECT_TRUE(prefs()->IsBlocklistedExtensionAcknowledged(not_installed_id_));
  }

 private:
  ExtensionList extensions_;

  std::string not_installed_id_;
  std::string external_id_;
  std::string blocklisted_id_;
};
TEST_F(ExtensionPrefsAcknowledgment, Acknowledgment) {}

// Tests the idle install information functions.
class ExtensionPrefsDelayedInstallInfo : public ExtensionPrefsTest {
 public:
  // Sets idle install information for one test extension.
  void SetIdleInfo(const std::string& id, int num) {
    base::Value::Dict manifest;
    manifest.Set(manifest_keys::kName, "test");
    manifest.Set(manifest_keys::kVersion, "1." + base::NumberToString(num));
    manifest.Set(manifest_keys::kManifestVersion, 2);
    base::FilePath path =
        prefs_.extensions_dir().AppendASCII(base::NumberToString(num));
    std::string errors;
    scoped_refptr<Extension> extension =
        Extension::Create(path, ManifestLocation::kInternal, manifest,
                          Extension::NO_FLAGS, id, &errors);
    ASSERT_TRUE(extension.get()) << errors;
    ASSERT_EQ(id, extension->id());
    prefs()->SetDelayedInstallInfo(extension.get(), /*disable_reasons=*/{},
                                   kInstallFlagNone,
                                   ExtensionPrefs::DelayReason::kWaitForIdle,
                                   syncer::StringOrdinal(), std::string());
  }

  // Verifies that we get back expected idle install information previously
  // set by SetIdleInfo.
  void VerifyIdleInfo(const std::string& id, int num) {
    std::optional<ExtensionInfo> info(prefs()->GetDelayedInstallInfo(id));
    ASSERT_TRUE(info);
    const std::string* version =
        info->extension_manifest->FindString("version");
    ASSERT_TRUE(version);
    ASSERT_EQ("1." + base::NumberToString(num), *version);
    ASSERT_EQ(base::NumberToString(num),
              info->extension_path.BaseName().MaybeAsASCII());
  }

  bool HasInfoForId(const ExtensionPrefs::ExtensionsInfo& info,
                    const std::string& id) {
    return std::ranges::find_if(info.begin(), info.end(),
                                [&id](const ExtensionInfo& info) {
                                  return info.extension_id == id;
                                }) != info.end();
  }

  void Initialize() override {
    base::PathService::Get(chrome::DIR_TEST_DATA, &basedir_);
    now_ = Time::Now();
    id1_ = prefs_.AddExtensionAndReturnId("1");
    id2_ = prefs_.AddExtensionAndReturnId("2");
    id3_ = prefs_.AddExtensionAndReturnId("3");
    id4_ = prefs_.AddExtensionAndReturnId("4");

    // Set info for two extensions, then remove it.
    SetIdleInfo(id1_, 1);
    SetIdleInfo(id2_, 2);
    VerifyIdleInfo(id1_, 1);
    VerifyIdleInfo(id2_, 2);
    ExtensionPrefs::ExtensionsInfo info = prefs()->GetAllDelayedInstallInfo();
    EXPECT_EQ(2u, info.size());
    EXPECT_TRUE(HasInfoForId(info, id1_));
    EXPECT_TRUE(HasInfoForId(info, id2_));
    prefs()->RemoveDelayedInstallInfo(id1_);
    prefs()->RemoveDelayedInstallInfo(id2_);
    info = prefs()->GetAllDelayedInstallInfo();
    EXPECT_TRUE(info.empty());

    // Try getting/removing info for an id that used to have info set.
    EXPECT_FALSE(prefs()->GetDelayedInstallInfo(id1_));
    EXPECT_FALSE(prefs()->RemoveDelayedInstallInfo(id1_));

    // Try getting/removing info for an id that has not yet had any info set.
    EXPECT_FALSE(prefs()->GetDelayedInstallInfo(id3_));
    EXPECT_FALSE(prefs()->RemoveDelayedInstallInfo(id3_));

    // Set info for 4 extensions, then remove for one of them.
    SetIdleInfo(id1_, 1);
    SetIdleInfo(id2_, 2);
    SetIdleInfo(id3_, 3);
    SetIdleInfo(id4_, 4);
    VerifyIdleInfo(id1_, 1);
    VerifyIdleInfo(id2_, 2);
    VerifyIdleInfo(id3_, 3);
    VerifyIdleInfo(id4_, 4);
    prefs()->RemoveDelayedInstallInfo(id3_);
  }

  void Verify() override {
    // Make sure the info for the 3 extensions we expect is present.
    ExtensionPrefs::ExtensionsInfo info = prefs()->GetAllDelayedInstallInfo();
    EXPECT_EQ(3u, info.size());
    EXPECT_TRUE(HasInfoForId(info, id1_));
    EXPECT_TRUE(HasInfoForId(info, id2_));
    EXPECT_TRUE(HasInfoForId(info, id4_));
    VerifyIdleInfo(id1_, 1);
    VerifyIdleInfo(id2_, 2);
    VerifyIdleInfo(id4_, 4);

    // Make sure there isn't info the for the one extension id we removed.
    EXPECT_FALSE(prefs()->GetDelayedInstallInfo(id3_));
  }

 protected:
  Time now_;
  base::FilePath basedir_;
  std::string id1_;
  std::string id2_;
  std::string id3_;
  std::string id4_;
};
TEST_F(ExtensionPrefsDelayedInstallInfo, DelayedInstallInfo) {}

// Tests the FinishDelayedInstallInfo function.
class ExtensionPrefsFinishDelayedInstallInfo : public ExtensionPrefsTest {
 public:
  void Initialize() override {
    base::Value::Dict dictionary;
    dictionary.Set(manifest_keys::kName, "test");
    dictionary.Set(manifest_keys::kVersion, "0.1");
    dictionary.Set(manifest_keys::kManifestVersion, 2);
    dictionary.SetByDottedPath(manifest_keys::kBackgroundPage,
                               "background.html");
    scoped_refptr<Extension> extension = prefs_.AddExtensionWithManifest(
        dictionary, ManifestLocation::kInternal);
    id_ = extension->id();

    // Set idle info
    base::Value::Dict manifest;
    manifest.Set(manifest_keys::kName, "test");
    manifest.Set(manifest_keys::kVersion, "0.2");
    manifest.Set(manifest_keys::kManifestVersion, 2);
    base::Value::List scripts;
    scripts.Append("test.js");
    manifest.SetByDottedPath(manifest_keys::kBackgroundScripts,
                             std::move(scripts));
    base::FilePath path = prefs_.extensions_dir().AppendASCII("test_0.2");
    std::string errors;
    scoped_refptr<Extension> new_extension =
        Extension::Create(path, ManifestLocation::kInternal, manifest,
                          Extension::NO_FLAGS, id_, &errors);
    ASSERT_TRUE(new_extension.get()) << errors;
    ASSERT_EQ(id_, new_extension->id());
    prefs()->SetDelayedInstallInfo(new_extension.get(), /*disable_reasons=*/{},
                                   kInstallFlagNone,
                                   ExtensionPrefs::DelayReason::kWaitForIdle,
                                   syncer::StringOrdinal(), "Param");

    // Finish idle installation
    ASSERT_TRUE(prefs()->FinishDelayedInstallInfo(id_));
  }

  void Verify() override {
    EXPECT_FALSE(prefs()->GetDelayedInstallInfo(id_));
    EXPECT_EQ(std::string("Param"), GetInstallParam(prefs(), id_));

    const base::Value::Dict* dict = prefs()->ReadPrefAsDict(id_, "manifest");
    ASSERT_TRUE(dict);
    const std::string* name = dict->FindString(manifest_keys::kName);
    ASSERT_TRUE(name);
    EXPECT_EQ("test", *name);
    const std::string* version = dict->FindString(manifest_keys::kVersion);
    ASSERT_TRUE(version);
    EXPECT_EQ("0.2", *version);
    EXPECT_FALSE(dict->FindString(manifest_keys::kBackgroundPage));
    const base::Value::List* scripts =
        dict->FindListByDottedPath(manifest_keys::kBackgroundScripts);
    ASSERT_TRUE(scripts);
    EXPECT_EQ(1u, scripts->size());
  }

 protected:
  std::string id_;
};

TEST_F(ExtensionPrefsFinishDelayedInstallInfo, FinishDelayedInstallInfo) {}

class ExtensionPrefsOnExtensionInstalled : public ExtensionPrefsTest {
 public:
  void Initialize() override {
    extension_ = prefs_.AddExtension("on_extension_installed");
    EXPECT_FALSE(prefs()->IsExtensionDisabled(extension_->id()));
    prefs()->OnExtensionInstalled(extension_.get(),
                                  {disable_reason::DISABLE_USER_ACTION},
                                  syncer::StringOrdinal(), "Param");
  }

  void Verify() override {
    EXPECT_TRUE(prefs()->IsExtensionDisabled(extension_->id()));
    EXPECT_EQ(std::string("Param"), GetInstallParam(prefs(), extension_->id()));
  }

 private:
  scoped_refptr<Extension> extension_;
};
TEST_F(ExtensionPrefsOnExtensionInstalled, ExtensionPrefsOnExtensionInstalled) {
}

class ExtensionPrefsPopulatesInstallTimePrefs : public ExtensionPrefsTest {
 public:
  void Initialize() override {
    extension_ = prefs_.AddExtension("test1");
    // Cache the first install time.
    first_install_time_ = GetFirstInstallTime(prefs(), extension_->id());
    auto last_update_time = GetLastUpdateTime(prefs(), extension_->id());
    // First time install will result in same value for both first_install_time
    // and last_update_time prefs.
    EXPECT_NE(base::Time(), first_install_time_);
    EXPECT_NE(base::Time(), last_update_time);
    EXPECT_EQ(first_install_time_, last_update_time);

    // Update the extension.
    extension_ = prefs_.AddExtension("test1");
  }

  void Verify() override {
    auto first_install_time = GetFirstInstallTime(prefs(), extension_->id());
    auto last_update_time = GetLastUpdateTime(prefs(), extension_->id());
    EXPECT_NE(base::Time(), first_install_time);
    EXPECT_NE(base::Time(), last_update_time);
    // Verify that the first_install_time remains unchanged after the extension
    // update.
    EXPECT_EQ(first_install_time, first_install_time_);
    // Verify that the last_update_time is no longer the same as the
    // first_install_time after the extension update.
    EXPECT_NE(first_install_time, last_update_time);
  }

 private:
  scoped_refptr<Extension> extension_;
  base::Time first_install_time_;
};
TEST_F(ExtensionPrefsPopulatesInstallTimePrefs,
       ExtensionPrefsPopulatesInstallTimePrefs) {}

class ExtensionPrefsMigratesToLastUpdateTime : public ExtensionPrefsTest {
 public:
  void Initialize() override {
    extension_ = prefs_.AddExtension("test1");
    // Re-create migration scenario by removing the new first_install_time,
    // last_update_time pref keys and adding back the legacy install_time key.
    prefs()->UpdateExtensionPref(extension_->id(), kLastUpdateTimePrefKey,
                                 std::nullopt);
    prefs()->UpdateExtensionPref(extension_->id(), kFirstInstallTimePrefKey,
                                 std::nullopt);
    time_str_ = base::NumberToString(
        base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds());
    prefs()->SetStringPref(extension_->id(), kOldInstallTimePrefMap, time_str_);

    // Run the migration routine.
    prefs()->BackfillAndMigrateInstallTimePrefs();
  }

  void Verify() override {
    auto* dict = prefs()->GetExtensionPref(extension_->id());

    // Verify the legacy install_time key has been removed and replaced by
    // the last_update_time key. Also verify that the first_install_time key
    // has been added and has the same value as the last_update_time key.
    EXPECT_FALSE(dict->FindString(kOldInstallTimePrefKey));
    const std::string* first_install_time =
        dict->FindString(kFirstInstallTimePrefKey);
    ASSERT_TRUE(first_install_time);
    EXPECT_EQ(*first_install_time, time_str_);
    const std::string* last_update_time =
        dict->FindString(kLastUpdateTimePrefKey);
    ASSERT_TRUE(last_update_time);
    EXPECT_EQ(*last_update_time, time_str_);
  }

 private:
  scoped_refptr<Extension> extension_;
  std::string time_str_;
  static constexpr char kFirstInstallTimePrefKey[] = "first_install_time";
  static constexpr char kLastUpdateTimePrefKey[] = "last_update_time";
  static constexpr char kOldInstallTimePrefKey[] = "install_time";
  static constexpr PrefMap kOldInstallTimePrefMap = {
      kOldInstallTimePrefKey, PrefType::kString, PrefScope::kExtensionSpecific};
};
TEST_F(ExtensionPrefsMigratesToLastUpdateTime,
       ExtensionPrefsMigratesToLastUpdateTime) {}

// Tests that the bit map pref value is cleared if the value matches the default
// bit.
class ExtensionPrefsBitMapPrefValueClearedIfEqualsDefaultValue
    : public ExtensionPrefsTest {
 public:
  void Initialize() override {
    extension_ = prefs_.AddExtension("test1");
    prefs()->ModifyBitMapPrefBits(
        extension_->id(), disable_reason::DISABLE_PERMISSIONS_INCREASE,
        ExtensionPrefs::BitMapPrefOperation::kAdd, "disable_reasons",
        disable_reason::DISABLE_USER_ACTION);
    // Set the bit map pref value to the default value, it should clear the
    // pref.
    prefs()->ModifyBitMapPrefBits(
        extension_->id(), disable_reason::DISABLE_USER_ACTION,
        ExtensionPrefs::BitMapPrefOperation::kReplace, "disable_reasons",
        disable_reason::DISABLE_USER_ACTION);
  }

  void Verify() override {
    const base::Value::Dict* ext = prefs()->GetExtensionPref(extension_->id());
    ASSERT_TRUE(ext);
    // The pref value should be cleared.
    EXPECT_FALSE(ext->FindInt("disable_reasons"));
  }

 private:
  scoped_refptr<Extension> extension_;
};

TEST_F(ExtensionPrefsBitMapPrefValueClearedIfEqualsDefaultValue,
       ExtensionPrefsBitMapPrefValueClearedIfEqualsDefaultValue) {}

class ExtensionPrefsFlags : public ExtensionPrefsTest {
 public:
  void Initialize() override {
    {
      base::Value::Dict dictionary;
      dictionary.Set(manifest_keys::kName, "from_webstore");
      dictionary.Set(manifest_keys::kVersion, "0.1");
      dictionary.Set(manifest_keys::kManifestVersion, 2);
      webstore_extension_ = prefs_.AddExtensionWithManifestAndFlags(
          dictionary, ManifestLocation::kInternal, Extension::FROM_WEBSTORE);
    }

    {
      base::Value::Dict dictionary;
      dictionary.Set(manifest_keys::kName, "was_installed_by_default");
      dictionary.Set(manifest_keys::kVersion, "0.1");
      dictionary.Set(manifest_keys::kManifestVersion, 2);
      default_extension_ = prefs_.AddExtensionWithManifestAndFlags(
          dictionary, ManifestLocation::kInternal,
          Extension::WAS_INSTALLED_BY_DEFAULT);
    }

    {
      base::Value::Dict dictionary;
      dictionary.Set(manifest_keys::kName, "was_installed_by_oem");
      dictionary.Set(manifest_keys::kVersion, "0.1");
      dictionary.Set(manifest_keys::kManifestVersion, 2);
      oem_extension_ = prefs_.AddExtensionWithManifestAndFlags(
          dictionary, ManifestLocation::kInternal,
          Extension::WAS_INSTALLED_BY_OEM);
    }
  }

  void Verify() override {
    EXPECT_TRUE(IsFromWebStore(prefs(), webstore_extension_->id()));
    EXPECT_TRUE(WasInstalledByDefault(prefs(), default_extension_->id()));
    EXPECT_TRUE(WasInstalledByOem(prefs(), oem_extension_->id()));
  }

 private:
  scoped_refptr<Extension> webstore_extension_;
  scoped_refptr<Extension> default_extension_;
  scoped_refptr<Extension> oem_extension_;
};
TEST_F(ExtensionPrefsFlags, ExtensionPrefsFlags) {}

PrefsPrepopulatedTestBase::PrefsPrepopulatedTestBase() {
  base::Value::Dict simple_dict;
  std::string error;

  simple_dict.Set(manifest_keys::kVersion, "1.0.0.0");
  simple_dict.Set(manifest_keys::kManifestVersion, 2);
  simple_dict.Set(manifest_keys::kName, "unused");

  extension1_ = Extension::Create(prefs_.temp_dir().AppendASCII("ext1_"),
                                  ManifestLocation::kExternalPref, simple_dict,
                                  Extension::NO_FLAGS, &error);
  extension2_ = Extension::Create(prefs_.temp_dir().AppendASCII("ext2_"),
                                  ManifestLocation::kExternalPref, simple_dict,
                                  Extension::NO_FLAGS, &error);
  extension3_ = Extension::Create(prefs_.temp_dir().AppendASCII("ext3_"),
                                  ManifestLocation::kExternalPref, simple_dict,
                                  Extension::NO_FLAGS, &error);
  extension4_ = Extension::Create(prefs_.temp_dir().AppendASCII("ext4_"),
                                  ManifestLocation::kExternalPref, simple_dict,
                                  Extension::NO_FLAGS, &error);

  internal_extension_ = Extension::Create(
      prefs_.temp_dir().AppendASCII("internal extension"),
      ManifestLocation::kInternal, simple_dict, Extension::NO_FLAGS, &error);
}

PrefsPrepopulatedTestBase::~PrefsPrepopulatedTestBase() = default;

// Tests clearing the last launched preference.
class ExtensionPrefsClearLastLaunched : public ExtensionPrefsTest {
 public:
  ~ExtensionPrefsClearLastLaunched() override = default;

  void Initialize() override {
    extension_a_ = prefs_.AddExtension("a");
    extension_b_ = prefs_.AddExtension("b");
  }

  void Verify() override {
    // Set last launched times for each extension.
    prefs()->SetLastLaunchTime(extension_a_->id(), base::Time::Now());
    prefs()->SetLastLaunchTime(extension_b_->id(), base::Time::Now());

    // Also set some other preference for one of the extensions.
    prefs()->SetAllowFileAccess(extension_a_->id(), true);

    // Now clear the launch times.
    prefs()->ClearLastLaunchTimes();

    // All launch times should be gone.
    EXPECT_EQ(base::Time(), prefs()->GetLastLaunchTime(extension_a_->id()));
    EXPECT_EQ(base::Time(), prefs()->GetLastLaunchTime(extension_b_->id()));

    // Other preferences should be untouched.
    EXPECT_TRUE(prefs()->AllowFileAccess(extension_a_->id()));
  }

 private:
  scoped_refptr<const Extension> extension_a_;
  scoped_refptr<const Extension> extension_b_;
};
TEST_F(ExtensionPrefsClearLastLaunched, ExtensionPrefsClearLastLaunched) {}

class ExtensionPrefsComponentExtension : public ExtensionPrefsTest {
 public:
  ~ExtensionPrefsComponentExtension() override = default;
  void Initialize() override {
    // Adding a component extension.
    component_extension_ =
        ExtensionBuilder("a")
            .SetLocation(ManifestLocation::kComponent)
            .SetPath(prefs_.extensions_dir().AppendASCII("a"))
            .Build();
    prefs_.AddExtension(component_extension_.get());

    // Adding a non component extension.
    no_component_extension_ =
        ExtensionBuilder("b")
            .SetLocation(ManifestLocation::kInternal)
            .SetPath(prefs_.extensions_dir().AppendASCII("b"))
            .Build();
    prefs_.AddExtension(no_component_extension_.get());

    APIPermissionSet api_perms;
    api_perms.insert(APIPermissionID::kTab);
    api_perms.insert(APIPermissionID::kBookmark);
    api_perms.insert(APIPermissionID::kHistory);

    URLPatternSet shosts;
    AddPattern(&shosts, "chrome://print/*");

    active_perms_ = std::make_unique<PermissionSet>(
        std::move(api_perms), ManifestPermissionSet(), URLPatternSet(),
        std::move(shosts));
    // Set the desired active permissions.
    prefs()->SetDesiredActivePermissions(component_extension_->id(),
                                         *active_perms_);
    prefs()->SetDesiredActivePermissions(no_component_extension_->id(),
                                         *active_perms_);
  }

  void Verify() override {
    // Component extension can access chrome://print/*.
    std::unique_ptr<const PermissionSet> component_permissions =
        prefs()->GetDesiredActivePermissions(component_extension_->id());
    EXPECT_EQ(1u, component_permissions->scriptable_hosts().size());

    // Non Component extension can not access chrome://print/*.
    std::unique_ptr<const PermissionSet> no_component_permissions =
        prefs()->GetDesiredActivePermissions(no_component_extension_->id());
    EXPECT_EQ(0u, no_component_permissions->scriptable_hosts().size());

    // |URLPattern::SCHEME_CHROMEUI| scheme will be added in valid_schemes for
    // component extensions.
    URLPatternSet scriptable_hosts;
    std::string pref_key = "active_permissions.scriptable_host";
    int valid_schemes = URLPattern::SCHEME_ALL & ~URLPattern::SCHEME_CHROMEUI;

    EXPECT_TRUE(prefs()->ReadPrefAsURLPatternSet(component_extension_->id(),
                                                 pref_key, &scriptable_hosts,
                                                 valid_schemes));

    EXPECT_FALSE(prefs()->ReadPrefAsURLPatternSet(no_component_extension_->id(),
                                                  pref_key, &scriptable_hosts,
                                                  valid_schemes));

    // Both extensions should be registered with the ExtensionPrefValueMap.
    // See https://crbug.com/454513.
    EXPECT_TRUE(prefs_.extension_pref_value_map()->CanExtensionControlPref(
        component_extension_->id(), "a_pref", false));
    EXPECT_TRUE(prefs_.extension_pref_value_map()->CanExtensionControlPref(
        no_component_extension_->id(), "a_pref", false));
  }

 private:
  std::unique_ptr<const PermissionSet> active_perms_;
  scoped_refptr<const Extension> component_extension_;
  scoped_refptr<const Extension> no_component_extension_;
};
TEST_F(ExtensionPrefsComponentExtension, ExtensionPrefsComponentExtension) {}

// Tests reading and writing runtime granted permissions.
class ExtensionPrefsRuntimeGrantedPermissions : public ExtensionPrefsTest {
 public:
  ExtensionPrefsRuntimeGrantedPermissions() = default;

  ExtensionPrefsRuntimeGrantedPermissions(
      const ExtensionPrefsRuntimeGrantedPermissions&) = delete;
  ExtensionPrefsRuntimeGrantedPermissions& operator=(
      const ExtensionPrefsRuntimeGrantedPermissions&) = delete;

  ~ExtensionPrefsRuntimeGrantedPermissions() override = default;

  void Initialize() override {
    extension_a_ = prefs_.AddExtension("a");
    extension_b_ = prefs_.AddExtension("b");

    // By default, runtime-granted permissions are empty.
    EXPECT_TRUE(
        prefs()->GetRuntimeGrantedPermissions(extension_a_->id())->IsEmpty());
    EXPECT_TRUE(
        prefs()->GetRuntimeGrantedPermissions(extension_b_->id())->IsEmpty());

    URLPattern example_com(URLPattern::SCHEME_ALL, "https://example.com/*");
    URLPattern chromium_org(URLPattern::SCHEME_ALL, "https://chromium.org/*");

    {
      // Add two hosts to the runtime granted permissions. Verify they were
      // correctly added.
      URLPatternSet added_urls({example_com, chromium_org});
      PermissionSet added_permissions(APIPermissionSet(),
                                      ManifestPermissionSet(),
                                      std::move(added_urls), URLPatternSet());
      prefs()->AddRuntimeGrantedPermissions(extension_a_->id(),
                                            added_permissions);

      std::unique_ptr<const PermissionSet> retrieved_permissions =
          prefs()->GetRuntimeGrantedPermissions(extension_a_->id());
      ASSERT_TRUE(retrieved_permissions);
      EXPECT_EQ(added_permissions, *retrieved_permissions);
    }

    {
      // Remove one of the hosts. The only remaining host should be
      // example.com
      URLPatternSet removed_urls({chromium_org});
      PermissionSet removed_permissions(
          APIPermissionSet(), ManifestPermissionSet(), std::move(removed_urls),
          URLPatternSet());
      prefs()->RemoveRuntimeGrantedPermissions(extension_a_->id(),
                                               removed_permissions);

      URLPatternSet remaining_urls({example_com});
      PermissionSet remaining_permissions(
          APIPermissionSet(), ManifestPermissionSet(),
          std::move(remaining_urls), URLPatternSet());
      std::unique_ptr<const PermissionSet> retrieved_permissions =
          prefs()->GetRuntimeGrantedPermissions(extension_a_->id());
      ASSERT_TRUE(retrieved_permissions);
      EXPECT_EQ(remaining_permissions, *retrieved_permissions);
    }

    // The second extension should still have no runtime-granted permissions.
    EXPECT_TRUE(
        prefs()->GetRuntimeGrantedPermissions(extension_b_->id())->IsEmpty());
  }

  void Verify() override {
    {
      // The first extension should still have example.com as the granted
      // permission.
      URLPattern example_com(URLPattern::SCHEME_ALL, "https://example.com/*");
      URLPatternSet remaining_urls({example_com});
      PermissionSet remaining_permissions(
          APIPermissionSet(), ManifestPermissionSet(),
          std::move(remaining_urls), URLPatternSet());
      std::unique_ptr<const PermissionSet> retrieved_permissions =
          prefs()->GetRuntimeGrantedPermissions(extension_a_->id());
      ASSERT_TRUE(retrieved_permissions);
      EXPECT_EQ(remaining_permissions, *retrieved_permissions);
    }

    EXPECT_TRUE(
        prefs()->GetRuntimeGrantedPermissions(extension_b_->id())->IsEmpty());
  }

 private:
  scoped_refptr<const Extension> extension_a_;
  scoped_refptr<const Extension> extension_b_;
};
TEST_F(ExtensionPrefsRuntimeGrantedPermissions,
       ExtensionPrefsRuntimeGrantedPermissions) {}

// Tests the removal of obsolete keys from extension pref entries.
class ExtensionPrefsObsoletePrefRemoval : public ExtensionPrefsTest {
 public:
  ExtensionPrefsObsoletePrefRemoval() = default;

  ExtensionPrefsObsoletePrefRemoval(const ExtensionPrefsObsoletePrefRemoval&) =
      delete;
  ExtensionPrefsObsoletePrefRemoval& operator=(
      const ExtensionPrefsObsoletePrefRemoval&) = delete;

  ~ExtensionPrefsObsoletePrefRemoval() override = default;

  void Initialize() override {
    extension_ = prefs_.AddExtension("a");
    constexpr char kTestValue[] = "test_value";
    prefs()->UpdateExtensionPref(extension_->id(),
                                 ExtensionPrefs::kFakeObsoletePrefForTesting,
                                 base::Value(kTestValue));
    std::string str_value;
    EXPECT_TRUE(prefs()->ReadPrefAsString(
        extension_->id(), ExtensionPrefs::kFakeObsoletePrefForTesting,
        &str_value));
    EXPECT_EQ(kTestValue, str_value);

    prefs()->MigrateObsoleteExtensionPrefs();
  }

  void Verify() override {
    std::string str_value;
    EXPECT_FALSE(prefs()->ReadPrefAsString(
        extension_->id(), ExtensionPrefs::kFakeObsoletePrefForTesting,
        &str_value));
  }

 private:
  scoped_refptr<const Extension> extension_;
};

TEST_F(ExtensionPrefsObsoletePrefRemoval, ExtensionPrefsObsoletePrefRemoval) {}

// Tests the migration of renamed keys from extension pref entries.
class ExtensionPrefsMigratedPref : public ExtensionPrefsTest {
 public:
  ExtensionPrefsMigratedPref() = default;

  ExtensionPrefsMigratedPref(const ExtensionPrefsMigratedPref&) = delete;
  ExtensionPrefsMigratedPref& operator=(const ExtensionPrefsMigratedPref&) =
      delete;

  ~ExtensionPrefsMigratedPref() override = default;

  void Initialize() override {
    extension_ = prefs_.AddExtension("a");
    prefs()->MigrateObsoleteExtensionPrefs();
  }

  void Verify() override {}

 private:
  scoped_refptr<const Extension> extension_;
};

TEST_F(ExtensionPrefsMigratedPref, ExtensionPrefsMigratedPref) {}

// Tests the removal of obsolete keys from extension pref entries.
class ExtensionPrefsIsExternalExtensionUninstalled : public ExtensionPrefsTest {
 public:
  ExtensionPrefsIsExternalExtensionUninstalled() = default;
  ExtensionPrefsIsExternalExtensionUninstalled(
      const ExtensionPrefsIsExternalExtensionUninstalled& other) = delete;
  ExtensionPrefsIsExternalExtensionUninstalled& operator=(
      const ExtensionPrefsIsExternalExtensionUninstalled& other) = delete;
  ~ExtensionPrefsIsExternalExtensionUninstalled() override = default;

  void Initialize() override {
    uninstalled_external_id_ =
        prefs_
            .AddExtensionWithLocation("external uninstall",
                                      ManifestLocation::kExternalPref)
            ->id();
    uninstalled_by_program_external_id_ =
        prefs_
            .AddExtensionWithLocation("external uninstall by program",
                                      ManifestLocation::kExternalPref)
            ->id();
    installed_external_id_ =
        prefs_
            .AddExtensionWithLocation("external install",
                                      ManifestLocation::kExternalPref)
            ->id();
    uninstalled_internal_id_ =
        prefs_
            .AddExtensionWithLocation("internal uninstall",
                                      ManifestLocation::kInternal)
            ->id();
    installed_internal_id_ =
        prefs_
            .AddExtensionWithLocation("internal install",
                                      ManifestLocation::kInternal)
            ->id();

    prefs()->OnExtensionUninstalled(uninstalled_external_id_,
                                    ManifestLocation::kExternalPref, false);
    prefs()->OnExtensionUninstalled(uninstalled_by_program_external_id_,
                                    ManifestLocation::kExternalPref, true);
    prefs()->OnExtensionUninstalled(uninstalled_internal_id_,
                                    ManifestLocation::kInternal, false);
  }

  void Verify() override {
    EXPECT_TRUE(
        prefs()->IsExternalExtensionUninstalled(uninstalled_external_id_));
    EXPECT_FALSE(prefs()->IsExternalExtensionUninstalled(
        uninstalled_by_program_external_id_));
    EXPECT_FALSE(
        prefs()->IsExternalExtensionUninstalled(installed_external_id_));
    EXPECT_FALSE(
        prefs()->IsExternalExtensionUninstalled(uninstalled_internal_id_));
    EXPECT_FALSE(
        prefs()->IsExternalExtensionUninstalled(installed_internal_id_));
  }

 private:
  std::string uninstalled_external_id_;
  std::string uninstalled_by_program_external_id_;
  std::string installed_external_id_;
  std::string uninstalled_internal_id_;
  std::string installed_internal_id_;
};

TEST_F(ExtensionPrefsIsExternalExtensionUninstalled,
       ExtensionPrefsIsExternalExtensionUninstalled) {}

#if BUILDFLAG(IS_CHROMEOS)
class ExtensionPrefsApplyPendingUpdates
    : public ExtensionPrefsTest,
      public testing::WithParamInterface<std::tuple<std::string, bool>> {
 public:
  void Initialize() override {
    auto [pref, value] = GetParam();
    extension_ = prefs_.AddExtension("apply_pending_updates");
    prefs()->UpdateExtensionPref(extension_->id(), pref + "-pending",
                                 base::Value(value));
  }

  void Verify() override {}

 protected:
  scoped_refptr<Extension> extension_;
};

INSTANTIATE_TEST_SUITE_P(
    PendingUpdates,
    ExtensionPrefsApplyPendingUpdates,
    ::testing::Combine(::testing::Values("newAllowFileAccess", "incognito"),
                       ::testing::Bool()));

TEST_P(ExtensionPrefsApplyPendingUpdates, ExtensionPrefsApplyPendingUpdates) {
  auto id = extension_->id();
  bool actual = false;
  auto [pref, value] = GetParam();

  ASSERT_TRUE(prefs()->HasPrefForExtension(id));
  ASSERT_TRUE(prefs()->ReadPrefAsBoolean(id, pref + "-pending", &actual));
  ASSERT_EQ(actual, value);
  ASSERT_FALSE(prefs()->ReadPrefAsBoolean(id, pref, &actual));

  prefs()->ApplyPendingUpdates();

  ASSERT_FALSE(prefs()->ReadPrefAsBoolean(id, pref + "-pending", &actual));
  ASSERT_TRUE(prefs()->ReadPrefAsBoolean(id, pref, &actual));
  ASSERT_EQ(actual, value);
}
#endif  // BUILDFLAG(IS_CHROMEOS)

////////////////////////////////////////////////////////////////////////////////
// The following are ExtensionPrefs tests that don't use the same
// Initialize(), Verify(), <recreate>, Verify() flow that the others do, and
// instead just use a normal testing::Test setup.

using ExtensionPrefsSimpleTest = testing::Test;

// Tests that raw manipulation of extension disable reasons works and unknown
// values can be written / read back. This also also tests that the non-raw
// getter collapses unknown values to DISABLE_UNKNOWN.
TEST_F(ExtensionPrefsSimpleTest, DisableReasonsRawManipulation) {
  content::BrowserTaskEnvironment task_environment;
  TestExtensionPrefs prefs(base::SingleThreadTaskRunner::GetCurrentDefault());
  std::string extension_id = prefs.AddExtension("Test Extension")->id();

  ExtensionPrefs* extension_prefs = prefs.prefs();
  ASSERT_FALSE(extension_prefs->IsExtensionDisabled(extension_id));

  auto passkey = ExtensionPrefs::DisableReasonRawManipulationPasskey();
  constexpr int kUnknownReason_1 = disable_reason::DISABLE_REASON_LAST + 1;
  constexpr int kUnknownReason_2 = disable_reason::DISABLE_REASON_LAST + 2;
  constexpr int kUnknownReason_3 = disable_reason::DISABLE_REASON_LAST + 3;
  constexpr disable_reason::DisableReason kKnownReason_1 =
      disable_reason::DISABLE_USER_ACTION;
  constexpr disable_reason::DisableReason kKnownReason_2 =
      disable_reason::DISABLE_PERMISSIONS_INCREASE;

  // Disable the extension with known and unknown reasons.
  extension_prefs->ReplaceRawDisableReasons(passkey, extension_id,
                                            {kKnownReason_1, kUnknownReason_1});
  EXPECT_THAT(extension_prefs->GetRawDisableReasons(passkey, extension_id),
              testing::UnorderedElementsAre(kKnownReason_1, kUnknownReason_1));
  EXPECT_THAT(extension_prefs->GetDisableReasons(extension_id),
              testing::UnorderedElementsAre(kKnownReason_1,
                                            disable_reason::DISABLE_UNKNOWN));

  // Add one known and one unknown reason.
  extension_prefs->AddRawDisableReasons(passkey, extension_id,
                                        {kKnownReason_2, kUnknownReason_2});
  EXPECT_THAT(extension_prefs->GetRawDisableReasons(passkey, extension_id),
              testing::UnorderedElementsAre(kKnownReason_1, kUnknownReason_1,
                                            kKnownReason_2, kUnknownReason_2));
  EXPECT_THAT(extension_prefs->GetDisableReasons(extension_id),
              testing::UnorderedElementsAre(kKnownReason_1, kKnownReason_2,
                                            disable_reason::DISABLE_UNKNOWN));

  // Try replacing the disable reason set.
  extension_prefs->ReplaceRawDisableReasons(passkey, extension_id,
                                            {kUnknownReason_3, kKnownReason_1});
  EXPECT_THAT(extension_prefs->GetRawDisableReasons(passkey, extension_id),
              testing::UnorderedElementsAre(kUnknownReason_3, kKnownReason_1));
  EXPECT_THAT(extension_prefs->GetDisableReasons(extension_id),
              testing::UnorderedElementsAre(kKnownReason_1,
                                            disable_reason::DISABLE_UNKNOWN));

  // Try replacing the disable reason set with only known reasons.
  extension_prefs->ReplaceRawDisableReasons(passkey, extension_id,
                                            {kKnownReason_1, kKnownReason_2});
  EXPECT_THAT(extension_prefs->GetRawDisableReasons(passkey, extension_id),
              testing::UnorderedElementsAre(kKnownReason_1, kKnownReason_2));
  EXPECT_THAT(extension_prefs->GetDisableReasons(extension_id),
              testing::UnorderedElementsAre(kKnownReason_1, kKnownReason_2));
}

// Tests the generic Get/Set functions for profile wide extension prefs.
TEST_F(ExtensionPrefsSimpleTest, ProfileExtensionPrefsMapTest) {
  constexpr PrefMap kTestBooleanPref = {"test.boolean", PrefType::kBool,
                                        PrefScope::kProfile};
  constexpr PrefMap kTestIntegerPref = {"test.integer", PrefType::kInteger,
                                        PrefScope::kProfile};
  constexpr PrefMap kTestStringPref = {"test.string", PrefType::kString,
                                       PrefScope::kProfile};
  constexpr PrefMap kTestTimePref = {"test.time", PrefType::kTime,
                                     PrefScope::kProfile};
  constexpr PrefMap kTestGURLPref = {"test.gurl", PrefType::kGURL,
                                     PrefScope::kProfile};
  constexpr PrefMap kTestDictPref = {"test.dict", PrefType::kDictionary,
                                     PrefScope::kProfile};

  content::BrowserTaskEnvironment task_environment_;
  TestExtensionPrefs prefs(base::SingleThreadTaskRunner::GetCurrentDefault());

  auto* registry = prefs.pref_registry().get();
  registry->RegisterBooleanPref(kTestBooleanPref.name, false);
  registry->RegisterIntegerPref(kTestIntegerPref.name, 0);
  registry->RegisterStringPref(kTestStringPref.name, std::string());
  registry->RegisterStringPref(kTestTimePref.name, std::string());
  registry->RegisterStringPref(kTestGURLPref.name, std::string());
  registry->RegisterDictionaryPref(kTestDictPref.name);

  prefs.prefs()->SetBooleanPref(kTestBooleanPref, true);
  prefs.prefs()->SetIntegerPref(kTestIntegerPref, 1);
  prefs.prefs()->SetStringPref(kTestStringPref, "foo");
  base::Time time = base::Time::Now();
  prefs.prefs()->SetTimePref(kTestTimePref, time);
  GURL url = GURL("https://example/com");
  prefs.prefs()->SetGURLPref(kTestGURLPref, url);
  base::Value::Dict dict;
  dict.Set("key", "val");
  prefs.prefs()->SetDictionaryPref(kTestDictPref, std::move(dict));

  EXPECT_TRUE(prefs.prefs()->GetPrefAsBoolean(kTestBooleanPref));
  EXPECT_EQ(prefs.prefs()->GetPrefAsInteger(kTestIntegerPref), 1);
  EXPECT_EQ(prefs.prefs()->GetPrefAsString(kTestStringPref), "foo");
  EXPECT_EQ(prefs.prefs()->GetPrefAsTime(kTestTimePref), time);
  EXPECT_EQ(prefs.prefs()->GetPrefAsGURL(kTestGURLPref), url);
  const std::string* string_ptr =
      prefs.prefs()->GetPrefAsDictionary(kTestDictPref).FindString("key");
  EXPECT_TRUE(string_ptr);
  EXPECT_EQ(*string_ptr, "val");
}

// Tests that the "state" key is removed in per-extension prefs.
TEST_F(ExtensionPrefsSimpleTest, ExtensionEnabledStateMigration) {
  content::BrowserTaskEnvironment task_environment;
  TestExtensionPrefs test_prefs(
      base::SingleThreadTaskRunner::GetCurrentDefault());
  ExtensionPrefs* extension_prefs = test_prefs.prefs();
  constexpr const char kPrefState[] = "state";
  int state = -1;

  // Possible values for the "state" pref.
  enum State {
    DISABLED = 0,
    ENABLED = 1,
    DEPRECATED_EXTERNAL_EXTENSION_UNINSTALLED = 2
  };

  // Extension 1 is disabled with disable reasons. After migration, the state
  // should be removed and the current disable reasons should be retained.
  const ExtensionId extension_1 = test_prefs.AddExtensionAndReturnId("test1");
  extension_prefs->UpdateExtensionPref(extension_1, kPrefState,
                                       base::Value(State::DISABLED));
  const DisableReasonSet extension_1_disable_reasons = {
      disable_reason::DISABLE_EXTERNAL_EXTENSION,
      disable_reason::DISABLE_PERMISSIONS_INCREASE};
  extension_prefs->AddDisableReasons(extension_1, extension_1_disable_reasons);
  EXPECT_TRUE(
      extension_prefs->ReadPrefAsInteger(extension_1, kPrefState, &state));
  EXPECT_EQ(state, State::DISABLED);

  // Extension 2 is disabled without disable reasons. After migration, the
  // state should be removed and DISABLE_USER_ACTION should be added as the
  // disable reason.
  const ExtensionId extension_2 = test_prefs.AddExtensionAndReturnId("test2");
  extension_prefs->UpdateExtensionPref(extension_2, kPrefState,
                                       base::Value(State::DISABLED));
  EXPECT_TRUE(
      extension_prefs->ReadPrefAsInteger(extension_2, kPrefState, &state));
  EXPECT_EQ(state, State::DISABLED);

  // Extension 3 is enabled. It should remain enabled after the migration.
  const ExtensionId extension_3 = test_prefs.AddExtensionAndReturnId("test3");
  extension_prefs->UpdateExtensionPref(extension_3, kPrefState,
                                       base::Value(State::ENABLED));
  EXPECT_TRUE(
      extension_prefs->ReadPrefAsInteger(extension_3, kPrefState, &state));
  EXPECT_EQ(state, State::ENABLED);

  // Cause the migration.
  test_prefs.RecreateExtensionPrefs();
  extension_prefs = test_prefs.prefs();

  // Validate extension 1.
  EXPECT_FALSE(
      extension_prefs->ReadPrefAsInteger(extension_1, kPrefState, &state));
  EXPECT_TRUE(extension_prefs->IsExtensionDisabled(extension_1));
  EXPECT_EQ(extension_prefs->GetDisableReasons(extension_1),
            extension_1_disable_reasons);

  // Validate extension 2.
  EXPECT_FALSE(
      extension_prefs->ReadPrefAsInteger(extension_2, kPrefState, &state));
  EXPECT_TRUE(extension_prefs->IsExtensionDisabled(extension_2));
  EXPECT_THAT(extension_prefs->GetDisableReasons(extension_2),
              testing::ElementsAre(disable_reason::DISABLE_USER_ACTION));

  // Validate extension 3.
  EXPECT_FALSE(
      extension_prefs->ReadPrefAsInteger(extension_3, kPrefState, &state));
  EXPECT_FALSE(extension_prefs->IsExtensionDisabled(extension_3));
  EXPECT_THAT(extension_prefs->GetDisableReasons(extension_3),
              testing::IsEmpty());
}

TEST_F(ExtensionPrefsSimpleTest, DisableReasonsObserverTest) {
  content::BrowserTaskEnvironment task_environment;
  TestExtensionPrefs test_prefs(
      base::SingleThreadTaskRunner::GetCurrentDefault());
  const ExtensionId extension_id = test_prefs.AddExtensionAndReturnId("test");

  class Observer : public ExtensionPrefsObserver {
   public:
    explicit Observer(ExtensionPrefs* prefs) {
      scoped_observation_.Observe(prefs);
    }
    ~Observer() = default;

    MOCK_METHOD(void,
                OnExtensionDisableReasonsChanged,
                (const ExtensionId& extension_id,
                 DisableReasonSet disabled_reasons),
                (override));
    MOCK_METHOD(void,
                OnExtensionStateChanged,
                (const ExtensionId& extension_id, bool state),
                (override));

   private:
    base::ScopedObservation<ExtensionPrefs, ExtensionPrefsObserver>
        scoped_observation_{this};
  };

  ExtensionPrefs* extension_prefs = test_prefs.prefs();
  ASSERT_FALSE(extension_prefs->IsExtensionDisabled(extension_id));

  const disable_reason::DisableReason disable_reason_1 =
      disable_reason::DISABLE_USER_ACTION;
  const disable_reason::DisableReason disable_reason_2 =
      disable_reason::DISABLE_PERMISSIONS_INCREASE;

  // The extension is initially enabled. This test sequentially adds and then
  // removes two disable reasons. This is how the disable reason change:
  //
  // S0: {}
  // S1: {disable_reason_1}
  // S2: {disable_reason_1, disable_reason_2}
  // S3: {disable_reason_1}
  // S4: {}
  //
  // OnExtensionDisableReasonsChanged() should be called for every state, except
  // the initial state (S0).
  //
  // OnExtensionStateChanged() should be called when the first disable reason is
  // added (S1) and when the last disable reason is removed (S4).
  Observer observer(extension_prefs);
  ::testing::InSequence sequence;

  // S1.
  EXPECT_CALL(observer, OnExtensionDisableReasonsChanged(
                            extension_id,
                            testing::UnorderedElementsAre(disable_reason_1)))
      .Times(1);
  EXPECT_CALL(observer, OnExtensionStateChanged(extension_id, false)).Times(1);

  // S2.
  EXPECT_CALL(observer,
              OnExtensionDisableReasonsChanged(
                  extension_id, testing::UnorderedElementsAre(
                                    disable_reason_1, disable_reason_2)))
      .Times(1);

  // S3.
  EXPECT_CALL(observer, OnExtensionDisableReasonsChanged(
                            extension_id,
                            testing::UnorderedElementsAre(disable_reason_1)))
      .Times(1);

  // S4.
  EXPECT_CALL(observer, OnExtensionDisableReasonsChanged(extension_id,
                                                         testing::IsEmpty()))
      .Times(1);
  EXPECT_CALL(observer, OnExtensionStateChanged(extension_id, true)).Times(1);

  extension_prefs->AddDisableReason(extension_id, disable_reason_1);
  extension_prefs->AddDisableReason(extension_id, disable_reason_2);
  extension_prefs->RemoveDisableReason(extension_id, disable_reason_2);
  extension_prefs->RemoveDisableReason(extension_id, disable_reason_1);
}

TEST_F(ExtensionPrefsSimpleTest, ExtensionSpecificPrefsMapTest) {
  constexpr PrefMap kTestBooleanPref = {"test.boolean", PrefType::kBool,
                                        PrefScope::kExtensionSpecific};
  constexpr PrefMap kTestIntegerPref = {"test.integer", PrefType::kInteger,
                                        PrefScope::kExtensionSpecific};
  constexpr PrefMap kTestStringPref = {"test.string", PrefType::kString,
                                       PrefScope::kExtensionSpecific};
  constexpr PrefMap kTestDictPref = {"test.dict", PrefType::kDictionary,
                                     PrefScope::kExtensionSpecific};
  constexpr PrefMap kTestListPref = {"test.list", PrefType::kList,
                                     PrefScope::kExtensionSpecific};
  constexpr PrefMap kTestTimePref = {"test.time", PrefType::kTime,
                                     PrefScope::kExtensionSpecific};

  content::BrowserTaskEnvironment task_environment_;
  TestExtensionPrefs prefs(base::SingleThreadTaskRunner::GetCurrentDefault());

  std::string extension_id = prefs.AddExtensionAndReturnId("1");
  prefs.prefs()->SetBooleanPref(extension_id, kTestBooleanPref, true);
  prefs.prefs()->SetIntegerPref(extension_id, kTestIntegerPref, 1);
  prefs.prefs()->SetStringPref(extension_id, kTestStringPref, "foo");
  base::Value::Dict dict;
  dict.Set("key", "val");
  prefs.prefs()->SetDictionaryPref(extension_id, kTestDictPref,
                                   std::move(dict));
  base::Value::List list;
  list.Append("list_val");
  prefs.prefs()->SetListPref(extension_id, kTestListPref, std::move(list));
  base::Time time = base::Time::Now();
  prefs.prefs()->SetTimePref(extension_id, kTestTimePref, time);

  bool bool_value = false;
  EXPECT_TRUE(prefs.prefs()->ReadPrefAsBoolean(extension_id, kTestBooleanPref,
                                               &bool_value));
  EXPECT_TRUE(bool_value);
  int int_value = 0;
  EXPECT_TRUE(prefs.prefs()->ReadPrefAsInteger(extension_id, kTestIntegerPref,
                                               &int_value));
  EXPECT_EQ(int_value, 1);
  std::string string_value;
  EXPECT_TRUE(prefs.prefs()->ReadPrefAsString(extension_id, kTestStringPref,
                                              &string_value));
  EXPECT_EQ(string_value, "foo");

  const base::Value::Dict* dict_val =
      prefs.prefs()->ReadPrefAsDictionary(extension_id, kTestDictPref);
  ASSERT_TRUE(dict_val);
  const std::string* string_ptr = dict_val->FindString("key");
  ASSERT_TRUE(string_ptr);
  EXPECT_EQ(*string_ptr, "val");

  const base::Value::List* list_val =
      prefs.prefs()->ReadPrefAsList(extension_id, kTestListPref);
  ASSERT_TRUE(list_val);
  ASSERT_FALSE(list_val->empty());
  ASSERT_TRUE((*list_val)[0].is_string());
  EXPECT_EQ((*list_val)[0].GetString(), "list_val");

  EXPECT_EQ(time, prefs.prefs()->ReadPrefAsTime(extension_id, kTestTimePref));
}

TEST_F(ExtensionPrefsSimpleTest, HasOnlyDisableReasonTest) {
  content::BrowserTaskEnvironment task_environment;
  TestExtensionPrefs prefs(base::SingleThreadTaskRunner::GetCurrentDefault());
  std::string extension_id = prefs.AddExtension("Test Extension")->id();
  ExtensionPrefs* extension_prefs = prefs.prefs();

  // No disable reasons to begin with.
  EXPECT_FALSE(extension_prefs->HasOnlyDisableReason(
      extension_id, disable_reason::DISABLE_USER_ACTION));

  // Add a disable reason.
  extension_prefs->AddDisableReason(extension_id,
                                    disable_reason::DISABLE_USER_ACTION);
  EXPECT_TRUE(extension_prefs->HasOnlyDisableReason(
      extension_id, disable_reason::DISABLE_USER_ACTION));

  // Add another disable reason.
  extension_prefs->AddDisableReason(extension_id,
                                    disable_reason::DISABLE_EXTERNAL_EXTENSION);
  EXPECT_FALSE(extension_prefs->HasOnlyDisableReason(
      extension_id, disable_reason::DISABLE_USER_ACTION));
  EXPECT_FALSE(extension_prefs->HasOnlyDisableReason(
      extension_id, disable_reason::DISABLE_EXTERNAL_EXTENSION));

  // Remove the first disable reason.
  extension_prefs->RemoveDisableReason(extension_id,
                                       disable_reason::DISABLE_USER_ACTION);
  EXPECT_TRUE(extension_prefs->HasOnlyDisableReason(
      extension_id, disable_reason::DISABLE_EXTERNAL_EXTENSION));
}

TEST_F(ExtensionPrefsSimpleTest, RemoveDisableReasons) {
  content::BrowserTaskEnvironment task_environment;
  TestExtensionPrefs prefs(base::SingleThreadTaskRunner::GetCurrentDefault());
  std::string extension_id = prefs.AddExtension("Test Extension")->id();
  ExtensionPrefs* extension_prefs = prefs.prefs();
  extension_prefs->AddDisableReasons(
      extension_id, {disable_reason::DISABLE_USER_ACTION,
                     disable_reason::DISABLE_EXTERNAL_EXTENSION,
                     disable_reason::DISABLE_CORRUPTED});

  // Remove one disable reason.
  extension_prefs->RemoveDisableReason(extension_id,
                                       disable_reason::DISABLE_USER_ACTION);
  EXPECT_THAT(
      extension_prefs->GetDisableReasons(extension_id),
      testing::UnorderedElementsAre(disable_reason::DISABLE_EXTERNAL_EXTENSION,
                                    disable_reason::DISABLE_CORRUPTED));

  // Try removing a disable reason that doesn't exist.
  extension_prefs->RemoveDisableReasons(extension_id,
                                        {disable_reason::DISABLE_RELOAD});
  EXPECT_THAT(
      extension_prefs->GetDisableReasons(extension_id),
      testing::UnorderedElementsAre(disable_reason::DISABLE_EXTERNAL_EXTENSION,
                                    disable_reason::DISABLE_CORRUPTED));

  // Remove the remaining disable reasons.
  extension_prefs->RemoveDisableReasons(
      extension_id, {disable_reason::DISABLE_EXTERNAL_EXTENSION,
                     disable_reason::DISABLE_CORRUPTED});
  EXPECT_TRUE(extension_prefs->GetDisableReasons(extension_id).empty());
}

}  // namespace extensions
