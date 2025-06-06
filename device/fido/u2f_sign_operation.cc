// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/fido/u2f_sign_operation.h"

#include <cstdint>
#include <utility>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "components/apdu/apdu_response.h"
#include "components/device_event_log/device_event_log.h"
#include "crypto/hash.h"
#include "device/fido/authenticator_get_assertion_response.h"
#include "device/fido/ctap_get_assertion_request.h"
#include "device/fido/device_response_converter.h"
#include "device/fido/features.h"
#include "device/fido/fido_device.h"
#include "device/fido/u2f_command_constructor.h"

namespace device {

void LogU2fSignOperationResult(U2fSignOperationResult result) {
  base::UmaHistogramEnumeration("WebAuthentication.U2fSignOperation", result);
}

U2fSignOperation::U2fSignOperation(FidoDevice* device,
                                   const CtapGetAssertionRequest& request,
                                   DeviceResponseCallback callback)
    : DeviceOperation(device, request, std::move(callback)) {}

U2fSignOperation::~U2fSignOperation() = default;

void U2fSignOperation::Start() {
  if (!request().allow_list.empty()) {
    if (request().alternative_application_parameter.has_value()) {
      // Try the alternative value first. This is because the U2F Zero
      // authenticator (at least) crashes if we try the wrong AppID first.
      app_param_type_ = ApplicationParameterType::kAlternative;
    }
    WinkAndTrySign();
  } else {
    // In order to make U2F authenticators blink on sign request with an empty
    // allow list, we send fake enrollment to the device and error out when the
    // user has provided presence.
    WinkAndTryFakeEnrollment();
  }
}

void U2fSignOperation::Cancel() {
  LogU2fSignOperationResult(
      failed_and_retried_ ? U2fSignOperationResult::kLowLevelErrorThenCancelled
                          : U2fSignOperationResult::kCancelled);
  canceled_ = true;
}

void U2fSignOperation::WinkAndTrySign() {
  device()->TryWink(
      base::BindOnce(&U2fSignOperation::TrySign, weak_factory_.GetWeakPtr()));
}

void U2fSignOperation::TrySign() {
  DispatchU2FCommand(
      ConvertToU2fSignCommand(request(), app_param_type_, key_handle()),
      base::BindOnce(&U2fSignOperation::OnSignResponseReceived,
                     weak_factory_.GetWeakPtr()));
}

void U2fSignOperation::OnSignResponseReceived(
    std::optional<std::vector<uint8_t>> device_response) {
  if (canceled_) {
    return;
  }

  auto result = apdu::ApduResponse::Status::SW_WRONG_DATA;
  std::optional<apdu::ApduResponse> apdu_response;
  if (device_response) {
    FIDO_LOG(DEBUG) << "U2F device responded with "
                    << base::HexEncode(*device_response);
    apdu_response =
        apdu::ApduResponse::CreateFromMessage(std::move(*device_response));
    if (apdu_response) {
      result = apdu_response->status();
      FIDO_LOG(DEBUG) << "U2F result code: " << static_cast<uint16_t>(result);
    } else {
      FIDO_LOG(ERROR) << "U2F response is invalid";
    }
  } else {
    FIDO_LOG(ERROR) << "U2F device responded with empty response";
    if (base::FeatureList::IsEnabled(device::kWebAuthnRetryU2FErrors)) {
      failed_and_retried_ = true;
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&U2fSignOperation::WinkAndTrySign,
                         weak_factory_.GetWeakPtr()),
          kU2fRetryDelay);
      return;
    }
  }

  // Older U2F devices may respond with the length of the input as an error
  // response if the length is unexpected.
  if (result == static_cast<apdu::ApduResponse::Status>(key_handle().size())) {
    result = apdu::ApduResponse::Status::SW_WRONG_LENGTH;
    FIDO_LOG(ERROR) << "U2F device responded with length of input";
  }

  switch (result) {
    case apdu::ApduResponse::Status::SW_NO_ERROR: {
      auto application_parameter =
          app_param_type_ == ApplicationParameterType::kPrimary
              ? crypto::hash::Sha256(request().rp_id)
              : request().alternative_application_parameter.value_or(
                    std::array<uint8_t, kRpIdHashLength>());
      auto sign_response =
          AuthenticatorGetAssertionResponse::CreateFromU2fSignResponse(
              std::move(application_parameter), apdu_response->data(),
              key_handle(), device()->DeviceTransport());
      if (!sign_response) {
        FIDO_LOG(ERROR) << "Failed to generate valid U2F signature";
        LogU2fSignOperationResult(
            failed_and_retried_
                ? U2fSignOperationResult::kLowLevelErrorThenFatalError
                : U2fSignOperationResult::kFatalError);
        std::move(callback())
            .Run(CtapDeviceResponseCode::kCtap2ErrOther, std::nullopt);
        return;
      }

      FIDO_LOG(DEBUG)
          << "Received successful U2F sign response from authenticator: "
          << base::HexEncode(apdu_response->data());
      LogU2fSignOperationResult(
          failed_and_retried_
              ? U2fSignOperationResult::kLowLevelErrorThenSuccess
              : U2fSignOperationResult::kSuccess);
      std::move(callback())
          .Run(CtapDeviceResponseCode::kSuccess, std::move(sign_response));
      break;
    }

    case apdu::ApduResponse::Status::SW_WRONG_DATA:
    case apdu::ApduResponse::Status::SW_WRONG_LENGTH:
      if (app_param_type_ == ApplicationParameterType::kAlternative) {
        // |application_parameter_| failed, but there is also
        // the primary value to try.
        app_param_type_ = ApplicationParameterType::kPrimary;
        WinkAndTrySign();
      } else if (++current_key_handle_index_ < request().allow_list.size()) {
        // Key is not for this device. Try signing with the next key.
        if (request().alternative_application_parameter.has_value()) {
          app_param_type_ = ApplicationParameterType::kAlternative;
        }
        WinkAndTrySign();
      } else {
        // No provided key was accepted by this device. Send registration
        // (i.e. fake enroll) request to device.
        TryFakeEnrollment();
      }
      break;

    case apdu::ApduResponse::Status::SW_CONDITIONS_NOT_SATISFIED:
      // Waiting for user touch. Retry after 200 milliseconds delay.
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&U2fSignOperation::WinkAndTrySign,
                         weak_factory_.GetWeakPtr()),
          kU2fRetryDelay);
      break;

    default:
      // Some sort of failure occurred. Abandon this device and move on.
      FIDO_LOG(ERROR) << "U2F device has unknown failure. Dropping device.";
      LogU2fSignOperationResult(
          failed_and_retried_
              ? U2fSignOperationResult::kLowLevelErrorThenFatalError
              : U2fSignOperationResult::kFatalError);
      std::move(callback())
          .Run(CtapDeviceResponseCode::kCtap2ErrOther, std::nullopt);
      return;
  }
}

void U2fSignOperation::WinkAndTryFakeEnrollment() {
  device()->TryWink(base::BindOnce(&U2fSignOperation::TryFakeEnrollment,
                                   weak_factory_.GetWeakPtr()));
}

void U2fSignOperation::TryFakeEnrollment() {
  DispatchU2FCommand(
      ConstructBogusU2fRegistrationCommand(),
      base::BindOnce(&U2fSignOperation::OnEnrollmentResponseReceived,
                     weak_factory_.GetWeakPtr()));
}

void U2fSignOperation::OnEnrollmentResponseReceived(
    std::optional<std::vector<uint8_t>> device_response) {
  if (canceled_) {
    return;
  }

  auto result = apdu::ApduResponse::Status::SW_WRONG_DATA;
  if (device_response) {
    const auto apdu_response =
        apdu::ApduResponse::CreateFromMessage(std::move(*device_response));
    if (apdu_response) {
      result = apdu_response->status();
    }
  }

  switch (result) {
    case apdu::ApduResponse::Status::SW_NO_ERROR:
      LogU2fSignOperationResult(
          failed_and_retried_
              ? U2fSignOperationResult::kLowLevelErrorThenNoCredentials
              : U2fSignOperationResult::kNoCredentials);
      std::move(callback())
          .Run(CtapDeviceResponseCode::kCtap2ErrNoCredentials, std::nullopt);
      break;

    case apdu::ApduResponse::Status::SW_CONDITIONS_NOT_SATISFIED:
      // Waiting for user touch. Retry after 200 milliseconds delay.
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&U2fSignOperation::TryFakeEnrollment,
                         weak_factory_.GetWeakPtr()),
          kU2fRetryDelay);
      break;

    default:
      // Some sort of failure occurred. Abandon this device and move on.
      LogU2fSignOperationResult(
          failed_and_retried_
              ? U2fSignOperationResult::kLowLevelErrorThenFatalError
              : U2fSignOperationResult::kFatalError);
      std::move(callback())
          .Run(CtapDeviceResponseCode::kCtap2ErrOther, std::nullopt);
      return;
  }
}

const std::vector<uint8_t>& U2fSignOperation::key_handle() const {
  DCHECK_LT(current_key_handle_index_, request().allow_list.size());
  return request().allow_list.at(current_key_handle_index_).id;
}

}  // namespace device
