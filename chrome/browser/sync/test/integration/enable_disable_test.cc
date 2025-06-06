// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/strings/string_util.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/sync/test/integration/bookmarks_helper.h"
#include "chrome/browser/sync/test/integration/sync_service_impl_harness.h"
#include "chrome/browser/sync/test/integration/sync_test.h"
#include "chrome/browser/sync/test/integration/updated_progress_marker_checker.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/sync/base/data_type.h"
#include "components/sync/base/features.h"
#include "components/sync/base/user_selectable_type.h"
#include "components/sync/engine/cycle/entity_change_metric_recording.h"
#include "components/sync/protocol/sync.pb.h"
#include "components/sync/service/glue/sync_transport_data_prefs.h"
#include "components/sync/service/sync_service_impl.h"
#include "components/sync/test/bookmark_entity_builder.h"
#include "components/sync/test/entity_builder_factory.h"
#include "content/public/test/browser_test.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_CHROMEOS)
// To control Floating SSO (= sync of cookies) on ChromeOS.
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#endif  // BUILDFLAG(IS_CHROMEOS)

namespace {

using fake_server::FakeServer;
using syncer::DataType;
using syncer::DataTypeSet;
using syncer::DataTypeToDebugString;
using syncer::UserSelectableType;
using syncer::UserSelectableTypeSet;
using testing::ElementsAre;
using testing::IsEmpty;

const char kSyncedBookmarkURL[] = "http://www.mybookmark.com";
// Non-utf8 string to make sure it gets handled well.
const char kTestServerChips[] = "\xed\xa0\x80\xed\xbf\xbf";

// A FakeServer observer than saves all issued GetUpdates requests to a vector.
class GetUpdatesRequestRecorder : public FakeServer::Observer {
 public:
  explicit GetUpdatesRequestRecorder(FakeServer* fake_server) {
    CHECK(fake_server);
    observation_.Observe(fake_server);
  }

  ~GetUpdatesRequestRecorder() override = default;

  const std::vector<sync_pb::ClientToServerMessage>& recorded_requests() const {
    return recorded_requests_;
  }

  // FakeServer::Observer overrides.
  void OnWillGetUpdates(
      const sync_pb::ClientToServerMessage& message) override {
    recorded_requests_.push_back(message);
  }

 private:
  std::vector<sync_pb::ClientToServerMessage> recorded_requests_;
  base::ScopedObservation<FakeServer, FakeServer::Observer> observation_{this};
};

MATCHER_P2(MatchesGetUpdatesRequest, origin, data_type_set, "") {
  if (!testing::ExplainMatchResult(
          origin, arg.get_updates().get_updates_origin(), result_listener)) {
    *result_listener << "Unexpected origin "
                     << arg.get_updates().get_updates_origin();
    return false;
  }

  DataTypeSet actual_data_types;
  for (const sync_pb::DataTypeProgressMarker& marker :
       arg.get_updates().from_progress_marker()) {
    actual_data_types.Put(
        syncer::GetDataTypeFromSpecificsFieldNumber(marker.data_type_id()));
  }
  return testing::ExplainMatchResult(data_type_set, actual_data_types,
                                     result_listener);
}

// Some types show up in multiple groups. This means that there are at least two
// user selectable groups that will cause these types to become enabled. This
// affects our tests because we cannot assume that before enabling a multi type
// it will be disabled, because the other selectable type(s) could already be
// enabling it. And vice versa for disabling.
DataTypeSet MultiGroupTypes(const DataTypeSet& registered_types) {
  DataTypeSet seen;
  DataTypeSet multi;
  for (UserSelectableType type : UserSelectableTypeSet::All()) {
    const DataTypeSet grouped_types =
        syncer::UserSelectableTypeToAllDataTypes(type);
    for (DataType grouped_type : grouped_types) {
      if (seen.Has(grouped_type)) {
        multi.Put(grouped_type);
      } else {
        seen.Put(grouped_type);
      }
    }
  }
  multi.RetainAll(registered_types);
  return multi;
}

// This test enables and disables types and verifies the type is active via
// SyncService::GetActiveDataTypes().
class EnableDisableSingleClientTest : public SyncTest {
 public:
  EnableDisableSingleClientTest() : SyncTest(SINGLE_CLIENT) {}

  EnableDisableSingleClientTest(const EnableDisableSingleClientTest&) = delete;
  EnableDisableSingleClientTest& operator=(
      const EnableDisableSingleClientTest&) = delete;

  ~EnableDisableSingleClientTest() override = default;

  // Don't use self-notifications as they can trigger additional sync cycles.
  bool TestUsesSelfNotifications() override { return false; }

  bool IsDataTypeActive(DataType type) {
    return GetSyncService(0)->GetActiveDataTypes().Has(type);
  }

  void InjectSyncedBookmark() {
    fake_server::BookmarkEntityBuilder bookmark_builder =
        entity_builder_factory_.NewBookmarkEntityBuilder("My Bookmark");
    GetFakeServer()->InjectEntity(
        bookmark_builder.BuildBookmark(GURL(kSyncedBookmarkURL)));
  }

  int GetNumUpdatesDownloadedInLastCycle() {
    return GetSyncService(0)
        ->GetLastCycleSnapshotForDebugging()
        .model_neutral_state()
        .num_updates_downloaded_total;
  }

 protected:
  void SetupTest(bool all_types_enabled) {
    ASSERT_TRUE(SetupClients());

#if BUILDFLAG(IS_CHROMEOS)
    // This unblocks sync of cookies on ChromeOS, see dedicated controller
    // CookieSyncDataTypeController. The tests in this file are not prepared
    // to handle selectable datatypes which are disabled by default via their
    // DataTypeController, so we have to enable the pref for them to pass.
    // TODO(crbug.com/378091718): think if we can also make the tests pass with
    // this preference disabled.
    GetProfile(0)->GetPrefs()->SetBoolean(::prefs::kFloatingSsoEnabled, true);
#endif  // BUILDFLAG(IS_CHROMEOS)

    ASSERT_TRUE(
        GetClient(0)->SetupSyncWithCustomSettings(base::BindLambdaForTesting(
            [all_types_enabled](syncer::SyncUserSettings* user_settings) {
              user_settings->SetSelectedTypes(all_types_enabled, {});
#if !BUILDFLAG(IS_CHROMEOS)
              user_settings->SetInitialSyncFeatureSetupComplete(
                  syncer::SyncFirstSetupCompleteSource::ADVANCED_FLOW_CONFIRM);
#endif  // !BUILDFLAG(IS_CHROMEOS)
            })));

    registered_data_types_ = GetSyncService(0)->GetRegisteredDataTypesForTest();

    multi_grouped_types_ = MultiGroupTypes(registered_data_types_);
    registered_selectable_types_ = GetRegisteredSelectableTypes(0);
  }

  DataTypeSet ResolveGroup(UserSelectableType type) {
    DataTypeSet grouped_types = syncer::UserSelectableTypeToAllDataTypes(type);
    grouped_types.RetainAll(registered_data_types_);
    return grouped_types;
  }

  DataTypeSet WithoutMultiTypes(const DataTypeSet& input) {
    return Difference(input, multi_grouped_types_);
  }

  DataTypeSet registered_data_types_;
  DataTypeSet multi_grouped_types_;
  UserSelectableTypeSet registered_selectable_types_;

 private:
  fake_server::EntityBuilderFactory entity_builder_factory_;
};

IN_PROC_BROWSER_TEST_F(EnableDisableSingleClientTest, PRE_EnableAndRestart) {
  GetUpdatesRequestRecorder get_updates_recorder(GetFakeServer());

  SetupTest(/*all_types_enabled=*/true);

  // Commit-only types don't issue GetUpdates (by definition) and supervised
  // user types are also excluded in this test, because the account being used
  // isn't supervised. Finally, a few types aren't launched so they should also
  // be excluded.
  const DataTypeSet types_without_updates =
      Union(syncer::CommitOnlyTypes(),
            {syncer::SUPERVISED_USER_SETTINGS, syncer::PLUS_ADDRESS,
             syncer::PLUS_ADDRESS_SETTING});
  // High priority types in this test are a subset of
  // syncer::HighPriorityUserTypes(), excluding those identified earlier.
  const DataTypeSet high_priority_types = Difference(
      Intersection(syncer::HighPriorityUserTypes(), registered_data_types_),
      types_without_updates);
  // Similarly, low priority types in this test are a subset of
  // syncer::LowPriorityUserTypes().
  const DataTypeSet low_priority_types = Difference(
      Intersection(syncer::LowPriorityUserTypes(), registered_data_types_),
      types_without_updates);
  // All other types have regular priority.
  const DataTypeSet regular_priority_types = Difference(
      registered_data_types_,
      Union(types_without_updates, Union(syncer::HighPriorityUserTypes(),
                                         syncer::LowPriorityUserTypes())));

  // Initial sync takes four GetUpdates requests to the server to download:
  // 1. Control types (NIGORI).
  // 2. High-priority user types.
  // 3. Regular-priority types.
  // 4. Low-priority types.
  //
  // The sequence of GetUpdatesOrigin below matches what is empirically
  // observed outside tests when Sync is turned on during desktop FRE using the
  // advanced sync setup flow (open settings), which is also what tests mimic.
  EXPECT_THAT(
      get_updates_recorder.recorded_requests(),
      ElementsAre(MatchesGetUpdatesRequest(sync_pb::SyncEnums::NEW_CLIENT,
                                           syncer::ControlTypes()),
                  MatchesGetUpdatesRequest(
                      sync_pb::SyncEnums::NEW_CLIENT,
                      Union(syncer::ControlTypes(), high_priority_types)),
                  MatchesGetUpdatesRequest(
                      sync_pb::SyncEnums::NEW_CLIENT,
                      Union(syncer::ControlTypes(), regular_priority_types)),
                  MatchesGetUpdatesRequest(
                      sync_pb::SyncEnums::NEW_CLIENT,
                      Union(syncer::ControlTypes(), low_priority_types))));
}

IN_PROC_BROWSER_TEST_F(EnableDisableSingleClientTest, EnableAndRestart) {
  GetUpdatesRequestRecorder get_updates_recorder(GetFakeServer());

  ASSERT_TRUE(SetupClients());

  EXPECT_TRUE(GetClient(0)->AwaitEngineInitialization());

  for (UserSelectableType type : UserSelectableTypeSet::All()) {
    for (DataType data_type : ResolveGroup(type)) {
      EXPECT_TRUE(IsDataTypeActive(data_type))
          << " for " << DataTypeToDebugString(data_type);
    }
  }

  EXPECT_THAT(get_updates_recorder.recorded_requests(), IsEmpty());
}

IN_PROC_BROWSER_TEST_F(EnableDisableSingleClientTest, EnableOneAtATime) {
  // Setup sync with no enabled types.
  SetupTest(/*all_types_enabled=*/false);

  // Certain datatypes like SESSIONS can be configured by multiple
  // user-selectable types. Hence, enabling a new user-selectable type doesn't
  // necessarily mean that the datatype (SESSIONS) will be newly-configured. In
  // this particular test, this influences whether the engine will issue UMA
  // corresponding to the configuration cycle.
  syncer::DataTypeSet previously_active_types;

  for (UserSelectableType type : registered_selectable_types_) {
    const DataTypeSet grouped_types = ResolveGroup(type);
    for (DataType single_grouped_type : WithoutMultiTypes(grouped_types)) {
      ASSERT_FALSE(IsDataTypeActive(single_grouped_type))
          << " for " << GetUserSelectableTypeName(type);
    }

    base::HistogramTester histogram_tester;
    EXPECT_TRUE(GetClient(0)->EnableSyncForType(type));

    for (DataType grouped_type : grouped_types) {
      EXPECT_TRUE(IsDataTypeActive(grouped_type))
          << " for " << GetUserSelectableTypeName(type);

      if (!syncer::ProtocolTypes().Has(grouped_type) ||
          syncer::CommitOnlyTypes().Has(grouped_type)) {
        EXPECT_EQ(0,
                  histogram_tester.GetBucketCount(
                      "Sync.PostedDataTypeGetUpdatesRequest",
                      static_cast<int>(DataTypeHistogramValue(grouped_type))))
            << " for " << DataTypeToDebugString(grouped_type);
      } else if (previously_active_types.Has(grouped_type)) {
        // If the type was already configured, no additional configuration cycle
        // is expected, but it's impossible to rule out that the type has issued
        // a GetUpdates request for different reasons (since it's actively
        // sync-ing).
      } else {
        EXPECT_NE(0,
                  histogram_tester.GetBucketCount(
                      "Sync.PostedDataTypeGetUpdatesRequest",
                      static_cast<int>(DataTypeHistogramValue(grouped_type))))
            << " for " << DataTypeToDebugString(grouped_type);
      }

      previously_active_types.Put(grouped_type);
    }
  }
}

IN_PROC_BROWSER_TEST_F(EnableDisableSingleClientTest, DisableOneAtATime) {
  // Setup sync with no disabled types.
  SetupTest(/*all_types_enabled=*/true);

  for (UserSelectableType type : registered_selectable_types_) {
    const DataTypeSet grouped_types = ResolveGroup(type);
    for (DataType grouped_type : grouped_types) {
      ASSERT_TRUE(IsDataTypeActive(grouped_type))
          << " for " << GetUserSelectableTypeName(type);
    }

    EXPECT_TRUE(GetClient(0)->DisableSyncForType(type));

    for (DataType single_grouped_type : WithoutMultiTypes(grouped_types)) {
      EXPECT_FALSE(IsDataTypeActive(single_grouped_type))
          << " for " << GetUserSelectableTypeName(type);
    }
  }

  // Lastly make sure that all the multi grouped times are all gone, since we
  // did not check these after disabling inside the above loop.
  for (DataType multi_grouped_type : multi_grouped_types_) {
    EXPECT_FALSE(IsDataTypeActive(multi_grouped_type))
        << " for " << DataTypeToDebugString(multi_grouped_type);
  }
}

IN_PROC_BROWSER_TEST_F(EnableDisableSingleClientTest,
                       FastEnableDisableOneAtATime) {
  // Setup sync with no enabled types.
  SetupTest(/*all_types_enabled=*/false);

  for (UserSelectableType type : registered_selectable_types_) {
    const DataTypeSet grouped_types = ResolveGroup(type);
    const DataTypeSet single_grouped_types = WithoutMultiTypes(grouped_types);
    for (DataType single_grouped_type : single_grouped_types) {
      ASSERT_FALSE(IsDataTypeActive(single_grouped_type))
          << " for " << GetUserSelectableTypeName(type);
    }

    // Enable and then disable immediately afterwards, before the datatype has
    // had the chance to finish startup (which usually involves task posting).
    EXPECT_TRUE(GetClient(0)->EnableSyncForType(type));
    EXPECT_TRUE(GetClient(0)->DisableSyncForType(type));

    for (DataType single_grouped_type : single_grouped_types) {
      EXPECT_FALSE(IsDataTypeActive(single_grouped_type))
          << " for " << GetUserSelectableTypeName(type);
    }
  }

  // Lastly make sure that all the multi grouped times are all gone, since we
  // did not check these after disabling inside the above loop.
  for (DataType multi_grouped_type : multi_grouped_types_) {
    EXPECT_FALSE(IsDataTypeActive(multi_grouped_type))
        << " for " << DataTypeToDebugString(multi_grouped_type);
  }
}

IN_PROC_BROWSER_TEST_F(EnableDisableSingleClientTest,
                       FastDisableEnableOneAtATime) {
  // Setup sync with no disabled types.
  SetupTest(/*all_types_enabled=*/true);

  for (UserSelectableType type : registered_selectable_types_) {
    const DataTypeSet grouped_types = ResolveGroup(type);
    for (DataType grouped_type : grouped_types) {
      ASSERT_TRUE(IsDataTypeActive(grouped_type))
          << " for " << GetUserSelectableTypeName(type);
    }

    // Disable and then reenable immediately afterwards, before the datatype has
    // had the chance to stop fully (which usually involves task posting).
    EXPECT_TRUE(GetClient(0)->DisableSyncForType(type));
    EXPECT_TRUE(GetClient(0)->EnableSyncForType(type));

    for (DataType grouped_type : grouped_types) {
      EXPECT_TRUE(IsDataTypeActive(grouped_type))
          << " for " << GetUserSelectableTypeName(type);
    }
  }
}

IN_PROC_BROWSER_TEST_F(EnableDisableSingleClientTest,
                       FastEnableDisableEnableOneAtATime) {
  // Setup sync with no enabled types.
  SetupTest(/*all_types_enabled=*/false);

  for (UserSelectableType type : registered_selectable_types_) {
    const DataTypeSet single_grouped_types =
        WithoutMultiTypes(ResolveGroup(type));
    for (DataType single_grouped_type : single_grouped_types) {
      ASSERT_FALSE(IsDataTypeActive(single_grouped_type))
          << " for " << GetUserSelectableTypeName(type);
    }

    // Fast enable-disable-enable sequence, before the datatype has had the
    // chance to transition fully across states (usually involves task posting).
    EXPECT_TRUE(GetClient(0)->EnableSyncForType(type));
    EXPECT_TRUE(GetClient(0)->DisableSyncForType(type));
    EXPECT_TRUE(GetClient(0)->EnableSyncForType(type));

    for (DataType single_grouped_type : single_grouped_types) {
      EXPECT_TRUE(IsDataTypeActive(single_grouped_type))
          << " for " << GetUserSelectableTypeName(type);
    }
  }
}

IN_PROC_BROWSER_TEST_F(EnableDisableSingleClientTest, EnableDisable) {
  SetupTest(/*all_types_enabled=*/false);

  // Enable all, and then disable immediately afterwards, before datatypes
  // have had the chance to finish startup (which usually involves task
  // posting).
  ASSERT_TRUE(GetClient(0)->EnableSyncForRegisteredDatatypes());
  ASSERT_TRUE(GetClient(0)->DisableSyncForAllDatatypes());

  for (UserSelectableType type : UserSelectableTypeSet::All()) {
    for (DataType grouped_type : ResolveGroup(type)) {
      EXPECT_FALSE(IsDataTypeActive(grouped_type))
          << " for " << GetUserSelectableTypeName(type);
    }
  }
}

IN_PROC_BROWSER_TEST_F(EnableDisableSingleClientTest, FastEnableDisableEnable) {
  SetupTest(/*all_types_enabled=*/false);

  // Enable all, and then disable+reenable immediately afterwards, before
  // datatypes have had the chance to finish startup (which usually involves
  // task posting).
  ASSERT_TRUE(GetClient(0)->EnableSyncForRegisteredDatatypes());
  ASSERT_TRUE(GetClient(0)->DisableSyncForAllDatatypes());
  ASSERT_TRUE(GetClient(0)->EnableSyncForRegisteredDatatypes());

  for (UserSelectableType type : UserSelectableTypeSet::All()) {
    for (DataType data_type : ResolveGroup(type)) {
      EXPECT_TRUE(IsDataTypeActive(data_type))
          << " for " << DataTypeToDebugString(data_type);
    }
  }
}

// This test makes sure that after a signout, Sync data gets redownloaded
// when Sync is started again. This does not actually verify that the data is
// gone from disk (which seems infeasible); it's mostly here as a baseline for
// the following tests.
//
// ChromeOS does not support signing out of a primary account.
#if !BUILDFLAG(IS_CHROMEOS)
IN_PROC_BROWSER_TEST_F(EnableDisableSingleClientTest, RedownloadsAfterSignout) {
  ASSERT_TRUE(SetupClients());
  ASSERT_FALSE(bookmarks_helper::GetBookmarkModel(0)->IsBookmarked(
      GURL(kSyncedBookmarkURL)));

  // Create a bookmark on the server, then turn on Sync on the client.
  InjectSyncedBookmark();
  // Disable any LowPriorityUserTypes() (in practice, history, and incoming
  // password sharing invitations controlled by Passwords data type): This test
  // inspects the last-sync-cycle state. If low-prio types are active, they
  // cause another (uninteresting) cycle and mess up the stats we're interested
  // in.
  // TODO(crbug.com/40215602): Rewrite this test to avoid disabling low priotiy
  // types.
  ASSERT_TRUE(GetClient(0)->SetupSyncWithCustomSettings(
      base::BindOnce([](syncer::SyncUserSettings* settings) {
        UserSelectableTypeSet types = settings->GetRegisteredSelectableTypes();
        types.Remove(syncer::UserSelectableType::kHistory);
        types.Remove(syncer::UserSelectableType::kPasswords);
        settings->SetSelectedTypes(/*sync_everything=*/false, types);
#if !BUILDFLAG(IS_CHROMEOS)
        settings->SetInitialSyncFeatureSetupComplete(
            syncer::SyncFirstSetupCompleteSource::ADVANCED_FLOW_CONFIRM);
#endif  // !BUILDFLAG(IS_CHROMEOS)
      })));
  ASSERT_TRUE(GetSyncService(0)->IsSyncFeatureActive());
  ASSERT_FALSE(GetSyncService(0)->GetActiveDataTypes().HasAny(
      syncer::LowPriorityUserTypes()));

  // Make sure the bookmark got synced down.
  ASSERT_TRUE(bookmarks_helper::GetBookmarkModel(0)->IsBookmarked(
      GURL(kSyncedBookmarkURL)));
  // Note: The response may also contain permanent nodes, so we can't check the
  // exact count.
  const int initial_updates_downloaded = GetNumUpdatesDownloadedInLastCycle();
  ASSERT_GT(initial_updates_downloaded, 0);

  // Stop and restart Sync.
  GetClient(0)->SignOutPrimaryAccount();
  ASSERT_TRUE(GetClient(0)->SetupSync());
  ASSERT_TRUE(GetSyncService(0)->IsSyncFeatureActive());

  // Everything should have been redownloaded.
  ASSERT_TRUE(bookmarks_helper::GetBookmarkModel(0)->IsBookmarked(
      GURL(kSyncedBookmarkURL)));
  EXPECT_EQ(GetNumUpdatesDownloadedInLastCycle(), initial_updates_downloaded);
}
#endif  // !BUILDFLAG(IS_CHROMEOS)

IN_PROC_BROWSER_TEST_F(EnableDisableSingleClientTest,
                       DoesNotRedownloadAfterSyncUnpaused) {
  ASSERT_TRUE(SetupClients());
  ASSERT_FALSE(bookmarks_helper::GetBookmarkModel(0)->IsBookmarked(
      GURL(kSyncedBookmarkURL)));

  // Create a bookmark on the server, then turn on Sync on the client.
  InjectSyncedBookmark();
  // Disable any LowPriorityUserTypes() (in practice, history, and incoming
  // password sharing invitations controlled by Passwords data type): This test
  // inspects the last-sync-cycle state. If low-prio types are active, they
  // cause another (uninteresting) cycle and mess up the stats we're interested
  // in.
  // TODO(crbug.com/40215602): Rewrite this test to avoid disabling low priotiy
  // types.
  ASSERT_TRUE(GetClient(0)->SetupSyncWithCustomSettings(
      base::BindOnce([](syncer::SyncUserSettings* settings) {
        UserSelectableTypeSet types = settings->GetRegisteredSelectableTypes();
        types.Remove(syncer::UserSelectableType::kHistory);
        types.Remove(syncer::UserSelectableType::kPasswords);
        settings->SetSelectedTypes(/*sync_everything=*/false, types);
#if !BUILDFLAG(IS_CHROMEOS)
        settings->SetInitialSyncFeatureSetupComplete(
            syncer::SyncFirstSetupCompleteSource::ADVANCED_FLOW_CONFIRM);
#endif  // !BUILDFLAG(IS_CHROMEOS)
      })));
  ASSERT_TRUE(GetSyncService(0)->IsSyncFeatureActive());
  ASSERT_FALSE(GetSyncService(0)->GetActiveDataTypes().HasAny(
      syncer::LowPriorityUserTypes()));

  // Make sure the bookmark got synced down.
  ASSERT_TRUE(bookmarks_helper::GetBookmarkModel(0)->IsBookmarked(
      GURL(kSyncedBookmarkURL)));
  // Note: The response may also contain permanent nodes, so we can't check the
  // exact count.
  ASSERT_GT(GetNumUpdatesDownloadedInLastCycle(), 0);

  // Pause sync.
  GetClient(0)->EnterSyncPausedStateForPrimaryAccount();
  ASSERT_FALSE(GetSyncService(0)->IsSyncFeatureActive());

  // Resume sync.
  base::HistogramTester histogram_tester;
  GetClient(0)->ExitSyncPausedStateForPrimaryAccount();
  ASSERT_TRUE(GetSyncService(0)->IsSyncFeatureActive());

  // The bookmark should still be there, *without* having been redownloaded.
  ASSERT_TRUE(SetupSync());
  ASSERT_TRUE(bookmarks_helper::GetBookmarkModel(0)->IsBookmarked(
      GURL(kSyncedBookmarkURL)));
  EXPECT_EQ(0, histogram_tester.GetBucketCount(
                   "Sync.DataTypeEntityChange.BOOKMARK",
                   syncer::DataTypeEntityChange::kRemoteNonInitialUpdate));
  EXPECT_EQ(0, histogram_tester.GetBucketCount(
                   "Sync.DataTypeEntityChange.BOOKMARK",
                   syncer::DataTypeEntityChange::kRemoteInitialUpdate));
}

IN_PROC_BROWSER_TEST_F(EnableDisableSingleClientTest,
                       DoesNotClearPrefsWithKeepData) {
  SetupTest(/*all_types_enabled=*/true);

  syncer::SyncTransportDataPrefs prefs(
      GetProfile(0)->GetPrefs(),
      GetClient(0)->GetGaiaIdHashForPrimaryAccount());
  const std::string cache_guid = prefs.GetCacheGuid();
  ASSERT_NE("", cache_guid);

  GetClient(0)->EnterSyncPausedStateForPrimaryAccount();
  EXPECT_EQ(cache_guid, prefs.GetCacheGuid());
}

class EnableDisableSingleClientSelfNotifyTest
    : public EnableDisableSingleClientTest {
 public:
  // UpdatedProgressMarkerChecker relies on the 'self-notify' feature.
  bool TestUsesSelfNotifications() override { return true; }

  sync_pb::ClientToServerMessage TriggerGetUpdatesCycleAndWait() {
    TriggerSyncForDataTypes(0, {syncer::BOOKMARKS});
    EXPECT_TRUE(UpdatedProgressMarkerChecker(GetSyncService(0)).Wait());

    sync_pb::ClientToServerMessage message;
    EXPECT_TRUE(GetFakeServer()->GetLastGetUpdatesMessage(&message));
    return message;
  }
};

IN_PROC_BROWSER_TEST_F(EnableDisableSingleClientSelfNotifyTest,
                       PRE_ResendsBagOfChips) {
  sync_pb::ChipBag bag_of_chips;
  bag_of_chips.set_server_chips(kTestServerChips);
  ASSERT_FALSE(base::IsStringUTF8(bag_of_chips.SerializeAsString()));
  GetFakeServer()->SetBagOfChips(bag_of_chips);

  SetupTest(/*all_types_enabled=*/true);

  syncer::SyncTransportDataPrefs prefs(
      GetProfile(0)->GetPrefs(),
      GetClient(0)->GetGaiaIdHashForPrimaryAccount());
  EXPECT_EQ(bag_of_chips.SerializeAsString(), prefs.GetBagOfChips());

  sync_pb::ClientToServerMessage message = TriggerGetUpdatesCycleAndWait();
  EXPECT_TRUE(message.has_bag_of_chips());
  EXPECT_EQ(kTestServerChips, message.bag_of_chips().server_chips());
}

IN_PROC_BROWSER_TEST_F(EnableDisableSingleClientSelfNotifyTest,
                       ResendsBagOfChips) {
  ASSERT_TRUE(SetupClients());
  syncer::SyncTransportDataPrefs prefs(
      GetProfile(0)->GetPrefs(),
      GetClient(0)->GetGaiaIdHashForPrimaryAccount());
  ASSERT_NE("", prefs.GetBagOfChips());
  ASSERT_TRUE(GetClient(0)->AwaitSyncSetupCompletion());

  sync_pb::ClientToServerMessage message = TriggerGetUpdatesCycleAndWait();
  EXPECT_TRUE(message.has_bag_of_chips());
  EXPECT_EQ(kTestServerChips, message.bag_of_chips().server_chips());
}

}  // namespace
