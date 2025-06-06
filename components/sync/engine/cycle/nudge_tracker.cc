// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/engine/cycle/nudge_tracker.h"

#include <algorithm>
#include <utility>

#include "components/sync/protocol/data_type_progress_marker.pb.h"
#include "components/sync/protocol/sync_enums.pb.h"

namespace syncer {

namespace {

// Nudge delay for local refresh. Common to all data types.
constexpr base::TimeDelta kLocalRefreshDelay = base::Milliseconds(500);

}  // namespace

NudgeTracker::NudgeTracker() {
  for (DataType type : DataTypeSet::All()) {
    type_trackers_[type] = std::make_unique<DataTypeTracker>(type);
  }
}

NudgeTracker::~NudgeTracker() = default;

bool NudgeTracker::IsSyncRequired(DataTypeSet types) const {
  for (DataType type : types) {
    TypeTrackerMap::const_iterator tracker_it = type_trackers_.find(type);
    CHECK(tracker_it != type_trackers_.end()) << DataTypeToDebugString(type);
    if (tracker_it->second->IsSyncRequired()) {
      return true;
    }
  }

  return false;
}

bool NudgeTracker::IsGetUpdatesRequired(DataTypeSet types) const {
  if (invalidations_out_of_sync_) {
    return true;
  }

  for (DataType type : types) {
    TypeTrackerMap::const_iterator tracker_it = type_trackers_.find(type);
    CHECK(tracker_it != type_trackers_.end()) << DataTypeToDebugString(type);
    if (tracker_it->second->IsGetUpdatesRequired()) {
      return true;
    }
  }
  return false;
}

void NudgeTracker::RecordSuccessfulCommitMessage(DataTypeSet types) {
  for (DataType type : types) {
    TypeTrackerMap::const_iterator tracker_it = type_trackers_.find(type);
    CHECK(tracker_it != type_trackers_.end()) << DataTypeToDebugString(type);
    tracker_it->second->RecordSuccessfulCommitMessage();
  }
}

void NudgeTracker::RecordSuccessfulSyncCycleIfNotBlocked(DataTypeSet types) {
  // A successful cycle while invalidations are enabled puts us back into sync.
  invalidations_out_of_sync_ = !invalidations_enabled_;

  for (DataType type : types) {
    TypeTrackerMap::const_iterator tracker_it = type_trackers_.find(type);
    CHECK(tracker_it != type_trackers_.end()) << DataTypeToDebugString(type);
    tracker_it->second->RecordSuccessfulSyncCycleIfNotBlocked();
  }
}

void NudgeTracker::RecordInitialSyncDone(DataTypeSet types) {
  for (DataType type : types) {
    TypeTrackerMap::const_iterator tracker_it = type_trackers_.find(type);
    CHECK(tracker_it != type_trackers_.end()) << DataTypeToDebugString(type);
    tracker_it->second->RecordInitialSyncDone();
  }
}

base::TimeDelta NudgeTracker::RecordLocalChange(DataType type,
                                                bool is_single_client) {
  DCHECK(type_trackers_.contains(type));
  type_trackers_[type]->RecordLocalChange();
  return type_trackers_[type]->GetLocalChangeNudgeDelay(is_single_client);
}

base::TimeDelta NudgeTracker::RecordLocalRefreshRequest(DataTypeSet types) {
  for (DataType type : types) {
    TypeTrackerMap::const_iterator tracker_it = type_trackers_.find(type);
    CHECK(tracker_it != type_trackers_.end()) << DataTypeToDebugString(type);
    tracker_it->second->RecordLocalRefreshRequest();
  }
  return kLocalRefreshDelay;
}

base::TimeDelta NudgeTracker::GetRemoteInvalidationDelay(DataType type) const {
  TypeTrackerMap::const_iterator tracker_it = type_trackers_.find(type);
  CHECK(tracker_it != type_trackers_.end());
  return tracker_it->second->GetRemoteInvalidationDelay();
}

void NudgeTracker::RecordInitialSyncRequired(DataType type) {
  TypeTrackerMap::const_iterator tracker_it = type_trackers_.find(type);
  CHECK(tracker_it != type_trackers_.end());
  tracker_it->second->RecordInitialSyncRequired();
}

void NudgeTracker::RecordCommitConflict(DataType type) {
  TypeTrackerMap::const_iterator tracker_it = type_trackers_.find(type);
  CHECK(tracker_it != type_trackers_.end());
  tracker_it->second->RecordCommitConflict();
}

void NudgeTracker::OnInvalidationsEnabled() {
  invalidations_enabled_ = true;
}

void NudgeTracker::OnInvalidationsDisabled() {
  invalidations_enabled_ = false;
  invalidations_out_of_sync_ = true;
}

void NudgeTracker::SetTypesThrottledUntil(DataTypeSet types,
                                          base::TimeDelta length,
                                          base::TimeTicks now) {
  for (DataType type : types) {
    TypeTrackerMap::const_iterator tracker_it = type_trackers_.find(type);
    tracker_it->second->ThrottleType(length, now);
  }
}

void NudgeTracker::SetTypeBackedOff(DataType type,
                                    base::TimeDelta length,
                                    base::TimeTicks now) {
  TypeTrackerMap::const_iterator tracker_it = type_trackers_.find(type);
  CHECK(tracker_it != type_trackers_.end());
  tracker_it->second->BackOffType(length, now);
}

void NudgeTracker::UpdateTypeThrottlingAndBackoffState() {
  for (const auto& [type, tracker] : type_trackers_) {
    tracker->UpdateThrottleOrBackoffState();
  }
}

void NudgeTracker::SetHasPendingInvalidations(DataType type,
                                              bool has_invalidation) {
  TypeTrackerMap::const_iterator tracker_it = type_trackers_.find(type);
  CHECK(tracker_it != type_trackers_.end());
  tracker_it->second->SetHasPendingInvalidations(has_invalidation);
}

bool NudgeTracker::IsAnyTypeBlocked() const {
  for (const auto& [type, tracker] : type_trackers_) {
    if (tracker->IsBlocked()) {
      return true;
    }
  }
  return false;
}

bool NudgeTracker::IsTypeBlocked(DataType type) const {
  DCHECK(type_trackers_.find(type) != type_trackers_.end())
      << DataTypeToDebugString(type);
  return type_trackers_.find(type)->second->IsBlocked();
}

WaitInterval::BlockingMode NudgeTracker::GetTypeBlockingMode(
    DataType type) const {
  DCHECK(type_trackers_.find(type) != type_trackers_.end());
  return type_trackers_.find(type)->second->GetBlockingMode();
}

base::TimeDelta NudgeTracker::GetTimeUntilNextUnblock() const {
  DCHECK(IsAnyTypeBlocked()) << "This function requires a pending unblock.";

  // Return min of GetTimeUntilUnblock() values for all IsBlocked() types.
  base::TimeDelta time_until_next_unblock = base::TimeDelta::Max();
  for (const auto& [type, tracker] : type_trackers_) {
    if (tracker->IsBlocked()) {
      time_until_next_unblock =
          std::min(time_until_next_unblock, tracker->GetTimeUntilUnblock());
    }
  }
  DCHECK(!time_until_next_unblock.is_max());

  return time_until_next_unblock;
}

base::TimeDelta NudgeTracker::GetTypeLastBackoffInterval(DataType type) const {
  auto tracker_it = type_trackers_.find(type);
  CHECK(tracker_it != type_trackers_.end());

  return tracker_it->second->GetLastBackoffInterval();
}

DataTypeSet NudgeTracker::GetBlockedTypes() const {
  DataTypeSet result;
  for (const auto& [type, tracker] : type_trackers_) {
    if (tracker->IsBlocked()) {
      result.Put(type);
    }
  }
  return result;
}

DataTypeSet NudgeTracker::GetNudgedTypes() const {
  DataTypeSet result;
  for (const auto& [type, tracker] : type_trackers_) {
    if (tracker->HasLocalChangePending()) {
      result.Put(type);
    }
  }
  return result;
}

DataTypeSet NudgeTracker::GetNotifiedTypes() const {
  DataTypeSet result;
  for (const auto& [type, tracker] : type_trackers_) {
    if (tracker->HasPendingInvalidation()) {
      result.Put(type);
    }
  }
  return result;
}

DataTypeSet NudgeTracker::GetRefreshRequestedTypes() const {
  DataTypeSet result;
  for (const auto& [type, tracker] : type_trackers_) {
    if (tracker->HasRefreshRequestPending()) {
      result.Put(type);
    }
  }
  return result;
}

sync_pb::SyncEnums::GetUpdatesOrigin NudgeTracker::GetOrigin() const {
  // TODO(crbug.com/40252048): This appears trivial after removing GU_RETRY, either
  // simplify the code around or add an explanation why this is needed.
  for (const auto& [type, tracker] : type_trackers_) {
    if (!tracker->IsBlocked() && (tracker->HasPendingInvalidation() ||
                                  tracker->HasRefreshRequestPending() ||
                                  tracker->HasLocalChangePending() ||
                                  tracker->IsInitialSyncRequired())) {
      return sync_pb::SyncEnums::GU_TRIGGER;
    }
  }

  return sync_pb::SyncEnums::UNKNOWN_ORIGIN;
}

void NudgeTracker::FillProtoMessage(DataType type,
                                    sync_pb::GetUpdateTriggers* msg) const {
  DCHECK(type_trackers_.find(type) != type_trackers_.end());

  // Fill what we can from the global data.
  msg->set_invalidations_out_of_sync(invalidations_out_of_sync_);

  // Delegate the type-specific work to the DataTypeTracker class.
  type_trackers_.find(type)->second->FillGetUpdatesTriggersMessage(msg);
}

void NudgeTracker::UpdateLocalChangeDelay(DataType type,
                                          const base::TimeDelta& delay) {
  if (type_trackers_.contains(type)) {
    type_trackers_[type]->UpdateLocalChangeNudgeDelay(delay);
  }
}

void NudgeTracker::SetLocalChangeDelayIgnoringMinForTest(
    DataType type,
    const base::TimeDelta& delay) {
  DCHECK(type_trackers_.contains(type));
  type_trackers_[type]->SetLocalChangeNudgeDelayIgnoringMinForTest(delay);
}

void NudgeTracker::SetQuotaParamsForExtensionTypes(
    std::optional<int> max_tokens,
    std::optional<base::TimeDelta> refill_interval,
    std::optional<base::TimeDelta> depleted_quota_nudge_delay) {
  for (const auto& [type, tracker] : type_trackers_) {
    tracker->SetQuotaParamsIfExtensionType(max_tokens, refill_interval,
                                           depleted_quota_nudge_delay);
  }
}

}  // namespace syncer
