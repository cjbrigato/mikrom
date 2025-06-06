// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/it2me/it2me_native_messaging_host_ash.h"

#include <utility>

#include "base/feature_list.h"
#include "base/functional/callback.h"
#include "base/json/json_writer.h"
#include "base/notreached.h"
#include "base/time/time.h"
#include "base/values.h"
#include "extensions/browser/api/messaging/native_message_host.h"
#include "remoting/host/chromeos/chromeos_enterprise_params.h"
#include "remoting/host/chromeos/features.h"
#include "remoting/host/chromoting_host_context.h"
#include "remoting/host/it2me/it2me_native_messaging_host.h"
#include "remoting/host/it2me/reconnect_params.h"
#include "remoting/host/native_messaging/native_messaging_helpers.h"
#include "remoting/host/policy_watcher.h"

namespace remoting {

It2MeNativeMessageHostAsh::It2MeNativeMessageHostAsh(
    std::unique_ptr<It2MeHostFactory> host_factory)
    : host_factory_(std::move(host_factory)) {}
It2MeNativeMessageHostAsh::~It2MeNativeMessageHostAsh() = default;

mojo::PendingReceiver<mojom::SupportHostObserver>
It2MeNativeMessageHostAsh::Start(
    std::unique_ptr<ChromotingHostContext> context,
    std::unique_ptr<PolicyWatcher> policy_watcher) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!native_message_host_);

  // Create the remote IPC channel before starting the NMH so any errors are
  // queued for sending once the receiver end of the channel is bound.
  mojo::PendingReceiver<mojom::SupportHostObserver> observer =
      remote_.BindNewPipeAndPassReceiver();

  remote_.set_disconnect_handler(base::BindOnce(
      &It2MeNativeMessageHostAsh::Disconnect, base::Unretained(this)));

  native_message_host_ = std::make_unique<It2MeNativeMessagingHost>(
      /*needs_elevation=*/false, std::move(policy_watcher), std::move(context),
      host_factory_->Clone());
  native_message_host_->Start(this);

  return observer;
}

void It2MeNativeMessageHostAsh::Connect(
    const mojom::SupportSessionParams& params,
    const std::optional<ChromeOsEnterpriseParams>& enterprise_params,
    const std::optional<ReconnectParams>& reconnect_params,
    base::OnceClosure connected_callback,
    HostStateConnectedCallback host_state_connected_callback,
    base::OnceClosure host_state_disconnected_callback,
    base::OnceClosure disconnected_callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(native_message_host_);
  DCHECK(!connected_callback_);
  DCHECK(!disconnected_callback_);

  connected_callback_ = std::move(connected_callback);
  disconnected_callback_ = std::move(disconnected_callback);
  host_state_connected_callback_ = std::move(host_state_connected_callback);
  host_state_disconnected_callback_ =
      std::move(host_state_disconnected_callback);

  auto message =
      base::Value::Dict()
          .Set(kMessageType, kConnectMessage)
          .Set(kUserName, params.user_name)
          .Set(kAccessToken, params.oauth_access_token)
          .Set(kIsEnterpriseAdminUser, enterprise_params.has_value());
  if (enterprise_params.has_value()) {
    message.Merge(enterprise_params->ToDict());
  }

  if (params.authorized_helper.has_value()) {
    message.Set(kAuthorizedHelper, *params.authorized_helper);
  }

  if (reconnect_params.has_value()) {
    // We persist the previously connected user as the `authorized_helper`, to
    // prevent anyone else from snooping in and connecting to the session.
    CHECK(params.authorized_helper.has_value());

    message.Set(kReconnectParamsDict,
                ReconnectParams::ToDict(*reconnect_params));
  }

  native_message_host_->OnMessage(*base::WriteJson(message));
}

void It2MeNativeMessageHostAsh::Disconnect() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  native_message_host_->OnMessage(*base::WriteJson(
      base::Value::Dict().Set(kMessageType, kDisconnectMessage)));

  // Notify the owner that the host has been disconnected.  This will result in
  // the destruction of this object so do not access member variables after this
  // callback is run.
  std::move(disconnected_callback_).Run();
}

void It2MeNativeMessageHostAsh::PostMessageFromNativeHost(
    const std::string& message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::string type;
  base::Value::Dict contents;
  if (!ParseNativeMessageJson(message, type, contents)) {
    CloseChannel(std::string());
    return;
  }

  if (type.empty()) {
    LOG(ERROR) << "'type' not found in request.";
    CloseChannel(ErrorCodeToString(protocol::ErrorCode::INVALID_ARGUMENT));
    return;
  }

  if (type == kConnectResponse) {
    HandleConnectResponse();
  } else if (type == kDisconnectResponse) {
    HandleDisconnectResponse();
  } else if (type == kHostStateChangedMessage) {
    HandleHostStateChangeMessage(std::move(contents));
  } else if (type == kNatPolicyChangedMessage) {
    HandleNatPolicyChangedMessage(std::move(contents));
  } else if (type == kPolicyErrorMessage) {
    HandlePolicyErrorMessage(std::move(contents));
  } else if (type == kErrorMessage) {
    HandleErrorMessage(std::move(contents));
  } else {
    LOG(ERROR) << "Unsupported message type: " << type;
    CloseChannel(ErrorCodeToString(protocol::ErrorCode::INCOMPATIBLE_PROTOCOL));
  }
}

void It2MeNativeMessageHostAsh::CloseChannel(const std::string& error_message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  LOG_IF(ERROR, !error_message.empty())
      << "CloseChannel called with error: " << error_message;
  Disconnect();
}

void It2MeNativeMessageHostAsh::HandleConnectResponse() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(connected_callback_).Run();
}

void It2MeNativeMessageHostAsh::HandleDisconnectResponse() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  remote_->OnHostStateDisconnected(ErrorCodeToString(protocol::ErrorCode::OK));
}

void It2MeNativeMessageHostAsh::HandleHostStateChangeMessage(
    base::Value::Dict message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string* new_state = message.FindString(kState);
  if (!new_state) {
    LOG(ERROR) << "Missing |" << kState << "| value in message.";
    CloseChannel(ErrorCodeToString(protocol::ErrorCode::INVALID_ARGUMENT));
    return;
  }

  if (*new_state == kHostStateStarting) {
    remote_->OnHostStateStarting();
  } else if (*new_state == kHostStateDisconnected) {
    const std::string* disconnect_reason =
        message.FindString(kDisconnectReason);
    remote_->OnHostStateDisconnected(
        disconnect_reason ? *disconnect_reason
                          : ErrorCodeToString(protocol::ErrorCode::OK));
    std::move(host_state_disconnected_callback_).Run();
  } else if (*new_state == kHostStateRequestedAccessCode) {
    remote_->OnHostStateRequestedAccessCode();
  } else if (*new_state == kHostStateReceivedAccessCode) {
    const std::string* access_code = message.FindString(kAccessCode);
    if (!access_code) {
      LOG(ERROR) << "Missing |" << kAccessCode << "| value in message.";
      CloseChannel(ErrorCodeToString(protocol::ErrorCode::INVALID_ARGUMENT));
      return;
    }
    std::optional<int> access_code_lifetime =
        message.FindInt(kAccessCodeLifetime);
    if (!access_code_lifetime) {
      LOG(ERROR) << "Missing |" << kAccessCodeLifetime << "| value in message.";
      CloseChannel(ErrorCodeToString(protocol::ErrorCode::INVALID_ARGUMENT));
      return;
    }
    remote_->OnHostStateReceivedAccessCode(
        *access_code, base::Seconds(*access_code_lifetime));
  } else if (*new_state == kHostStateConnecting) {
    remote_->OnHostStateConnecting();
  } else if (*new_state == kHostStateConnected) {
    const std::string* remote_username = message.FindString(kClient);
    if (!remote_username) {
      LOG(ERROR) << "Missing |" << kClient << "| value in message.";
      CloseChannel(ErrorCodeToString(protocol::ErrorCode::INVALID_ARGUMENT));
      return;
    }
    remote_->OnHostStateConnected(*remote_username);

    std::optional<ReconnectParams> reconnect_params;
    const auto* reconnect_params_ptr = message.FindDict(kReconnectParamsDict);
    if (reconnect_params_ptr) {
      reconnect_params.emplace(
          ReconnectParams::FromDict(*reconnect_params_ptr));
    }
    std::move(host_state_connected_callback_).Run(std::move(reconnect_params));

  } else if (*new_state == kHostStateError) {
    const std::string* error_code_string =
        message.FindString(kErrorMessageCode);
    if (!error_code_string) {
      LOG(ERROR) << "Missing |" << kErrorMessageCode << "| value in message.";
      CloseChannel(ErrorCodeToString(protocol::ErrorCode::INVALID_ARGUMENT));
      return;
    }

    protocol::ErrorCode error_code;
    if (!ParseErrorCode(*error_code_string, &error_code)) {
      LOG(ERROR) << "Invalid |" << kErrorMessageCode << "| value "
                 << *error_code_string << "in message.";
      CloseChannel(ErrorCodeToString(protocol::ErrorCode::INVALID_ARGUMENT));
      return;
    }
    remote_->OnHostStateError(static_cast<int64_t>(error_code));
  } else if (*new_state == kHostStateDomainError) {
    remote_->OnInvalidDomainError();
  } else {
    NOTREACHED() << "Unknown state: " << *new_state;
  }
}

void It2MeNativeMessageHostAsh::HandleNatPolicyChangedMessage(
    base::Value::Dict message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::optional<bool> nat_enabled =
      message.FindBool(kNatPolicyChangedMessageNatEnabled);
  if (!nat_enabled.has_value()) {
    LOG(ERROR) << "Missing |" << kNatPolicyChangedMessageNatEnabled
               << "| value in message.";
    CloseChannel(ErrorCodeToString(protocol::ErrorCode::INVALID_ARGUMENT));
    return;
  }

  std::optional<bool> relay_enabled =
      message.FindBool(kNatPolicyChangedMessageRelayEnabled);
  if (!nat_enabled.has_value()) {
    LOG(ERROR) << "Missing |" << kNatPolicyChangedMessageRelayEnabled
               << "| value in message.";
    CloseChannel(ErrorCodeToString(protocol::ErrorCode::INVALID_ARGUMENT));
    return;
  }

  mojom::NatPolicyStatePtr nat_policy = mojom::NatPolicyState::New();
  nat_policy->nat_enabled = *nat_enabled;
  nat_policy->relay_enabled = *relay_enabled;
  remote_->OnNatPolicyChanged(std::move(nat_policy));
}

void It2MeNativeMessageHostAsh::HandlePolicyErrorMessage(
    base::Value::Dict message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  remote_->OnPolicyError();
}

void It2MeNativeMessageHostAsh::HandleErrorMessage(base::Value::Dict message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string* error_code_string = message.FindString(kErrorMessageCode);
  if (!error_code_string) {
    LOG(ERROR) << "Missing |" << kErrorMessageCode << "| value in message.";
    CloseChannel(ErrorCodeToString(protocol::ErrorCode::INVALID_ARGUMENT));
    return;
  }

  protocol::ErrorCode error_code;
  if (!ParseErrorCode(*error_code_string, &error_code)) {
    LOG(ERROR) << "Invalid |" << kErrorMessageCode << "| value "
               << *error_code_string << "in message.";
    CloseChannel(ErrorCodeToString(protocol::ErrorCode::INVALID_ARGUMENT));
    return;
  }

  remote_->OnHostStateError(static_cast<int64_t>(error_code));
}

}  // namespace remoting
