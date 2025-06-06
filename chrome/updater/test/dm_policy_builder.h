// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_UPDATER_TEST_DM_POLICY_BUILDER_H_
#define CHROME_UPDATER_TEST_DM_POLICY_BUILDER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/containers/span.h"
#include "components/policy/proto/device_management_backend.pb.h"

namespace enterprise_management {
class DeviceManagementResponse;
class PolicyFetchResponse;
}  // namespace enterprise_management

namespace wireless_android_enterprise_devicemanagement {
class OmahaSettingsClientProto;
}  // namespace wireless_android_enterprise_devicemanagement

namespace updater::test {

// Manages DM response signing key.
class DMSigningKey {
 public:
  // `key_data` should be in DER-encoded PKCS8 format.
  // `key_signature` is SHA256 signature of `key_data` for `domain`.
  DMSigningKey(base::span<const uint8_t> key_data,
               base::span<const uint8_t> key_signature,
               int key_version,
               const std::string& domain);
  ~DMSigningKey();

  // Serialized public key part.
  std::string GetPublicKeyString() const;

  // The returned SHA256 signature includes the key and the domain.
  std::string GetPublicKeySignature() const { return key_signature_; }

  // Returns the domain that is signed as part of the signature. This is to
  // limit the key distribution to a certain organization.
  std::string GetPublicKeySignatureDomain() const {
    return key_signature_domain_;
  }

  bool has_key_version() const { return key_version_ >= 0; }
  int key_version() const { return key_version_; }

  // Signs `data` with the managed key into `signature`.
  void SignData(const std::string& data, std::string* signature) const;

 private:
  std::vector<uint8_t> key_data_;
  std::string key_signature_;
  int key_version_;
  std::string key_signature_domain_;
};

std::unique_ptr<DMSigningKey> GetTestKey1();
std::unique_ptr<DMSigningKey> GetTestKey2();

// Builds DM policy response.
class DMPolicyBuilder {
 public:
  enum class SigningOption {
    kSignNormally = 0,
    kTamperKeySignature = 1,
    kTamperDataSignature = 2,
  };

  DMPolicyBuilder(const std::string& dm_token,
                  const std::string& user_name,
                  const std::string& device_id,
                  std::unique_ptr<DMSigningKey> signing_key,
                  std::unique_ptr<DMSigningKey> new_signing_key,
                  SigningOption signing_option);
  ~DMPolicyBuilder();

  // Creates a default policy response builder with given options.
  // `first_request`: true if the response is for the first policy fetch
  // request.
  // `rotate_to_new_key`: true if the response should rotate to a new signing
  // key.
  static std::unique_ptr<DMPolicyBuilder> CreateInstanceWithOptions(
      bool first_request,
      bool rotate_to_new_key,
      SigningOption signing_option,
      const std::string& dm_token,
      const std::string& device_id);

  // Rotates signing key to the default new signing key.
  void SetNewSigningKeyToDefault();

  // Fills the PolicyFetchResponse with the given policy payload.
  void FillPolicyFetchResponseWithPayload(
      enterprise_management::PolicyFetchResponse* policy_response,
      const std::string& policy_type,
      const std::string& policy_payload,
      bool attach_new_public_key) const;

  // Returns serialized PolicyFetchResponse which contains the given
  // policy payload.
  std::string GetResponseBlobForPolicyPayload(
      const std::string& policy_type,
      const std::string& policy_payload) const;

  // Builds a DeviceManagementResponse with given policies.
  // `policies` is a map from policy type to policy payload string.
  std::unique_ptr<::enterprise_management::DeviceManagementResponse>
  BuildDMResponseForPolicies(
      const base::flat_map<std::string, std::string>& policies) const;

  // Builds a DeviceManagementResponse with the given error.
  std::unique_ptr<::enterprise_management::DeviceManagementResponse>
  BuildDMResponseWithError(
      ::enterprise_management::DeviceManagementErrorDetail error) const;

 private:
  const std::string dm_token_;
  const std::string user_name_;
  const std::string device_id_;
  SigningOption signing_option_;

  // Existing signing key. This value is empty when the device sends the first
  // policy fetch request.
  std::unique_ptr<DMSigningKey> signing_key_;

  // Optional next signing key. The value is set only when we are about to
  // rotate the signing key.
  std::unique_ptr<DMSigningKey> new_signing_key_;
};

// Gets the canned testing Omaha policy protobuf object.
std::unique_ptr<
    ::wireless_android_enterprise_devicemanagement::OmahaSettingsClientProto>
GetDefaultTestingOmahaPolicyProto();

// Creates a policy response for the given Omaha policies.
// `first_request`: true if the response is for the first policy fetch request.
// `rotate_to_new_key`: true if the response should rotate to a new signing key.
std::unique_ptr<::enterprise_management::DeviceManagementResponse>
GetDMResponseForOmahaPolicy(
    bool first_request,
    bool rotate_to_new_key,
    DMPolicyBuilder::SigningOption signing_option,
    const std::string& dm_token,
    const std::string& device_id,
    const ::wireless_android_enterprise_devicemanagement::
        OmahaSettingsClientProto& omaha_settings);

// Creates a policy response with default options.
// `first_request`: true if the response is for the first policy fetch request.
// `rotate_to_new_key`: true if the response should rotate to a new signing key.
std::unique_ptr<::enterprise_management::DeviceManagementResponse>
GetDefaultTestingPolicyFetchDMResponse(
    bool first_request,
    bool rotate_to_new_key,
    DMPolicyBuilder::SigningOption signing_option);

}  // namespace updater::test

#endif  // CHROME_UPDATER_TEST_DM_POLICY_BUILDER_H_
