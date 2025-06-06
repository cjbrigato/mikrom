// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ENTERPRISE_CONNECTORS_REPORTING_REALTIME_REPORTING_CLIENT_H_
#define CHROME_BROWSER_ENTERPRISE_CONNECTORS_REPORTING_REALTIME_REPORTING_CLIENT_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "build/build_config.h"
#include "components/enterprise/common/proto/upload_request_response.pb.h"
#include "components/enterprise/connectors/core/common.h"
#include "components/enterprise/connectors/core/realtime_reporting_client_base.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/policy/core/common/cloud/cloud_policy_client.h"

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
#include "components/device_signals/core/browser/signals_types.h"
#endif

namespace content {
class BrowserContext;
}

namespace signin {
class IdentityManager;
}

#if BUILDFLAG(IS_CHROMEOS)

namespace user_manager {
class User;
}

#endif

namespace enterprise_connectors {

// An event router that observes Safe Browsing events and notifies listeners.
// The router also uploads events to the chrome reporting server side API. The
// browser-based reporting logics are in RealtimeReportingClientBase whereas
// profile-based reporting logics are implemented in this class.
class RealtimeReportingClient : public RealtimeReportingClientBase {
 public:
  explicit RealtimeReportingClient(content::BrowserContext* context);

  RealtimeReportingClient(const RealtimeReportingClient&) = delete;
  RealtimeReportingClient& operator=(const RealtimeReportingClient&) = delete;

  ~RealtimeReportingClient() override;

  // RealtimeReportingClientBase overrides:
  std::string GetProfileUserName() override;
  base::WeakPtr<RealtimeReportingClientBase> AsWeakPtr() override;
  std::optional<ReportingSettings> GetReportingSettings() override;

  base::WeakPtr<RealtimeReportingClient> AsWeakPtrImpl() {
    return weak_ptr_factory_.GetWeakPtr();
  }

  void SetBrowserCloudPolicyClientForTesting(policy::CloudPolicyClient* client);
  void SetProfileCloudPolicyClientForTesting(policy::CloudPolicyClient* client);

  void SetProfileUserNameForTesting(std::string username);

  void SetIdentityManagerForTesting(signin::IdentityManager* identity_manager);

  // policy::CloudPolicyClient::Observer overrides:
  void OnClientError(policy::CloudPolicyClient* client) override;

  // Report safe browsing event through real-time reporting channel, if enabled.
  // Declared as virtual for tests.
  virtual void ReportRealtimeEvent(const std::string& name,
                                   const ReportingSettings& settings,
                                   base::Value::Dict event);

  base::Value::Dict ReportErrorDetails(
      const policy::CloudPolicyClient::Result& upload_result);

  // Report safe browsing events that have occurred in the past but has not yet
  // been reported. This is currently used for browser crash events, which are
  // polled at a fixed time interval. Declared as virtual for tests.
  virtual void ReportPastEvent(const std::string& name,
                               const ReportingSettings& settings,
                               base::Value::Dict event,
                               const base::Time& time);

 private:
  // RealtimeReportingClientBase overrides (all overrides below):
  std::string GetProfileIdentifier() override;
  std::string GetBrowserClientId() override;
  base::Value::Dict GetContext() override;
  ::chrome::cros::reporting::proto::UploadEventsRequest
  CreateUploadEventsRequest() override;
  bool ShouldIncludeDeviceInfo(bool per_profile) override;
  void UploadCallbackDeprecated(
      base::Value::Dict event_wrapper,
      bool per_profile,
      policy::CloudPolicyClient* client,
      EnterpriseReportingEventType event_type,
      base::TimeTicks upload_started_at,
      policy::CloudPolicyClient::Result upload_result) override;
  void UploadCallback(
      ::chrome::cros::reporting::proto::UploadEventsRequest request,
      bool per_profile,
      policy::CloudPolicyClient* client,
      EnterpriseReportingEventType event_type,
      base::TimeTicks upload_started_at,
      policy::CloudPolicyClient::Result upload_result) override;

#if !BUILDFLAG(IS_CHROMEOS)
  std::pair<std::string, policy::CloudPolicyClient*> InitProfileReportingClient(
      const std::string& dm_token) override;
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  // DEPRECATED: Use MaybeCollectDeviceSignalsAndReportEvent(Event, ...).
  void MaybeCollectDeviceSignalsAndReportEventDeprecated(
      base::Value::Dict event,
      policy::CloudPolicyClient* client,
      std::string name,
      const ReportingSettings& settings,
      base::Time time) override;

  // Add Crowdstrike signals to event report and upload it.
  // DEPRECATED: Use PopulateSignalsAndReportEvent(Event, ...) instead.
  void PopulateSignalsAndReportEventDeprecated(
      base::Value::Dict event,
      policy::CloudPolicyClient* client,
      std::string name,
      ReportingSettings settings,
      content::BrowserContext* context,
      base::Time time,
      device_signals::SignalsAggregationResponse response);

  void MaybeCollectDeviceSignalsAndReportEvent(
      ::chrome::cros::reporting::proto::Event event,
      policy::CloudPolicyClient* client,
      const ReportingSettings& settings) override;
  // Add Crowdstrike signals to event report and upload it.
  void PopulateSignalsAndReportEvent(
      ::chrome::cros::reporting::proto::Event event,
      policy::CloudPolicyClient* client,
      ReportingSettings settings,
      device_signals::SignalsAggregationResponse response);
#endif

  // Handle the availability of a cloud policy client.
  void OnCloudPolicyClientAvailable(const std::string& policy_client_desc,
                                    policy::CloudPolicyClient* client);

#if BUILDFLAG(IS_CHROMEOS)
  // Return the Chrome OS user who is subject to reporting, or nullptr if
  // the user cannot be deterined.
  static const user_manager::User* GetChromeOSUser();
#endif

  void RemoveDmTokenFromRejectedSet(const std::string& dm_token);

  raw_ptr<content::BrowserContext> context_;
  std::string username_;

  base::WeakPtrFactory<RealtimeReportingClient> weak_ptr_factory_{this};
};

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
// Populate event dict with CrowdStrike signal values. If those signals are
// available in `response`, this function returns a Dict with the following
// fields added:
// "securityAgents" : [
//   {
//     "crowdstrike": {
//       "agent_id": "agent-123",
//       "customer_id": "customer-123"
//     }
//   }
// ]
// These must match proto specified in
// chrome/cros/reporting/api/proto/browser_events.proto
void AddCrowdstrikeSignalsToEvent(
    base::Value::Dict& event,
    const device_signals::SignalsAggregationResponse& response);
#endif

}  // namespace enterprise_connectors

#endif  // CHROME_BROWSER_ENTERPRISE_CONNECTORS_REPORTING_REALTIME_REPORTING_CLIENT_H_
