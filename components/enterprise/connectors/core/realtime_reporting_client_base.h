// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ENTERPRISE_CONNECTORS_CORE_REALTIME_REPORTING_CLIENT_BASE_H_
#define COMPONENTS_ENTERPRISE_CONNECTORS_CORE_REALTIME_REPORTING_CLIENT_BASE_H_

#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "components/enterprise/common/proto/synced_from_google3/chrome_reporting_entity.pb.h"
#include "components/enterprise/common/proto/upload_request_response.pb.h"
#include "components/enterprise/connectors/core/common.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/policy/core/common/cloud/cloud_policy_client.h"

namespace signin {
class IdentityManager;
}

namespace network {
class SharedURLLoaderFactory;
}

namespace policy {
class DeviceManagementService;
}

namespace enterprise_connectors {

// The base class of an event router that observes Safe Browsing events and
// notifies listeners. The router also uploads events to the chrome reporting
// server side API.
class RealtimeReportingClientBase : public KeyedService,
                                    public policy::CloudPolicyClient::Observer {
 public:
  static const char kKeyProfileIdentifier[];
  static const char kKeyProfileUserName[];

  RealtimeReportingClientBase(
      policy::DeviceManagementService* device_management_service,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);

  RealtimeReportingClientBase(const RealtimeReportingClientBase&) = delete;
  RealtimeReportingClientBase& operator=(const RealtimeReportingClientBase&) =
      delete;

  ~RealtimeReportingClientBase() override;

  virtual base::WeakPtr<RealtimeReportingClientBase> AsWeakPtr() = 0;

  // Determines if the real-time reporting feature is enabled.
  // Obtain settings to apply to a reporting event from ConnectorsService.
  // std::nullopt represents that reporting should not be done.
  // Declared virtual for tests.
  virtual std::optional<ReportingSettings> GetReportingSettings() = 0;

  // Report an event to the reporting server. This method will not mutate the
  // event, so it is the caller's responsibility to ensure that all relevant
  // fields have been set on the event.
  virtual void ReportEvent(::chrome::cros::reporting::proto::Event event,
                           const ReportingSettings& settings);

  // Function that uploads security events, parameterized with the time. We
  // should stop using this once the migration for the reporting events from
  // dictionary to proto is done.
  void ReportEventWithTimestampDeprecated(const std::string& name,
                                          const ReportingSettings& settings,
                                          base::Value::Dict event,
                                          const base::Time& time,
                                          bool include_profile_user_name);

  // Return the user name associated with the profile.
  virtual std::string GetProfileUserName() = 0;

  // Sub-methods called by ReportEventWithTimestamp() to provide profile related
  // information.
  virtual std::string GetProfileIdentifier() = 0;

 protected:
  // Sub-method called by InitRealtimeReportingClient() to make appropriate
  // verifications and initialize the profile reporting client. Returns a policy
  // client description and a client, which can be nullptr if it can't be
  // initialized.
#if !BUILDFLAG(IS_CHROMEOS)
  virtual std::pair<std::string, policy::CloudPolicyClient*>
  InitProfileReportingClient(const std::string& dm_token) = 0;
#endif

  // Returns the browser client id required for initializing browser reporting
  // client.
  virtual std::string GetBrowserClientId() = 0;

  // Sub-method called by ReportEventWithTimestamp() to collect device signals
  // on Windows/Mac/Linux platforms. Regardless of collecting device signals or
  // not, this method is expected to call `UploadSecurityEventReport()` in the
  // end.
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  virtual void MaybeCollectDeviceSignalsAndReportEventDeprecated(
      base::Value::Dict event,
      policy::CloudPolicyClient* client,
      std::string name,
      const ReportingSettings& settings,
      base::Time time) = 0;
  virtual void MaybeCollectDeviceSignalsAndReportEvent(
      ::chrome::cros::reporting::proto::Event event,
      policy::CloudPolicyClient* client,
      const ReportingSettings& settings) = 0;
#endif

  // Returns whether device info should be reported for browser or profile.
  virtual bool ShouldIncludeDeviceInfo(bool per_profile) = 0;

  // Callback used with UploadSecurityEventReport() to upload events to the
  // reporting server.
  virtual void UploadCallbackDeprecated(
      base::Value::Dict event_wrapper,
      bool per_profile,
      policy::CloudPolicyClient* client,
      EnterpriseReportingEventType event_type,
      base::TimeTicks upload_started_at,
      policy::CloudPolicyClient::Result upload_result) = 0;

  virtual void UploadCallback(
      ::chrome::cros::reporting::proto::UploadEventsRequest request,
      bool per_profile,
      policy::CloudPolicyClient* client,
      EnterpriseReportingEventType event_type,
      base::TimeTicks upload_started_at,
      policy::CloudPolicyClient::Result upload_result) = 0;

  // Returns a dictionary of information added to reporting events,
  // corresponding to the Device, Browser and Profile protos defined in
  // google3/google/internal/chrome/reporting/v1/chromereporting.proto.
  virtual base::Value::Dict GetContext() = 0;

  // Creates and returns an UploadEventsRequest proto with the Device, Browser
  // and Profile protos set.
  virtual ::chrome::cros::reporting::proto::UploadEventsRequest
  CreateUploadEventsRequest() = 0;

  // Initialize a real-time report client if needed.  This client is used only
  // if real-time reporting is enabled, the machine is properly reigistered
  // with CBCM and the appropriate policies are enabled.
  void InitRealtimeReportingClient(const ReportingSettings& settings);

  void OnIpAddressesFetched(
      ::chrome::cros::reporting::proto::Event event,
      policy::CloudPolicyClient* client,
      const ReportingSettings& settings,
      std::vector<std::string> ip_addresses);

  // Prepares information required by CloudPolicyClient::UploadSecurityEvent()
  // and calls it.
  void UploadSecurityEvent(::chrome::cros::reporting::proto::Event event,
                           policy::CloudPolicyClient* client,
                           const ReportingSettings& settings);

  void FinishUploadSecurityEvent(::chrome::cros::reporting::proto::Event event,
                                 policy::CloudPolicyClient* client,
                                 const ReportingSettings& settings);

  void OnIpAddressesFetchedDeprecated(
      base::Value::Dict event,
      policy::CloudPolicyClient* client,
      std::string name,
      const ReportingSettings& settings,
      base::Time time,
      std::vector<std::string> ip_addresses);

  // Prepares information required by
  // CloudPolicyClient::UploadSecurityEventReportDeprecated() and calls it.
  // DEPRECATED: Use UploadSecurityEvent() instead.
  void UploadSecurityEventReportDeprecated(base::Value::Dict event,
                                           policy::CloudPolicyClient* client,
                                           std::string name,
                                           const ReportingSettings& settings,
                                           base::Time time);

  void FinishUploadSecurityEventReportDeprecated(
      base::Value::Dict event,
      policy::CloudPolicyClient* client,
      std::string name,
      const ReportingSettings& settings);

  const std::string GetProfilePolicyClientDescription();

  raw_ptr<signin::IdentityManager, DanglingUntriaged> identity_manager_ =
      nullptr;

  // The cloud policy clients used to upload browser events and profile events
  // to the cloud. These clients are never used to fetch policies. These
  // pointers are not owned by the class.
  raw_ptr<policy::CloudPolicyClient, DanglingUntriaged> browser_client_ =
      nullptr;
  raw_ptr<policy::CloudPolicyClient, DanglingUntriaged> profile_client_ =
      nullptr;

  // The private clients are used on platforms where we cannot just get a
  // client and we create our own (used through the above client pointers).
  std::unique_ptr<policy::CloudPolicyClient> browser_private_client_;
  std::unique_ptr<policy::CloudPolicyClient> profile_private_client_;

  // When a request is rejected for a given DM token, wait 24 hours before
  // trying again for this specific DM Token.
  base::flat_map<std::string, std::unique_ptr<base::OneShotTimer>>
      rejected_dm_token_timers_;

 private:
  // Sub-method called by InitRealtimeReportingClient to make appropriate
  // verifications and initialize the browser reporting client. Returns a policy
  // client description and a client, which can be nullptr if it can't be
  // initialized.
  std::pair<std::string, policy::CloudPolicyClient*> InitBrowserReportingClient(
      const std::string& dm_token);

  // Handle the availability of a cloud policy client.
  void OnCloudPolicyClientAvailable(const std::string& policy_client_desc,
                                    policy::CloudPolicyClient* client);

  raw_ptr<policy::DeviceManagementService> device_management_service_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
};

}  // namespace enterprise_connectors

#endif  // COMPONENTS_ENTERPRISE_CONNECTORS_CORE_REALTIME_REPORTING_CLIENT_BASE_H_
