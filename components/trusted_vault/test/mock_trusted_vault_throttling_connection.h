// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_TRUSTED_VAULT_TEST_MOCK_TRUSTED_VAULT_THROTTLING_CONNECTION_H_
#define COMPONENTS_TRUSTED_VAULT_TEST_MOCK_TRUSTED_VAULT_THROTTLING_CONNECTION_H_

#include <memory>

#include "base/functional/callback.h"
#include "components/signin/public/identity_manager/account_info.h"
#include "components/trusted_vault/securebox.h"
#include "components/trusted_vault/trusted_vault_throttling_connection.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace trusted_vault {

class MockTrustedVaultThrottlingConnection
    : public TrustedVaultThrottlingConnection {
 public:
  MockTrustedVaultThrottlingConnection();
  ~MockTrustedVaultThrottlingConnection() override;
  MOCK_METHOD(std::unique_ptr<Request>,
              RegisterAuthenticationFactor,
              (const CoreAccountInfo& account_info,
               const MemberKeysSource& member_keys_source,
               const SecureBoxPublicKey& authentication_factor_public_key,
               AuthenticationFactorTypeAndRegistrationParams
                   authentication_factor_type_and_registration_params,
               RegisterAuthenticationFactorCallback callback),
              (override));
  MOCK_METHOD(std::unique_ptr<Request>,
              RegisterLocalDeviceWithoutKeys,
              (const CoreAccountInfo& account_info,
               const SecureBoxPublicKey& device_public_key,
               RegisterAuthenticationFactorCallback callback),
              (override));
  MOCK_METHOD(
      std::unique_ptr<Request>,
      DownloadNewKeys,
      (const CoreAccountInfo& account_info,
       const TrustedVaultKeyAndVersion& last_trusted_vault_key_and_version,
       std::unique_ptr<SecureBoxKeyPair> device_key_pair,
       DownloadNewKeysCallback callback),
      (override));
  MOCK_METHOD(std::unique_ptr<Request>,
              DownloadIsRecoverabilityDegraded,
              (const CoreAccountInfo& account_info,
               IsRecoverabilityDegradedCallback callback),
              (override));
  MOCK_METHOD(std::unique_ptr<Request>,
              DownloadAuthenticationFactorsRegistrationState,
              (const CoreAccountInfo& account_info,
               DownloadAuthenticationFactorsRegistrationStateCallback callback,
               base::RepeatingClosure keep_alive_callback),
              (override));
  MOCK_METHOD(std::unique_ptr<Request>,
              DownloadAuthenticationFactorsRegistrationState,
              (const CoreAccountInfo& account_info,
               std::set<trusted_vault_pb::SecurityDomainMember_MemberType>
                   recovery_factor_filter,
               DownloadAuthenticationFactorsRegistrationStateCallback callback,
               base::RepeatingClosure keep_alive_callback),
              (override));
  MOCK_METHOD(bool,
              AreRequestsThrottled,
              (const CoreAccountInfo& account_info),
              (override));
  MOCK_METHOD(void,
              RecordFailedRequestForThrottling,
              (const CoreAccountInfo& account_info),
              (override));
};

}  // namespace trusted_vault

#endif  // COMPONENTS_TRUSTED_VAULT_TEST_MOCK_TRUSTED_VAULT_THROTTLING_CONNECTION_H_
