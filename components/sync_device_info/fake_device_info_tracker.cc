// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync_device_info/fake_device_info_tracker.h"

#include <algorithm>
#include <map>

#include "base/check.h"
#include "base/memory/raw_ptr.h"
#include "base/notreached.h"
#include "components/sync/protocol/sync_enums.pb.h"
#include "components/sync_device_info/device_info.h"

namespace syncer {

FakeDeviceInfoTracker::FakeDeviceInfoTracker() = default;

FakeDeviceInfoTracker::~FakeDeviceInfoTracker() = default;

bool FakeDeviceInfoTracker::IsSyncing() const {
  return !devices_.empty();
}

const DeviceInfo* FakeDeviceInfoTracker::GetDeviceInfo(
    const std::string& client_id) const {
  for (const DeviceInfo* device : devices_) {
    if (device->guid() == client_id) {
      return device;
    }
  }
  return nullptr;
}

std::vector<const DeviceInfo*> FakeDeviceInfoTracker::GetAllDeviceInfo() const {
  std::vector<const DeviceInfo*> devices;
  for (const DeviceInfo* device : devices_) {
    devices.push_back(device);
  }
  return devices;
}

std::vector<const DeviceInfo*> FakeDeviceInfoTracker::GetAllChromeDeviceInfo()
    const {
  return GetAllDeviceInfo();
}

void FakeDeviceInfoTracker::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void FakeDeviceInfoTracker::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

std::map<DeviceInfo::FormFactor, int>
FakeDeviceInfoTracker::CountActiveDevicesByType() const {
  if (device_count_per_type_override_) {
    return *device_count_per_type_override_;
  }

  std::map<DeviceInfo::FormFactor, int> count_by_type;
  for (const syncer::DeviceInfo* device : devices_) {
    count_by_type[device->form_factor()]++;
  }
  return count_by_type;
}

void FakeDeviceInfoTracker::ForcePulseForTest() {
  NOTREACHED();
}

bool FakeDeviceInfoTracker::IsRecentLocalCacheGuid(
    const std::string& cache_guid) const {
  return local_device_cache_guid_ == cache_guid;
}

void FakeDeviceInfoTracker::Add(const DeviceInfo* device) {
  devices_.push_back(device);
  for (auto& observer : observers_) {
    observer.OnDeviceInfoChange();
  }
}

void FakeDeviceInfoTracker::Add(const std::vector<const DeviceInfo*>& devices) {
  for (auto* device : devices) {
    devices_.push_back(device);
  }
  for (auto& observer : observers_) {
    observer.OnDeviceInfoChange();
  }
}

void FakeDeviceInfoTracker::Add(std::unique_ptr<DeviceInfo> device) {
  owned_devices_.push_back(std::move(device));
  Add(owned_devices_.back().get());
}

void FakeDeviceInfoTracker::Remove(const DeviceInfo* device) {
  const auto to_remove = std::ranges::remove(devices_, device);
  CHECK(!to_remove.empty());
  devices_.erase(to_remove.begin(), to_remove.end());
}

void FakeDeviceInfoTracker::Replace(const DeviceInfo* old_device,
                                    const DeviceInfo* new_device) {
  auto it = std::ranges::find(devices_, old_device);
  CHECK(devices_.end() != it) << "Tracker doesn't contain device";
  *it = new_device;
  for (auto& observer : observers_) {
    observer.OnDeviceInfoChange();
  }
}

void FakeDeviceInfoTracker::OverrideActiveDeviceCount(
    const std::map<DeviceInfo::FormFactor, int>& counts) {
  device_count_per_type_override_ = counts;
  for (auto& observer : observers_) {
    observer.OnDeviceInfoChange();
  }
}

void FakeDeviceInfoTracker::SetLocalCacheGuid(const std::string& cache_guid) {
  // ensure that this cache guid is present in the tracker.
  DCHECK(GetDeviceInfo(cache_guid));
  local_device_cache_guid_ = cache_guid;
}

}  // namespace syncer
