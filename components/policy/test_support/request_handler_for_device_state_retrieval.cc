// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/policy/test_support/request_handler_for_device_state_retrieval.h"

#include "base/strings/strcat.h"
#include "components/policy/core/common/cloud/cloud_policy_constants.h"
#include "components/policy/proto/device_management_backend.pb.h"
#include "components/policy/test_support/client_storage.h"
#include "components/policy/test_support/policy_storage.h"
#include "components/policy/test_support/test_server_helpers.h"
#include "net/base/url_util.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

using net::test_server::HttpRequest;
using net::test_server::HttpResponse;

namespace em = enterprise_management;

namespace policy {

RequestHandlerForDeviceStateRetrieval::RequestHandlerForDeviceStateRetrieval(
    EmbeddedPolicyTestServer* parent)
    : EmbeddedPolicyTestServer::RequestHandler(parent) {}

RequestHandlerForDeviceStateRetrieval::
    ~RequestHandlerForDeviceStateRetrieval() = default;

std::string RequestHandlerForDeviceStateRetrieval::RequestType() {
  return dm_protocol::kValueRequestDeviceStateRetrieval;
}

std::unique_ptr<HttpResponse>
RequestHandlerForDeviceStateRetrieval::HandleRequest(
    const HttpRequest& request) {
  em::DeviceManagementRequest device_management_request;
  device_management_request.ParseFromString(request.content);

  const em::DeviceStateRetrievalRequest& state_request =
      device_management_request.device_state_retrieval_request();

  // Look for a match for ZTE first.
  const PolicyStorage::InitialEnrollmentState* state =
      policy_storage()->GetInitialEnrollmentState(base::StrCat(
          {state_request.brand_code(), "_", state_request.serial_number()}));
  if (state && state->initial_enrollment_mode !=
                   em::DeviceInitialEnrollmentStateResponse::
                       INITIAL_ENROLLMENT_MODE_NONE) {
    em::DeviceManagementResponse device_management_response;
    em::DeviceInitialEnrollmentStateResponse* state_response =
        device_management_response.mutable_device_state_retrieval_response()
            ->mutable_initial_state_response();
    state_response->set_initial_enrollment_mode(state->initial_enrollment_mode);
    state_response->set_management_domain(state->management_domain);
    return CreateHttpResponse(net::HTTP_OK, device_management_response);
  }

  // Alternatively, look for a match for FRE.
  const std::string& server_backed_state_key =
      state_request.server_backed_state_key();
  em::DeviceManagementResponse device_management_response;
  if (client_storage()->LookupByStateKey(server_backed_state_key)) {
    em::DeviceStateRetrievalResponse* device_state_retrieval_response =
        device_management_response.mutable_device_state_retrieval_response();
    PolicyStorage::DeviceState device_state = policy_storage()->device_state();
    if (!device_state.management_domain.empty()) {
      device_state_retrieval_response->set_management_domain(
          device_state.management_domain);
    }
    device_state_retrieval_response->set_restore_mode(
        device_state.restore_mode);
  } else {
    em::DeviceStateRetrievalResponse* device_state_retrieval_response =
        device_management_response.mutable_device_state_retrieval_response();
    device_state_retrieval_response->set_restore_mode(
        em::DeviceStateRetrievalResponse::RESTORE_MODE_NONE);
  }
  return CreateHttpResponse(net::HTTP_OK, device_management_response);
}

}  // namespace policy
