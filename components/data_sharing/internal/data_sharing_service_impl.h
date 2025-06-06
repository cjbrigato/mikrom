// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_DATA_SHARING_INTERNAL_DATA_SHARING_SERVICE_IMPL_H_
#define COMPONENTS_DATA_SHARING_INTERNAL_DATA_SHARING_SERVICE_IMPL_H_

#include <set>
#include <unordered_map>

#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "base/version_info/channel.h"
#include "components/data_sharing/internal/collaboration_group_sync_bridge.h"
#include "components/data_sharing/internal/group_data_model.h"
#include "components/data_sharing/internal/preview_server_proxy.h"
#include "components/data_sharing/public/data_sharing_sdk_delegate.h"
#include "components/data_sharing/public/data_sharing_service.h"
#include "components/data_sharing/public/data_sharing_ui_delegate.h"
#include "components/sync/model/data_type_controller_delegate.h"
#include "components/sync/model/data_type_store.h"
#include "components/sync/model/data_type_sync_bridge.h"
#include "third_party/abseil-cpp/absl/status/status.h"

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace signin {
class IdentityManager;
}  // namespace signin

namespace image_fetcher {
class ImageFetcher;
}  // namespace image_fetcher

namespace data_sharing_pb {

class AddAccessTokenResult;
class CreateGroupResult;
class LookupGaiaIdByEmailResult;
class ReadGroupsResult;
class LookupGaiaIdByEmailResult;

}  // namespace data_sharing_pb

namespace data_sharing {
class DataSharingNetworkLoader;
class PreviewServerProxy;
class AvatarFetcher;
class Logger;

// The internal implementation of the DataSharingService.
class DataSharingServiceImpl : public DataSharingService,
                               public GroupDataModel::Observer {
 public:
  // `identity_manager` must not be null and must outlive this object.
  // `sdk_delegate` is nullable, indicating that SDK is not available.
  DataSharingServiceImpl(
      const base::FilePath& profile_dir,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      signin::IdentityManager* identity_manager,
      syncer::OnceDataTypeStoreFactory data_type_store_factory,
      version_info::Channel channel,
      std::unique_ptr<DataSharingSDKDelegate> sdk_delegate,
      std::unique_ptr<DataSharingUIDelegate> ui_delegate);
  ~DataSharingServiceImpl() override;

  // Disallow copy/assign.
  DataSharingServiceImpl(const DataSharingServiceImpl&) = delete;
  DataSharingServiceImpl& operator=(const DataSharingServiceImpl&) = delete;

  // DataSharingService implementation.
  bool IsEmptyService() override;
  void AddObserver(DataSharingService::Observer* observer) override;
  void RemoveObserver(DataSharingService::Observer* observer) override;
  DataSharingNetworkLoader* GetDataSharingNetworkLoader() override;
  base::WeakPtr<syncer::DataTypeControllerDelegate>
  GetCollaborationGroupControllerDelegate() override;
  bool IsGroupDataModelLoaded() override;
  std::optional<GroupData> ReadGroup(const GroupId& group_id) override;
  std::set<GroupData> ReadAllGroups() override;
  std::optional<GroupMemberPartialData> GetPossiblyRemovedGroupMember(
      const GroupId& group_id,
      const GaiaId& member_gaia_id) override;
  std::optional<GroupData> GetPossiblyRemovedGroup(
      const GroupId& group_id) override;
  void ReadGroupDeprecated(
      const GroupId& group_id,
      base::OnceCallback<void(const GroupDataOrFailureOutcome&)> callback)
      override;
  void ReadNewGroup(const GroupToken& token,
                    base::OnceCallback<void(const GroupDataOrFailureOutcome&)>
                        callback) override;
  void CreateGroup(const std::string& group_name,
                   base::OnceCallback<void(const GroupDataOrFailureOutcome&)>
                       callback) override;
  void DeleteGroup(
      const GroupId& group_id,
      base::OnceCallback<void(PeopleGroupActionOutcome)> callback) override;
  void InviteMember(
      const GroupId& group_id,
      const std::string& invitee_email,
      base::OnceCallback<void(PeopleGroupActionOutcome)> callback) override;
  void AddMember(
      const GroupId& group_id,
      const std::string& access_token,
      base::OnceCallback<void(PeopleGroupActionOutcome)> callback) override;
  void RemoveMember(
      const GroupId& group_id,
      const std::string& member_email,
      base::OnceCallback<void(PeopleGroupActionOutcome)> callback) override;
  void LeaveGroup(
      const GroupId& group_id,
      base::OnceCallback<void(PeopleGroupActionOutcome)> callback) override;
  bool IsLeavingOrDeletingGroup(const GroupId& group_id) override;
  std::vector<GroupEvent> GetGroupEventsSinceStartup() override;
  void HandleShareURLNavigationIntercepted(
      const GURL& url,
      std::unique_ptr<ShareURLInterceptionContext> context) override;
  std::unique_ptr<GURL> GetDataSharingUrl(const GroupData& group_data) override;
  void Shutdown() override;
  void EnsureGroupVisibility(
      const GroupId& group_id,
      base::OnceCallback<void(const GroupDataOrFailureOutcome&)> callback)
      override;
  void GetSharedEntitiesPreview(
      const GroupToken& group_token,
      base::OnceCallback<void(const SharedDataPreviewOrFailureOutcome&)>
          callback) override;
  void GetAvatarImageForURL(
      const GURL& avatar_url,
      int size,
      base::OnceCallback<void(const gfx::Image&)> callback,
      image_fetcher::ImageFetcher* image_fetcher) override;
  void SetSDKDelegate(
      std::unique_ptr<DataSharingSDKDelegate> sdk_delegate) override;
  void SetUIDelegate(
      std::unique_ptr<DataSharingUIDelegate> ui_delegate) override;
  DataSharingUIDelegate* GetUiDelegate() override;
  Logger* GetLogger() override;
  void AddGroupDataForTesting(GroupData group_data) override;
  void SetPreviewServerProxyForTesting(
      std::unique_ptr<PreviewServerProxy> preview_server_proxy) override;
  PreviewServerProxy* GetPreviewServerProxyForTesting() override;
  void OnCollaborationGroupRemoved(const GroupId& group_id) override;

  // GroupDataModel::Observer implementation.
  void OnModelLoaded() override;
  void OnGroupAdded(const GroupId& group_id,
                    const base::Time& event_time) override;
  void OnGroupUpdated(const GroupId& group_id,
                      const base::Time& event_time) override;
  void OnGroupDeleted(const GroupId& group_id,
                      const std::optional<GroupData>& group_data,
                      const base::Time& event_time) override;
  void OnMemberAdded(const GroupId& group_id,
                     const GaiaId& member_gaia_id,
                     const base::Time& event_time) override;
  void OnMemberRemoved(const GroupId& group_id,
                       const GaiaId& member_gaia_id,
                       const base::Time& event_time) override;
  void OnSyncBridgeUpdateTypeChanged(
      SyncBridgeUpdateType sync_bridge_update_type) override;

  CollaborationGroupSyncBridge* GetCollaborationGroupSyncBridgeForTesting();

  // Utillity to create URL from `group_token`. See
  // DataSharingService::GetDataSharingUrl().
  static std::unique_ptr<GURL> GetDataSharingUrl(const GroupToken& group_token);

 private:
  void OnReadSingleGroupCompleted(
      base::OnceCallback<void(const GroupDataOrFailureOutcome&)> callback,
      const base::expected<data_sharing_pb::ReadGroupsResult, absl::Status>&
          result);
  void OnCreateGroupCompleted(
      base::OnceCallback<void(const GroupDataOrFailureOutcome&)> callback,
      const base::expected<data_sharing_pb::CreateGroupResult, absl::Status>&
          result);
  void OnGaiaIdLookupForAddMemberCompleted(
      const GroupId& group_id,
      base::OnceCallback<void(PeopleGroupActionOutcome)> callback,
      const base::expected<data_sharing_pb::LookupGaiaIdByEmailResult,
                           absl::Status>& result);
  void OnGaiaIdLookupForRemoveMemberCompleted(
      const GroupId& group_id,
      base::OnceCallback<void(PeopleGroupActionOutcome)> callback,
      const base::expected<data_sharing_pb::LookupGaiaIdByEmailResult,
                           absl::Status>& result);

  // Converts absl::Status to PeopleGroupActionOutcome and passes it to
  // `callback`, used by DeleteGroup(), InviteMember(), and RemoveMember()
  // flows.
  void OnSimpleGroupActionCompleted(
      base::OnceCallback<void(PeopleGroupActionOutcome)> callback,
      const absl::Status& result);
  void OnAccessTokenAdded(
      base::OnceCallback<void(const GroupDataOrFailureOutcome&)> callback,
      const base::expected<data_sharing_pb::AddAccessTokenResult, absl::Status>&
          result);

  // Called when the SDK delegate has been updated, allowing the group data
  // model to be updated too.
  void OnSDKDelegateUpdated();

  THREAD_CHECKER(thread_checker_);

  // It must be destroyed after the `sdk_delegate_` member because
  // `sdk_delegate` needs the `data_sharing_network_loader_`.
  std::unique_ptr<DataSharingNetworkLoader> data_sharing_network_loader_;
  std::unique_ptr<CollaborationGroupSyncBridge>
      collaboration_group_sync_bridge_;
  // Nullable.
  std::unique_ptr<DataSharingSDKDelegate> sdk_delegate_;
  std::unique_ptr<DataSharingUIDelegate> ui_delegate_;
  // Nullable when `sdk_delegate_` is null.
  std::unique_ptr<GroupDataModel> group_data_model_;

  base::FilePath profile_dir_;

  base::ObserverList<DataSharingService::Observer> observers_;
  std::unique_ptr<PreviewServerProxy> preview_server_proxy_;
  std::unique_ptr<AvatarFetcher> avatar_fetcher_;
  std::unique_ptr<Logger> logger_;

  // An in-memory map of groups that have been removed this session. This is
  // required to be able to inform users about which groups they have been
  // removed from.
  std::unordered_map<GroupId, GroupData> deleted_groups_this_session_;

  // Stores arbitrary GroupData used for testing.
  std::unordered_map<GroupId, GroupData> group_data_for_testing_;

  // The set of groups that the user has attempted to leave in the current
  // session. Not cleared until a chrome restart.
  std::set<GroupId>
      groups_attempted_to_leave_or_delete_by_current_user_in_current_session_;

  base::WeakPtrFactory<DataSharingServiceImpl> weak_ptr_factory_{this};
};

}  // namespace data_sharing

#endif  // COMPONENTS_DATA_SHARING_INTERNAL_DATA_SHARING_SERVICE_IMPL_H_
