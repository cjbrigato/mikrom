// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/test/test_select_url_fenced_frame_config_observer_impl.h"

#include "content/public/browser/storage_partition.h"
#include "content/public/test/shared_storage_test_utils.h"
#include "content/public/test/test_select_url_fenced_frame_config_observer.h"
#include "url/gurl.h"

namespace content {

TestSelectURLFencedFrameConfigObserverImpl::
    TestSelectURLFencedFrameConfigObserverImpl() = default;
TestSelectURLFencedFrameConfigObserverImpl::
    ~TestSelectURLFencedFrameConfigObserverImpl() = default;

GlobalRenderFrameHostId
TestSelectURLFencedFrameConfigObserverImpl::AssociatedFrameHostId() const {
  return GlobalRenderFrameHostId();
}

bool TestSelectURLFencedFrameConfigObserverImpl::
    ShouldReceiveAllSharedStorageReports() const {
  return true;
}

void TestSelectURLFencedFrameConfigObserverImpl::OnSharedStorageAccessed(
    base::Time access_time,
    AccessScope scope,
    AccessMethod method,
    GlobalRenderFrameHostId main_frame_id,
    const std::string& owner_origin,
    const SharedStorageEventParams& params) {}

void TestSelectURLFencedFrameConfigObserverImpl::
    OnSharedStorageSelectUrlUrnUuidGenerated(const GURL& urn_uuid) {
  if (urn_uuid_.has_value()) {
    // This observer has already observed an urn::uuid.
    return;
  }
  urn_uuid_ = urn_uuid;
}

void TestSelectURLFencedFrameConfigObserverImpl::
    OnSharedStorageSelectUrlConfigPopulated(
        const std::optional<FencedFrameConfig>& config) {
  if (config_observed_ || !urn_uuid_.has_value() || !config.has_value() ||
      (urn_uuid_.value() != config->urn_uuid())) {
    // 1. This observer has already observed a config.
    // 2. This observer hasn't observed an urn::uuid yet.
    // 3. The given config is `std::nullopt`.
    // 4. The given config does not correspond to the observed urn::uuid.
    return;
  }
  config_observed_ = true;
  config_ = config;
}

void TestSelectURLFencedFrameConfigObserverImpl::
    OnSharedStorageWorkletOperationExecutionFinished(
        base::Time finished_time,
        base::TimeDelta execution_time,
        AccessMethod method,
        int operation_id,
        const base::UnguessableToken& worklet_devtools_token,
        GlobalRenderFrameHostId main_frame_id,
        const std::string& owner_origin) {}

const std::optional<GURL>&
TestSelectURLFencedFrameConfigObserverImpl::GetUrnUuid() const {
  return urn_uuid_;
}

const std::optional<FencedFrameConfig>&
TestSelectURLFencedFrameConfigObserverImpl::GetConfig() const {
  return config_;
}

bool TestSelectURLFencedFrameConfigObserverImpl::ConfigObserved() const {
  return config_observed_;
}

TestSelectURLFencedFrameConfigObserver::TestSelectURLFencedFrameConfigObserver(
    StoragePartition* storage_partition)
    : storage_partition_(storage_partition),
      impl_(std::make_unique<TestSelectURLFencedFrameConfigObserverImpl>()) {
  SharedStorageRuntimeManager* manager =
      GetSharedStorageRuntimeManagerForStoragePartition(storage_partition_);
  DCHECK(manager);

  manager->AddSharedStorageObserver(impl_.get());
}

TestSelectURLFencedFrameConfigObserver::
    ~TestSelectURLFencedFrameConfigObserver() {
  SharedStorageRuntimeManager* manager =
      GetSharedStorageRuntimeManagerForStoragePartition(storage_partition_);

  manager->RemoveSharedStorageObserver(impl_.get());
}

const std::optional<GURL>& TestSelectURLFencedFrameConfigObserver::GetUrnUuid()
    const {
  return impl_->GetUrnUuid();
}

const std::optional<FencedFrameConfig>&
TestSelectURLFencedFrameConfigObserver::GetConfig() const {
  return impl_->GetConfig();
}

bool TestSelectURLFencedFrameConfigObserver::ConfigObserved() const {
  return impl_->ConfigObserved();
}

}  // namespace content
