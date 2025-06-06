// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/connectors/device_trust/signals/decorators/browser/browser_signals_decorator.h"

#include <functional>
#include <utility>

#include "base/barrier_closure.h"
#include "base/check.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/enterprise/connectors/device_trust/device_trust_features.h"
#include "chrome/browser/enterprise/connectors/device_trust/signals/decorators/common/metrics_utils.h"
#include "chrome/browser/enterprise/connectors/device_trust/signals/decorators/common/signals_utils.h"
#include "chrome/browser/enterprise/signals/device_info_fetcher.h"
#include "components/device_signals/core/browser/browser_utils.h"
#include "components/device_signals/core/browser/signals_aggregator.h"
#include "components/device_signals/core/browser/signals_types.h"
#include "components/device_signals/core/common/common_types.h"
#include "components/device_signals/core/common/signals_constants.h"
#include "components/enterprise/core/dependency_factory.h"
#include "components/policy/core/common/cloud/cloud_policy_manager.h"
#include "components/policy/core/common/cloud/cloud_policy_store.h"
#include "components/policy/proto/device_management_backend.pb.h"

#if BUILDFLAG(IS_WIN)
#include "components/device_signals/core/common/win/win_types.h"
#endif

namespace enterprise_connectors {

namespace {

constexpr char kLatencyHistogramVariant[] = "Browser";

std::vector<std::string> RemoveDuplicates(std::vector<std::string> addresses) {
  std::sort(addresses.begin(), addresses.end());
  addresses.erase(std::unique(addresses.begin(), addresses.end()),
                  addresses.end());

  return addresses;
}

}  // namespace

BrowserSignalsDecorator::BrowserSignalsDecorator(
    policy::CloudPolicyManager* browser_cloud_policy_manager,
    std::unique_ptr<enterprise_core::DependencyFactory> dependency_factory,
    device_signals::SignalsAggregator* signals_aggregator)
    : browser_cloud_policy_manager_(browser_cloud_policy_manager),
      dependency_factory_(std::move(dependency_factory)),
      signals_aggregator_(signals_aggregator) {
  CHECK(dependency_factory_);
}

BrowserSignalsDecorator::~BrowserSignalsDecorator() = default;

void BrowserSignalsDecorator::Decorate(base::Value::Dict& signals,
                                       base::OnceClosure done_closure) {
  auto start_time = base::TimeTicks::Now();

  const auto device_enrollment_domain =
      device_signals::TryGetEnrollmentDomain(browser_cloud_policy_manager_);
  if (device_enrollment_domain) {
    signals.Set(device_signals::names::kDeviceEnrollmentDomain,
                device_enrollment_domain.value());
  }

  const auto user_enrollment_domain = device_signals::TryGetEnrollmentDomain(
      dependency_factory_->GetUserCloudPolicyManager());
  if (user_enrollment_domain) {
    signals.Set(device_signals::names::kUserEnrollmentDomain,
                user_enrollment_domain.value());
  }

  // On Chrome Browser, the trigger is currently always a browser navigation.
  signals.Set(
      device_signals::names::kTrigger,
      static_cast<int32_t>(device_signals::Trigger::kBrowserNavigation));

  auto barrier_closure = base::BarrierClosure(
      /*num_closures=*/signals_aggregator_ ? 2 : 1,
      base::BindOnce(&BrowserSignalsDecorator::OnAllSignalsReceived,
                     weak_ptr_factory_.GetWeakPtr(), start_time,
                     std::move(done_closure)));

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::BindOnce(&enterprise_signals::DeviceInfoFetcher::Fetch,
                     enterprise_signals::DeviceInfoFetcher::CreateInstance()),
      base::BindOnce(&BrowserSignalsDecorator::OnDeviceInfoFetched,
                     weak_ptr_factory_.GetWeakPtr(), std::ref(signals),
                     barrier_closure));

  if (signals_aggregator_) {
    device_signals::SignalsAggregationRequest request;
    request.signal_names.emplace(device_signals::SignalName::kAgent);

    if (IsDTCAntivirusSignalEnabled()) {
      request.signal_names.emplace(device_signals::SignalName::kAntiVirus);
    }
    signals_aggregator_->GetSignals(
        request,
        base::BindOnce(&BrowserSignalsDecorator::OnAggregatedSignalsReceived,
                       weak_ptr_factory_.GetWeakPtr(), std::ref(signals),
                       barrier_closure));
  }
}

void BrowserSignalsDecorator::OnDeviceInfoFetched(
    base::Value::Dict& signals,
    base::OnceClosure done_closure,
    const enterprise_signals::DeviceInfo& device_info) {
  signals.Set(device_signals::names::kSerialNumber, device_info.serial_number);
  signals.Set(device_signals::names::kScreenLockSecured,
              static_cast<int32_t>(device_info.screen_lock_secured));
  signals.Set(device_signals::names::kDiskEncrypted,
              static_cast<int32_t>(device_info.disk_encrypted));
  signals.Set(device_signals::names::kDeviceHostName,
              device_info.device_host_name);
  signals.Set(device_signals::names::kMacAddresses,
              ToListValue(RemoveDuplicates(device_info.mac_addresses)));

  if (device_info.windows_machine_domain) {
    signals.Set(device_signals::names::kWindowsMachineDomain,
                device_info.windows_machine_domain.value());
  }

  if (device_info.windows_user_domain) {
    signals.Set(device_signals::names::kWindowsUserDomain,
                device_info.windows_user_domain.value());
  }

  if (device_info.secure_boot_enabled) {
    signals.Set(device_signals::names::kSecureBootEnabled,
                static_cast<int32_t>(device_info.secure_boot_enabled.value()));
  }

  std::move(done_closure).Run();
}

void BrowserSignalsDecorator::OnAggregatedSignalsReceived(
    base::Value::Dict& signals,
    base::OnceClosure done_closure,
    device_signals::SignalsAggregationResponse response) {
  if (response.agent_signals_response &&
      response.agent_signals_response->crowdstrike_signals) {
    auto serialized_crowdstrike_signals =
        response.agent_signals_response->crowdstrike_signals->ToValue();
    if (serialized_crowdstrike_signals) {
      signals.Set(device_signals::names::kCrowdStrike,
                  std::move(serialized_crowdstrike_signals.value()));
    }
  }

#if BUILDFLAG(IS_WIN)
  if (IsDTCAntivirusSignalEnabled()) {
    device_signals::InstalledAntivirusState antivirus_state{
        device_signals::InstalledAntivirusState::kNone};
    if (response.av_signal_response) {
      antivirus_state = response.av_signal_response->antivirus_state;
    }
    signals.Set(device_signals::names::kAntivirusState,
                static_cast<int>(antivirus_state));
  }
#endif

  std::move(done_closure).Run();
}

void BrowserSignalsDecorator::OnAllSignalsReceived(
    base::TimeTicks start_time,
    base::OnceClosure done_closure) {
  LogSignalsCollectionLatency(kLatencyHistogramVariant, start_time);
  std::move(done_closure).Run();
}

}  // namespace enterprise_connectors
