// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/policy/remote_commands/crd/device_command_start_crd_session_job.h"

#include <iomanip>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "base/check_deref.h"
#include "base/check_is_test.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/notreached.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/ash/policy/remote_commands/crd/crd_logging.h"
#include "chrome/browser/ash/policy/remote_commands/crd/crd_remote_command_utils.h"
#include "chrome/browser/ash/policy/remote_commands/crd/crd_uma_logger.h"
#include "chrome/browser/ash/policy/remote_commands/crd/public/crd_session_result_codes.h"
#include "chrome/browser/ash/policy/remote_commands/crd/start_crd_session_job_delegate.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/device_identity/device_oauth2_token_service.h"
#include "chrome/browser/device_identity/device_oauth2_token_service_factory.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/pref_names.h"
#include "components/policy/policy_constants.h"
#include "components/policy/proto/device_management_backend.pb.h"
#include "components/prefs/pref_service.h"
#include "remoting/host/chromeos/features.h"

namespace policy {

namespace {

using SessionParameters = StartCrdSessionJobDelegate::SessionParameters;
using ErrorCallback = StartCrdSessionJobDelegate::ErrorCallback;

// Job parameters fields:

// Job requires that UI was idle for at least this period of time
// to proceed. If absent / equal to 0, job will proceed regardless of user
// activity.
const char kIdlenessCutoffFieldName[] = "idlenessCutoffSec";

// True if the admin has confirmed that they want to start the CRD session
// while a user is currently using the device.
const char kAckedUserPresenceFieldName[] = "ackedUserPresence";

// The type of CRD session that the admin wants to start.
const char kCrdSessionTypeFieldName[] = "crdSessionType";

// The admin's email address.
const char kAdminEmailFieldName[] = "adminEmail";

// True if CRD should show a confirmation dialog to the user to allow them
// to confirm/reject the admin session.
const char kShowConfirmationDialogFieldName[] = "showConfirmationDialog";

// Result payload fields:

// Integer value containing DeviceCommandStartCrdSessionJob::ResultCode
const char kResultCodeFieldName[] = "resultCode";

// CRD Access Code if job was completed successfully
const char kResultAccessCodeFieldName[] = "accessCode";

// Optional detailed error message for error result codes.
const char kResultMessageFieldName[] = "message";

// Period in seconds since last user activity, if job finished with
// FAILURE_NOT_IDLE result code.
const char kResultLastActivityFieldName[] = "lastActivitySec";

// Cutoff time to check if the device was idle in the last 5 minutes.
const base::TimeDelta kAutoApproveDeviceIdlenessCutoff = base::Minutes(5);

// Timeout used to countdown before the connection request is auto accepted and
// the session starts.
const base::TimeDelta kConnectionAutoAcceptTimeout = base::Seconds(30);

// Session cutoff to enforce a maximum duration for shared CRD sessions,
// automatically terminating sessions exceeding this limit.
const base::TimeDelta kMaximumRemoteSupportSessionDuration = base::Hours(8);

// Ensure the admin is able to control the host during the CRD session.
const bool kAllowRemoteInput = true;

// Ensure the admin is able to use clipboard with the host during the CRD
// session.
const bool kAllowClipboardSync = true;

std::optional<std::string> FindString(const base::Value::Dict& dict,
                                      std::string_view key) {
  if (!dict.contains(key)) {
    return std::nullopt;
  }
  return *dict.FindString(key);
}

void SendResultCodeToUma(CrdSessionType crd_session_type,
                         UserSessionType user_session_type,
                         ExtendedStartCrdSessionResultCode result_code) {
  base::UmaHistogramEnumeration("Enterprise.DeviceRemoteCommand.Crd.Result",
                                result_code);

  CrdUmaLogger(crd_session_type, user_session_type)
      .LogSessionLaunchResult(result_code);
}

std::string CreateSuccessPayload(const std::string& access_code) {
  return base::WriteJson(
             base::Value::Dict()
                 .Set(kResultCodeFieldName,
                      static_cast<int>(
                          StartCrdSessionResultCode::START_CRD_SESSION_SUCCESS))
                 .Set(kResultAccessCodeFieldName, access_code))
      .value();
}

std::string CreateNonIdlePayload(const base::TimeDelta& time_delta) {
  return base::WriteJson(
             base::Value::Dict()
                 .Set(kResultCodeFieldName,
                      static_cast<int>(
                          StartCrdSessionResultCode::FAILURE_NOT_IDLE))
                 .Set(kResultLastActivityFieldName,
                      static_cast<int>(time_delta.InSeconds())))
      .value();
}

std::string CreateErrorPayload(StartCrdSessionResultCode result_code,
                               const std::string& error_message) {
  CHECK_NE(result_code, StartCrdSessionResultCode::START_CRD_SESSION_SUCCESS);
  CHECK_NE(result_code, StartCrdSessionResultCode::FAILURE_NOT_IDLE);

  auto payload = base::Value::Dict()  //
                     .Set(kResultCodeFieldName, static_cast<int>(result_code));
  if (!error_message.empty()) {
    payload.Set(kResultMessageFieldName, error_message);
  }
  return base::WriteJson(payload).value();
}

DeviceOAuth2TokenService* GetOAuthService() {
  return DeviceOAuth2TokenServiceFactory::Get();
}

std::string GetRobotAccountUserName(const DeviceOAuth2TokenService* service) {
  CoreAccountId account_id = CHECK_DEREF(service).GetRobotAccountId();

  // TODO(msarda): This conversion will not be correct once account id is
  // migrated to be the Gaia ID on ChromeOS. Fix it.
  return account_id.ToString();
}

CrdSessionType ToCrdSessionTypeOrDefault(std::optional<int> int_value,
                                         CrdSessionType default_value) {
  if (!int_value.has_value() ||
      !enterprise_management::CrdSessionType_IsValid(int_value.value())) {
    return default_value;
  }
  return static_cast<CrdSessionType>(int_value.value());
}

void OnCrdSessionFinished(CrdSessionType crd_session_type,
                          UserSessionType user_session_type,
                          base::TimeDelta session_duration) {
  CrdUmaLogger(crd_session_type, user_session_type)
      .LogSessionDuration(session_duration);
}

bool IsKioskSession(UserSessionType session_type) {
  return session_type == UserSessionType::AUTO_LAUNCHED_KIOSK_SESSION ||
         session_type == UserSessionType::MANUALLY_LAUNCHED_KIOSK_SESSION;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// DeviceCommandStartCrdSessionJob
////////////////////////////////////////////////////////////////////////////////

DeviceCommandStartCrdSessionJob::DeviceCommandStartCrdSessionJob(
    Delegate& delegate)
    : delegate_(delegate),
      robot_account_id_(GetRobotAccountUserName(GetOAuthService())) {}

DeviceCommandStartCrdSessionJob::DeviceCommandStartCrdSessionJob(
    Delegate& delegate,
    std::string_view robot_account_id)
    : delegate_(delegate), robot_account_id_(robot_account_id) {
  CHECK_IS_TEST();
}

DeviceCommandStartCrdSessionJob::~DeviceCommandStartCrdSessionJob() = default;

enterprise_management::RemoteCommand_Type
DeviceCommandStartCrdSessionJob::GetType() const {
  return enterprise_management::RemoteCommand::DEVICE_START_CRD_SESSION;
}

bool DeviceCommandStartCrdSessionJob::ParseCommandPayload(
    const std::string& command_payload) {
  std::optional<base::Value::Dict> root =
      base::JSONReader::ReadDict(command_payload);
  if (!root) {
    LOG(WARNING) << "Rejecting remote command with invalid payload: "
                 << std::quoted(command_payload);
    return false;
  }
  CRD_VLOG(1) << "Received remote command with payload "
              << std::quoted(command_payload);

  idleness_cutoff_ =
      base::Seconds(root->FindInt(kIdlenessCutoffFieldName).value_or(0));

  acked_user_presence_ =
      root->FindBool(kAckedUserPresenceFieldName).value_or(false);

  CrdSessionType crd_session_type =
      ToCrdSessionTypeOrDefault(root->FindInt(kCrdSessionTypeFieldName),
                                CrdSessionType::REMOTE_SUPPORT_SESSION);

  admin_email_ = FindString(*root, kAdminEmailFieldName);

  show_confirmation_dialog_ = root->FindBool(kShowConfirmationDialogFieldName);

  curtain_local_user_session_ =
      (crd_session_type == CrdSessionType::REMOTE_ACCESS_SESSION);

  return true;
}

void DeviceCommandStartCrdSessionJob::RunImpl(
    CallbackWithResult result_callback) {
  CRD_LOG(INFO) << "Running start CRD session command";

  if (delegate_->HasActiveSession()) {
    CRD_VLOG(1) << "Terminating active session";
    delegate_->TerminateSession();
    CHECK(!delegate_->HasActiveSession());
  }

  result_callback_ = std::move(result_callback);

  if (!UserTypeSupportsCrd()) {
    return FinishWithError(
        ExtendedStartCrdSessionResultCode::kFailureUnsupportedUserType, "");
  }

  if (IsRemoteAccessSession() && !IsRemoteAccessAllowedByPolicy(CHECK_DEREF(
                                     g_browser_process->local_state()))) {
    LOG(ERROR) << "Rejecting CRD session type as CRD remote access is disabled "
                  "by device policy.";
    return FinishWithError(
        ExtendedStartCrdSessionResultCode::kFailureDisabledByPolicy, "");
  }

  if (!IsDeviceIdle()) {
    FinishWithNotIdleError();
    return;
  }

  // First perform managed network check,
  CheckManagedNetworkASync(
      // Then start the CRD host.
      base::BindOnce(&DeviceCommandStartCrdSessionJob::StartCrdHostAndGetCode,
                     weak_factory_.GetWeakPtr()));
}

void DeviceCommandStartCrdSessionJob::CheckManagedNetworkASync(
    base::OnceCallback<void(bool)> on_success) {
  CalculateIsInManagedEnvironmentAsync(base::BindOnce(
      [](base::OnceCallback<void(bool)> on_success, ErrorCallback on_error,
         bool require_managed_environment, bool is_in_managed_environment) {
        if (require_managed_environment && !is_in_managed_environment) {
          std::move(on_error).Run(
              ExtendedStartCrdSessionResultCode::kFailureUnmanagedEnvironment,
              /*error_messages=*/"");
        } else {
          std::move(on_success).Run(is_in_managed_environment);
        }
      },
      std::move(on_success), GetErrorCallback(),
      /*require_managed_environment=*/IsRemoteAccessSession()));
}

void DeviceCommandStartCrdSessionJob::StartCrdHostAndGetCode(
    bool is_in_managed_environment) {
  CRD_VLOG(1) << "Starting CRD host and retrieving CRD access code";
  SessionParameters parameters;
  parameters.user_name = robot_account_id_;
  parameters.terminate_upon_input = ShouldTerminateUponInput();
  parameters.show_confirmation_dialog = ShouldShowConfirmationDialog();
  parameters.curtain_local_user_session = IsRemoteAccessSession();
  parameters.admin_email = admin_email_;
  parameters.allow_troubleshooting_tools = ShouldAllowTroubleshootingTools();
  parameters.show_troubleshooting_tools = ShouldShowTroubleshootingTools();
  parameters.allow_reconnections = ShouldAllowReconnections();
  parameters.allow_file_transfer = ShouldAllowFileTransfer();
  parameters.allow_remote_input = kAllowRemoteInput;
  parameters.allow_clipboard_sync = kAllowClipboardSync;
  parameters.request_origin =
      StartCrdSessionJobDelegate::RequestOrigin::kEnterpriseAdmin;
  if (ShouldAutoAcceptSession(is_in_managed_environment)) {
    parameters.connection_auto_accept_timeout = kConnectionAutoAcceptTimeout;
  }
  if (IsRemoteSupportSession()) {
    parameters.maximum_session_duration = kMaximumRemoteSupportSessionDuration;
  }

  delegate_->StartCrdHostAndGetCode(
      parameters,
      base::BindOnce(&DeviceCommandStartCrdSessionJob::FinishWithSuccess,
                     weak_factory_.GetWeakPtr()),
      base::BindOnce(&DeviceCommandStartCrdSessionJob::FinishWithError,
                     weak_factory_.GetWeakPtr()),
      base::BindOnce(&OnCrdSessionFinished, GetCrdSessionType(),
                     GetCurrentUserSessionType()));
}

void DeviceCommandStartCrdSessionJob::FinishWithSuccess(
    const std::string& access_code) {
  CRD_LOG(INFO) << "Successfully received CRD access code";
  if (!result_callback_) {
    return;  // Task was terminated.
  }

  SendResultCodeToUma(GetCrdSessionType(), GetCurrentUserSessionType(),
                      ExtendedStartCrdSessionResultCode::kSuccess);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(result_callback_), ResultType::kSuccess,
                     CreateSuccessPayload(access_code)));
}

void DeviceCommandStartCrdSessionJob::FinishWithError(
    const ExtendedStartCrdSessionResultCode result_code,
    const std::string& message) {
  CHECK_NE(result_code, ExtendedStartCrdSessionResultCode::kSuccess);
  CRD_LOG(INFO) << "Not starting CRD session because of error (code "
                << static_cast<int>(result_code) << ", message '" << message
                << "')";
  if (!result_callback_) {
    return;  // Task was terminated.
  }

  SendResultCodeToUma(GetCrdSessionType(), GetCurrentUserSessionType(),
                      result_code);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(result_callback_), ResultType::kFailure,
                     CreateErrorPayload(
                         ToStartCrdSessionResultCode(result_code), message)));
}

void DeviceCommandStartCrdSessionJob::FinishWithNotIdleError() {
  CRD_LOG(INFO) << "Not starting CRD session because device is not idle";
  if (!result_callback_) {
    return;  // Task was terminated.
  }

  SendResultCodeToUma(GetCrdSessionType(), GetCurrentUserSessionType(),
                      ExtendedStartCrdSessionResultCode::kFailureNotIdle);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(result_callback_), ResultType::kFailure,
                     CreateNonIdlePayload(GetDeviceIdleTime())));
}

bool DeviceCommandStartCrdSessionJob::UserTypeSupportsCrd() const {
  CRD_VLOG(2) << "User is of type "
              << UserSessionTypeToString(GetCurrentUserSessionType());

  if (IsRemoteAccessSession()) {
    return UserSessionSupportsRemoteAccess(GetCurrentUserSessionType());
  } else {
    return UserSessionSupportsRemoteSupport(GetCurrentUserSessionType());
  }
}

CrdSessionType DeviceCommandStartCrdSessionJob::GetCrdSessionType() const {
  if (IsRemoteAccessSession()) {
    return CrdSessionType::REMOTE_ACCESS_SESSION;
  }
  return CrdSessionType::REMOTE_SUPPORT_SESSION;
}

bool DeviceCommandStartCrdSessionJob::IsDeviceIdle() const {
  return GetDeviceIdleTime() >= idleness_cutoff_;
}

bool DeviceCommandStartCrdSessionJob::IsRemoteSupportSession() const {
  return !curtain_local_user_session_;
}

bool DeviceCommandStartCrdSessionJob::IsRemoteAccessSession() const {
  return curtain_local_user_session_;
}

bool DeviceCommandStartCrdSessionJob::ShouldShowConfirmationDialog() const {
  if (show_confirmation_dialog_.has_value()) {
    return show_confirmation_dialog_.value();
  }
  switch (GetCurrentUserSessionType()) {
    case UserSessionType::AUTO_LAUNCHED_KIOSK_SESSION:
    case UserSessionType::MANUALLY_LAUNCHED_KIOSK_SESSION:
      return false;

    case UserSessionType::NO_SESSION:
      return GetCrdSessionType() == CrdSessionType::REMOTE_SUPPORT_SESSION;

    case UserSessionType::AFFILIATED_USER_SESSION:
    case UserSessionType::MANAGED_GUEST_SESSION:
    case UserSessionType::UNAFFILIATED_USER_SESSION:
    case UserSessionType::GUEST_SESSION:
      return true;

    case UserSessionType::USER_SESSION_TYPE_UNKNOWN:
      NOTREACHED();
  }
}

bool DeviceCommandStartCrdSessionJob::ShouldTerminateUponInput() const {
  if (IsRemoteAccessSession()) {
    return false;
  }

  switch (GetCurrentUserSessionType()) {
    case UserSessionType::AFFILIATED_USER_SESSION:
    case UserSessionType::MANAGED_GUEST_SESSION:
      // We never terminate upon input for the user-session scenarios, because:
      //   1. There is no risk of the admin spying on the users, as they need to
      //       explicitly accept the connection request.
      //   2. If we terminate upon input the session will immediately be
      //      terminated as soon as the user accepts the connection request,
      //      as pressing the button to accept the connection request counts as
      //      user input.
      return false;

    case UserSessionType::AUTO_LAUNCHED_KIOSK_SESSION:
    case UserSessionType::MANUALLY_LAUNCHED_KIOSK_SESSION:
      return !acked_user_presence_;

    case UserSessionType::NO_SESSION:
      return GetCrdSessionType() != CrdSessionType::REMOTE_SUPPORT_SESSION;

    case UserSessionType::UNAFFILIATED_USER_SESSION:
    case UserSessionType::GUEST_SESSION:
      return true;

    case UserSessionType::USER_SESSION_TYPE_UNKNOWN:
      NOTREACHED();
  }
}

bool DeviceCommandStartCrdSessionJob::ShouldAllowReconnections() const {
  // Curtained off sessions support reconnections if Chrome restarts.
  return IsRemoteAccessSession();
}

bool DeviceCommandStartCrdSessionJob::ShouldShowTroubleshootingTools() const {
  return IsKioskSession(GetCurrentUserSessionType());
}

bool DeviceCommandStartCrdSessionJob::ShouldAllowTroubleshootingTools() const {
  return IsKioskSession(GetCurrentUserSessionType()) &&
         CHECK_DEREF(ProfileManager::GetActiveUserProfile()->GetPrefs())
             .GetBoolean(prefs::kKioskTroubleshootingToolsEnabled);
}

bool DeviceCommandStartCrdSessionJob::ShouldAllowFileTransfer() const {
  return IsKioskSession(GetCurrentUserSessionType());
}

bool DeviceCommandStartCrdSessionJob::ShouldAutoAcceptSession(
    bool is_in_managed_environment) const {
  if (!base::FeatureList::IsEnabled(
          remoting::features::kAutoApproveEnterpriseSharedSessions)) {
    return false;
  }

  return is_in_managed_environment && ShouldShowConfirmationDialog() &&
         GetDeviceIdleTime() <= kAutoApproveDeviceIdlenessCutoff;
}

ErrorCallback DeviceCommandStartCrdSessionJob::GetErrorCallback() {
  return base::BindOnce(&DeviceCommandStartCrdSessionJob::FinishWithError,
                        weak_factory_.GetWeakPtr());
}

void DeviceCommandStartCrdSessionJob::TerminateImpl() {
  result_callback_.Reset();
  weak_factory_.InvalidateWeakPtrs();
  delegate_->TerminateSession();
}

}  // namespace policy
