// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/data_sharing/internal/data_sharing_service_impl.h"

#include <optional>

#include "base/check_is_test.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/metrics/histogram_functions_internal_overloads.h"
#include "base/notimplemented.h"
#include "base/notreached.h"
#include "base/observer_list.h"
#include "base/task/single_thread_task_runner.h"
#include "base/version_info/channel.h"
#include "components/data_sharing/internal/avatar_fetcher.h"
#include "components/data_sharing/internal/collaboration_group_sync_bridge.h"
#include "components/data_sharing/internal/data_sharing_network_loader_impl.h"
#include "components/data_sharing/internal/group_data_proto_utils.h"
#include "components/data_sharing/internal/logger_impl.h"
#include "components/data_sharing/public/data_sharing_sdk_delegate.h"
#include "components/data_sharing/public/data_sharing_service.h"
#include "components/data_sharing/public/features.h"
#include "components/data_sharing/public/group_data.h"
#include "components/data_sharing/public/protocol/data_sharing_sdk.pb.h"
#include "components/sync/base/data_type.h"
#include "components/sync/base/report_unrecoverable_error.h"
#include "components/sync/model/client_tag_based_data_type_processor.h"
#include "components/sync/model/data_type_store.h"
#include "components/sync/model/data_type_sync_bridge.h"
#include "net/base/url_util.h"
#include "third_party/abseil-cpp/absl/status/status.h"

namespace data_sharing {

namespace {

constexpr char kGroupIdKey[] = "g";
constexpr char kTokenBlobKey[] = "t";
constexpr base::FilePath::CharType kDataSharingDir[] =
    FILE_PATH_LITERAL("DataSharing");

// Should not be called with kOk StatusCode, unless SDK delegate misbehaves by
// passing it as an error value.
DataSharingService::PeopleGroupActionFailure StatusToPeopleGroupActionFailure(
    const absl::Status& status) {
  switch (status.code()) {
    case absl::StatusCode::kOk:
      // Not expected here, treat as a persistent failure.
      return DataSharingService::PeopleGroupActionFailure::kPersistentFailure;
    case absl::StatusCode::kCancelled:
    case absl::StatusCode::kUnknown:
    case absl::StatusCode::kDeadlineExceeded:
    case absl::StatusCode::kResourceExhausted:
    case absl::StatusCode::kAborted:
    case absl::StatusCode::kInternal:
    case absl::StatusCode::kUnavailable:
      return DataSharingService::PeopleGroupActionFailure::kTransientFailure;
    case absl::StatusCode::kNotFound:
    case absl::StatusCode::kAlreadyExists:
    case absl::StatusCode::kPermissionDenied:
    case absl::StatusCode::kFailedPrecondition:
    case absl::StatusCode::kOutOfRange:
    case absl::StatusCode::kUnimplemented:
    case absl::StatusCode::kDataLoss:
    case absl::StatusCode::kUnauthenticated:
      return DataSharingService::PeopleGroupActionFailure::kPersistentFailure;
    default:
      // absl::StatusCode should always have "default:" in `switch()`.
      return DataSharingService::PeopleGroupActionFailure::kPersistentFailure;
  }
}

DataSharingService::PeopleGroupActionOutcome StatusToPeopleGroupActionOutcome(
    const absl::Status& status) {
  if (status.ok()) {
    return DataSharingService::PeopleGroupActionOutcome::kSuccess;
  }
  switch (StatusToPeopleGroupActionFailure(status)) {
    case DataSharingService::PeopleGroupActionFailure::kUnknown:
      return DataSharingService::PeopleGroupActionOutcome::kUnknown;
    case DataSharingService::PeopleGroupActionFailure::kPersistentFailure:
      return DataSharingService::PeopleGroupActionOutcome::kPersistentFailure;
    case DataSharingService::PeopleGroupActionFailure::kTransientFailure:
      return DataSharingService::PeopleGroupActionOutcome::kTransientFailure;
  }
}

}  // namespace

DataSharingServiceImpl::DataSharingServiceImpl(
    const base::FilePath& profile_dir,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    signin::IdentityManager* identity_manager,
    syncer::OnceDataTypeStoreFactory data_type_store_factory,
    version_info::Channel channel,
    std::unique_ptr<DataSharingSDKDelegate> sdk_delegate,
    std::unique_ptr<DataSharingUIDelegate> ui_delegate)
    : data_sharing_network_loader_(
          std::make_unique<DataSharingNetworkLoaderImpl>(url_loader_factory,
                                                         identity_manager)),
      sdk_delegate_(std::move(sdk_delegate)),
      ui_delegate_(std::move(ui_delegate)),
      profile_dir_(profile_dir),
      preview_server_proxy_(
          std::make_unique<PreviewServerProxy>(identity_manager,
                                               url_loader_factory,
                                               channel)),
      avatar_fetcher_(std::make_unique<AvatarFetcher>()),
      logger_(std::make_unique<LoggerImpl>()) {
  auto change_processor =
      std::make_unique<syncer::ClientTagBasedDataTypeProcessor>(
          syncer::COLLABORATION_GROUP,
          base::BindRepeating(&syncer::ReportUnrecoverableError, channel));
  collaboration_group_sync_bridge_ =
      std::make_unique<CollaborationGroupSyncBridge>(
          std::move(change_processor), std::move(data_type_store_factory));

  OnSDKDelegateUpdated();
}

DataSharingServiceImpl::~DataSharingServiceImpl() {
  ClearAllUserData();
  if (group_data_model_) {
    group_data_model_->RemoveObserver(this);
  }
}

bool DataSharingServiceImpl::IsEmptyService() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return false;
}

void DataSharingServiceImpl::AddObserver(
    DataSharingService::Observer* observer) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  observers_.AddObserver(observer);
}

void DataSharingServiceImpl::RemoveObserver(
    DataSharingService::Observer* observer) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  observers_.RemoveObserver(observer);
}

DataSharingNetworkLoader*
DataSharingServiceImpl::GetDataSharingNetworkLoader() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return data_sharing_network_loader_.get();
}

base::WeakPtr<syncer::DataTypeControllerDelegate>
DataSharingServiceImpl::GetCollaborationGroupControllerDelegate() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return collaboration_group_sync_bridge_->change_processor()
      ->GetControllerDelegate();
}

bool DataSharingServiceImpl::IsGroupDataModelLoaded() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return group_data_model_ && group_data_model_->IsModelLoaded();
}

std::optional<GroupData> DataSharingServiceImpl::ReadGroup(
    const GroupId& group_id) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (group_data_for_testing_.contains(group_id)) {
    CHECK_IS_TEST();
    return group_data_for_testing_[group_id];
  }

  if (!group_data_model_) {
    return std::nullopt;
  }
  return group_data_model_->GetGroup(group_id);
}

std::set<GroupData> DataSharingServiceImpl::ReadAllGroups() {
  if (!group_data_model_) {
    return std::set<GroupData>();
  }
  return group_data_model_->GetAllGroups();
}

std::optional<GroupMemberPartialData>
DataSharingServiceImpl::GetPossiblyRemovedGroupMember(
    const GroupId& group_id,
    const GaiaId& member_gaia_id) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (group_data_for_testing_.contains(group_id)) {
    CHECK_IS_TEST();
    const auto& group = group_data_for_testing_[group_id];
    for (const auto& member : group.members) {
      if (member.gaia_id == member_gaia_id) {
        return GroupMemberPartialData::FromGroupMember(member);
      }
    }
  }

  if (!group_data_model_) {
    return std::nullopt;
  }
  return group_data_model_->GetPossiblyRemovedGroupMember(group_id,
                                                          member_gaia_id);
}

std::optional<GroupData> DataSharingServiceImpl::GetPossiblyRemovedGroup(
    const GroupId& group_id) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (deleted_groups_this_session_.find(group_id) ==
      deleted_groups_this_session_.end()) {
    return std::nullopt;
  }
  return deleted_groups_this_session_.at(group_id);
}

void DataSharingServiceImpl::ReadGroupDeprecated(
    const GroupId& group_id,
    base::OnceCallback<void(const GroupDataOrFailureOutcome&)> callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  // TODO(crbug.com/382036119): this method should be deleted.
  if (!sdk_delegate_) {
    // Reply in a posted task to avoid reentrance on the calling side.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            std::move(callback),
            base::unexpected(PeopleGroupActionFailure::kPersistentFailure)));
    return;
  }

  data_sharing_pb::ReadGroupsParams params;
  data_sharing_pb::ReadGroupsParams::GroupParams* group_params =
      params.add_group_params();
  group_params->set_group_id(group_id.value());
  group_params->set_consistency_token("");

  sdk_delegate_->ReadGroups(
      params,
      base::BindOnce(&DataSharingServiceImpl::OnReadSingleGroupCompleted,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void DataSharingServiceImpl::ReadNewGroup(
    const GroupToken& token,
    base::OnceCallback<void(const GroupDataOrFailureOutcome&)> callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!sdk_delegate_) {
    // Reply in a posted task to avoid reentrance on the calling side.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            std::move(callback),
            base::unexpected(PeopleGroupActionFailure::kPersistentFailure)));
    return;
  }

  data_sharing_pb::ReadGroupWithTokenParams params;
  const std::string& group_id = token.group_id.value();
  params.set_group_id(group_id);
  params.set_access_token(token.access_token);
  sdk_delegate_->ReadGroupWithToken(
      params,
      base::BindOnce(&DataSharingServiceImpl::OnReadSingleGroupCompleted,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void DataSharingServiceImpl::CreateGroup(
    const std::string& group_name,
    base::OnceCallback<void(const GroupDataOrFailureOutcome&)> callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!sdk_delegate_) {
    // Reply in a posted task to avoid reentrance on the calling side.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            std::move(callback),
            base::unexpected(PeopleGroupActionFailure::kPersistentFailure)));
    return;
  }

  data_sharing_pb::CreateGroupParams params;
  params.set_display_name(group_name);
  sdk_delegate_->CreateGroup(
      params,
      base::BindOnce(&DataSharingServiceImpl::OnCreateGroupCompleted,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void DataSharingServiceImpl::DeleteGroup(
    const GroupId& group_id,
    base::OnceCallback<void(PeopleGroupActionOutcome)> callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!sdk_delegate_) {
    // Reply in a posted task to avoid reentrance on the calling side.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       PeopleGroupActionOutcome::kPersistentFailure));
    return;
  }

  groups_attempted_to_leave_or_delete_by_current_user_in_current_session_
      .insert(group_id);

  data_sharing_pb::DeleteGroupParams params;
  params.set_group_id(group_id.value());
  sdk_delegate_->DeleteGroup(
      params,
      base::BindOnce(&DataSharingServiceImpl::OnSimpleGroupActionCompleted,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void DataSharingServiceImpl::InviteMember(
    const GroupId& group_id,
    const std::string& invitee_email,
    base::OnceCallback<void(PeopleGroupActionOutcome)> callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!sdk_delegate_) {
    // Reply in a posted task to avoid reentrance on the calling side.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       PeopleGroupActionOutcome::kPersistentFailure));
    return;
  }

  data_sharing_pb::LookupGaiaIdByEmailParams lookup_params;
  lookup_params.set_email(invitee_email);
  sdk_delegate_->LookupGaiaIdByEmail(
      lookup_params,
      base::BindOnce(
          &DataSharingServiceImpl::OnGaiaIdLookupForAddMemberCompleted,
          weak_ptr_factory_.GetWeakPtr(), group_id, std::move(callback)));
}

void DataSharingServiceImpl::AddMember(
    const GroupId& group_id,
    const std::string& access_token,
    base::OnceCallback<void(PeopleGroupActionOutcome)> callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!sdk_delegate_) {
    // Reply in a posted task to avoid reentrance on the calling side.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       PeopleGroupActionOutcome::kPersistentFailure));
    return;
  }

  data_sharing_pb::AddMemberParams params;
  params.set_group_id(group_id.value());
  params.set_access_token(access_token);
  sdk_delegate_->AddMember(
      params,
      base::BindOnce(&DataSharingServiceImpl::OnSimpleGroupActionCompleted,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void DataSharingServiceImpl::RemoveMember(
    const GroupId& group_id,
    const std::string& member_email,
    base::OnceCallback<void(PeopleGroupActionOutcome)> callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!sdk_delegate_) {
    // Reply in a posted task to avoid reentrance on the calling side.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       PeopleGroupActionOutcome::kPersistentFailure));
    return;
  }

  data_sharing_pb::LookupGaiaIdByEmailParams lookup_params;
  lookup_params.set_email(member_email);
  sdk_delegate_->LookupGaiaIdByEmail(
      lookup_params,
      base::BindOnce(
          &DataSharingServiceImpl::OnGaiaIdLookupForRemoveMemberCompleted,
          weak_ptr_factory_.GetWeakPtr(), group_id, std::move(callback)));
}

void DataSharingServiceImpl::LeaveGroup(
    const GroupId& group_id,
    base::OnceCallback<void(PeopleGroupActionOutcome)> callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!sdk_delegate_) {
    // Reply in a posted task to avoid reentrance on the calling side.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       PeopleGroupActionOutcome::kPersistentFailure));
    return;
  }

  groups_attempted_to_leave_or_delete_by_current_user_in_current_session_
      .insert(group_id);

  data_sharing_pb::LeaveGroupParams params;
  params.set_group_id(group_id.value());
  sdk_delegate_->LeaveGroup(
      params,
      base::BindOnce(&DataSharingServiceImpl::OnSimpleGroupActionCompleted,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

bool DataSharingServiceImpl::IsLeavingOrDeletingGroup(const GroupId& group_id) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return groups_attempted_to_leave_or_delete_by_current_user_in_current_session_
      .contains(group_id);
}

std::vector<GroupEvent> DataSharingServiceImpl::GetGroupEventsSinceStartup() {
  if (!group_data_model_) {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
    return std::vector<GroupEvent>();
  }
  return group_data_model_->GetGroupEventsSinceStartup();
}

void DataSharingServiceImpl::OnModelLoaded() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  std::set<GroupData> groups = ReadAllGroups();
  for (const GroupData& group : groups) {
    base::UmaHistogramCounts100("DataSharing.TotalMembersInGroup.AtStartup",
                                group.members.size());
  }

  for (auto& observer : observers_) {
    observer.OnGroupDataModelLoaded();
  }
}

void DataSharingServiceImpl::OnGroupAdded(const GroupId& group_id,
                                          const base::Time& event_time) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  CHECK(group_data_model_);
  std::optional<GroupData> group_data = group_data_model_->GetGroup(group_id);
  CHECK(group_data);
  for (auto& observer : observers_) {
    observer.OnGroupAdded(*group_data, event_time);
  }
}

void DataSharingServiceImpl::OnGroupUpdated(const GroupId& group_id,
                                            const base::Time& event_time) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  CHECK(group_data_model_);

  std::optional<GroupData> group_data = group_data_model_->GetGroup(group_id);
  CHECK(group_data);
  for (auto& observer : observers_) {
    observer.OnGroupChanged(*group_data, event_time);
  }
}

void DataSharingServiceImpl::OnGroupDeleted(
    const GroupId& group_id,
    const std::optional<GroupData>& group_data,
    const base::Time& event_time) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (group_data) {
    CHECK(group_id == group_data->group_token.group_id);
    deleted_groups_this_session_.emplace(group_id, *group_data);
  }
  for (auto& observer : observers_) {
    observer.OnGroupRemoved(group_id, event_time);
  }
}

void DataSharingServiceImpl::OnMemberAdded(const GroupId& group_id,
                                           const GaiaId& member_gaia_id,
                                           const base::Time& event_time) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  for (auto& observer : observers_) {
    observer.OnGroupMemberAdded(group_id, member_gaia_id, event_time);
  }
}

void DataSharingServiceImpl::OnMemberRemoved(const GroupId& group_id,
                                             const GaiaId& member_gaia_id,
                                             const base::Time& event_time) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  for (auto& observer : observers_) {
    observer.OnGroupMemberRemoved(group_id, member_gaia_id, event_time);
  }
}

void DataSharingServiceImpl::OnSyncBridgeUpdateTypeChanged(
    SyncBridgeUpdateType sync_bridge_update_type) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  for (auto& observer : observers_) {
    observer.OnSyncBridgeUpdateTypeChanged(sync_bridge_update_type);
  }
}

void DataSharingServiceImpl::Shutdown() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (sdk_delegate_) {
    sdk_delegate_->Shutdown();
  }
}

// static
std::unique_ptr<GURL> DataSharingServiceImpl::GetDataSharingUrl(
    const GroupToken& group_token) {
  GURL url = GURL(data_sharing::features::kDataSharingURL.Get());

  url =
      net::AppendQueryParameter(url, kGroupIdKey, group_token.group_id.value());
  url = net::AppendQueryParameter(url, kTokenBlobKey, group_token.access_token);
  return std::make_unique<GURL>(url);
}

void DataSharingServiceImpl::OnReadSingleGroupCompleted(
    base::OnceCallback<void(const GroupDataOrFailureOutcome&)> callback,
    const base::expected<data_sharing_pb::ReadGroupsResult, absl::Status>&
        result) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (result.has_value()) {
    if (result.value().group_data_size() == 1) {
      std::move(callback).Run(GroupDataFromProto(result.value().group_data(0)));
      return;
    } else {
      // SDK indicated success, but didn't return exactly single group,
      // indicating serious bug in SDK.
      std::move(callback).Run(
          base::unexpected(PeopleGroupActionFailure::kPersistentFailure));
      return;
    }
  }

  std::move(callback).Run(
      base::unexpected(StatusToPeopleGroupActionFailure(result.error())));
}

void DataSharingServiceImpl::OnCreateGroupCompleted(
    base::OnceCallback<void(const GroupDataOrFailureOutcome&)> callback,
    const base::expected<data_sharing_pb::CreateGroupResult, absl::Status>&
        result) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (result.has_value()) {
    std::move(callback).Run(GroupDataFromProto(result.value().group_data()));
    return;
  }

  std::move(callback).Run(
      base::unexpected(StatusToPeopleGroupActionFailure(result.error())));
}

void DataSharingServiceImpl::OnGaiaIdLookupForAddMemberCompleted(
    const GroupId& group_id,
    base::OnceCallback<void(PeopleGroupActionOutcome)> callback,
    const base::expected<data_sharing_pb::LookupGaiaIdByEmailResult,
                         absl::Status>& result) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!result.has_value()) {
    std::move(callback).Run(StatusToPeopleGroupActionOutcome(result.error()));
    return;
  }

  data_sharing_pb::AddMemberParams params;
  params.set_group_id(group_id.value());
  params.set_member_gaia_id(result.value().gaia_id());
  sdk_delegate_->AddMember(
      params,
      base::BindOnce(&DataSharingServiceImpl::OnSimpleGroupActionCompleted,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void DataSharingServiceImpl::OnGaiaIdLookupForRemoveMemberCompleted(
    const GroupId& group_id,
    base::OnceCallback<void(PeopleGroupActionOutcome)> callback,
    const base::expected<data_sharing_pb::LookupGaiaIdByEmailResult,
                         absl::Status>& result) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!result.has_value()) {
    std::move(callback).Run(StatusToPeopleGroupActionOutcome(result.error()));
    return;
  }

  data_sharing_pb::RemoveMemberParams params;
  params.set_group_id(group_id.value());
  params.set_member_gaia_id(result.value().gaia_id());
  sdk_delegate_->RemoveMember(
      params,
      base::BindOnce(&DataSharingServiceImpl::OnSimpleGroupActionCompleted,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void DataSharingServiceImpl::OnSimpleGroupActionCompleted(
    base::OnceCallback<void(PeopleGroupActionOutcome)> callback,
    const absl::Status& status) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  std::move(callback).Run(StatusToPeopleGroupActionOutcome(status));
}

CollaborationGroupSyncBridge*
DataSharingServiceImpl::GetCollaborationGroupSyncBridgeForTesting() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return collaboration_group_sync_bridge_.get();
}

void DataSharingServiceImpl::HandleShareURLNavigationIntercepted(
    const GURL& url,
    std::unique_ptr<ShareURLInterceptionContext> context) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!ui_delegate_) {
    return;
  }
  ui_delegate_->HandleShareURLIntercepted(url, std::move(context));
}

std::unique_ptr<GURL> DataSharingServiceImpl::GetDataSharingUrl(
    const GroupData& group_data) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!group_data.group_token.IsValid()) {
    return nullptr;
  }
  return GetDataSharingUrl(group_data.group_token);
}

void DataSharingServiceImpl::EnsureGroupVisibility(
    const GroupId& group_id,
    base::OnceCallback<void(const GroupDataOrFailureOutcome&)> callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!sdk_delegate_) {
    // Reply in a posted task to avoid reentrance on the calling side.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            std::move(callback),
            base::unexpected(PeopleGroupActionFailure::kPersistentFailure)));
    return;
  }

  // TODO(ritikagup@): If a token was added recently then skip adding and return
  // read group.
  data_sharing_pb::AddAccessTokenParams params;
  params.set_group_id(group_id.value());
  sdk_delegate_->AddAccessToken(
      params,
      base::BindOnce(&DataSharingServiceImpl::OnAccessTokenAdded,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void DataSharingServiceImpl::GetSharedEntitiesPreview(
    const GroupToken& group_token,
    base::OnceCallback<void(const SharedDataPreviewOrFailureOutcome&)>
        callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  preview_server_proxy_->GetSharedDataPreview(
      group_token, syncer::DataType::SHARED_TAB_GROUP_DATA,
      std::move(callback));
}

void DataSharingServiceImpl::GetAvatarImageForURL(
    const GURL& avatar_url,
    int size,
    base::OnceCallback<void(const gfx::Image&)> callback,
    image_fetcher::ImageFetcher* image_fetcher) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  avatar_fetcher_->Fetch(avatar_url, size, std::move(callback), image_fetcher);
}

void DataSharingServiceImpl::SetSDKDelegate(
    std::unique_ptr<DataSharingSDKDelegate> sdk_delegate) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  CHECK(!sdk_delegate || (sdk_delegate && !sdk_delegate_));

  sdk_delegate_ = std::move(sdk_delegate);

  OnSDKDelegateUpdated();
}

void DataSharingServiceImpl::SetUIDelegate(
    std::unique_ptr<DataSharingUIDelegate> ui_delegate) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  ui_delegate_ = std::move(ui_delegate);
}

DataSharingUIDelegate* DataSharingServiceImpl::GetUiDelegate() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (sdk_delegate_) {
    sdk_delegate_->ForceInitialize(data_sharing_network_loader_.get());
  }
  return ui_delegate_.get();
}

Logger* DataSharingServiceImpl::GetLogger() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return logger_.get();
}

void DataSharingServiceImpl::AddGroupDataForTesting(GroupData group_data) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  group_data_for_testing_.emplace(group_data.group_token.group_id, group_data);
}

void DataSharingServiceImpl::SetPreviewServerProxyForTesting(
    std::unique_ptr<PreviewServerProxy> preview_server_proxy) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  preview_server_proxy_ = std::move(preview_server_proxy);
}

PreviewServerProxy* DataSharingServiceImpl::GetPreviewServerProxyForTesting() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return preview_server_proxy_.get();
}

void DataSharingServiceImpl::OnCollaborationGroupRemoved(
    const data_sharing::GroupId& group_id) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (collaboration_group_sync_bridge_) {
    collaboration_group_sync_bridge_->RemoveGroupLocally(group_id);
  }
}

void DataSharingServiceImpl::OnAccessTokenAdded(
    base::OnceCallback<void(const GroupDataOrFailureOutcome&)> callback,
    const base::expected<data_sharing_pb::AddAccessTokenResult, absl::Status>&
        result) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (result.has_value()) {
    std::move(callback).Run(GroupDataFromProto(result.value().group_data()));
    return;
  }

  std::move(callback).Run(
      base::unexpected(StatusToPeopleGroupActionFailure(result.error())));
}

void DataSharingServiceImpl::OnSDKDelegateUpdated() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (group_data_model_) {
    group_data_model_->RemoveObserver(this);
    group_data_model_.reset();
  }

  if (sdk_delegate_) {
    sdk_delegate_->Initialize(data_sharing_network_loader_.get());

    const base::FilePath data_sharing_dir =
        profile_dir_.Append(kDataSharingDir);
    group_data_model_ = std::make_unique<GroupDataModel>(
        data_sharing_dir, collaboration_group_sync_bridge_.get(),
        sdk_delegate_.get());
    group_data_model_->AddObserver(this);
  }
}

}  // namespace data_sharing
