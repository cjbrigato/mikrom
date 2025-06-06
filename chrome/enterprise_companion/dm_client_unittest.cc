// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/enterprise_companion/dm_client.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/barrier_closure.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/mock_callback.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chrome/enterprise_companion/constants.h"
#include "chrome/enterprise_companion/device_management_storage/dm_storage.h"
#include "chrome/enterprise_companion/enterprise_companion_status.h"
#include "chrome/enterprise_companion/event_logger.h"
#include "chrome/enterprise_companion/proto/enterprise_companion_event.pb.h"
#include "components/policy/core/common/cloud/cloud_policy_client.h"
#include "components/policy/core/common/cloud/cloud_policy_constants.h"
#include "components/policy/core/common/cloud/cloud_policy_validator.h"
#include "components/policy/core/common/cloud/device_management_service.h"
#include "components/policy/core/common/cloud/mock_cloud_policy_client.h"
#include "components/policy/core/common/cloud/mock_device_management_service.h"
#include "components/policy/core/common/policy_types.h"
#include "components/policy/proto/device_management_backend.pb.h"
#include "net/http/http_status_code.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace enterprise_companion {

namespace {
constexpr char kFakeDeviceId[] = "test-device-id";
constexpr char kFakeEnrollmentToken[] = "TestEnrollmentToken";
constexpr char kFakeDMToken[] = "test-dm-token";
constexpr char kPolicyType1[] = "google/test-policy-type-1";
constexpr char kPolicyValue1[] = "test policy value 1";
constexpr char kPolicyType2[] = "google/test-policy-type-2";
constexpr char kPolicyValue2[] = "test policy value 2";
constexpr char kPublicKey1[] = "new public key 1";
constexpr char kPolicyValue3[] = "test policy value 3";
constexpr int kPublicKey1Version = 100;
constexpr int kTimestamp1 = 42;
constexpr int kTimestamp2 = 84;

using ::policy::PolicyFetchReason;
using ::testing::_;
using ::testing::ElementsAre;

// Wraps a real DM storage instance allowing behavior to be augmented by tests.
class TestDMStorage final : public device_management_storage::DMStorage {
 public:
  TestDMStorage(
      const base::FilePath& policy_cache_root,
      std::unique_ptr<device_management_storage::TokenServiceInterface>
          token_service)
      : storage_(device_management_storage::CreateDMStorage(
            policy_cache_root,
            std::move(token_service))) {}

  void SetCanPersistPolicies(bool can_persist_policies) {
    can_persist_policies_ = can_persist_policies;
  }

  void SetWillPersistPolicies(bool will_persist_policies) {
    will_persist_policies_ = will_persist_policies;
  }

  // Overrides for DMStorage.
  bool CanPersistPolicies() const override { return can_persist_policies_; }

  bool PersistPolicies(
      const device_management_storage::DMPolicyMap& policy_map) const override {
    return will_persist_policies_ ? storage_->PersistPolicies(policy_map)
                                  : false;
  }

  std::string GetDeviceID() const override { return storage_->GetDeviceID(); }

  bool IsEnrollmentMandatory() const override {
    return storage_->IsEnrollmentMandatory();
  }

  bool StoreEnrollmentToken(const std::string& enrollment_token) override {
    return storage_->StoreEnrollmentToken(enrollment_token);
  }

  bool DeleteEnrollmentToken() override {
    return storage_->DeleteEnrollmentToken();
  }

  std::string GetEnrollmentToken() const override {
    return storage_->GetEnrollmentToken();
  }

  bool StoreDmToken(const std::string& dm_token) override {
    return storage_->StoreDmToken(dm_token);
  }

  std::string GetDmToken() const override { return storage_->GetDmToken(); }

  bool InvalidateDMToken() override { return storage_->InvalidateDMToken(); }

  bool DeleteDMToken() override { return storage_->DeleteDMToken(); }

  bool IsValidDMToken() const override { return storage_->IsValidDMToken(); }

  bool IsDeviceDeregistered() const override {
    return storage_->IsDeviceDeregistered();
  }

  bool RemoveAllPolicies() const override {
    return storage_->RemoveAllPolicies();
  }

  std::unique_ptr<device_management_storage::CachedPolicyInfo>
  GetCachedPolicyInfo() const override {
    return storage_->GetCachedPolicyInfo();
  }

  std::optional<enterprise_management::PolicyData> ReadPolicyData(
      const std::string& policy_type) override {
    return storage_->ReadPolicyData(policy_type);
  }

  base::FilePath policy_cache_folder() const override {
    return storage_->policy_cache_folder();
  }

 private:
  ~TestDMStorage() override = default;

  scoped_refptr<device_management_storage::DMStorage> storage_;
  bool can_persist_policies_ = true;
  bool will_persist_policies_ = true;
};

class MockCloudPolicyClient : public policy::MockCloudPolicyClient {
 public:
  explicit MockCloudPolicyClient(policy::DeviceManagementService* service)
      : policy::MockCloudPolicyClient(service) {}

  MOCK_METHOD(void,
              RegisterPolicyAgentWithEnrollmentToken,
              (const std::string& token,
               const std::string& client_id,
               const policy::ClientDataDelegate& client_data_delegate),
              (override));
};

class TestTokenService
    : public device_management_storage::TokenServiceInterface {
 public:
  // Overrides for TokenServiceInterface.
  std::string GetDeviceID() const override { return kFakeDeviceId; }

  bool IsEnrollmentMandatory() const override { return false; }

  bool StoreEnrollmentToken(const std::string& enrollment_token) override {
    enrollment_token_ = enrollment_token;
    return true;
  }

  bool DeleteEnrollmentToken() override { return StoreEnrollmentToken(""); }

  std::string GetEnrollmentToken() const override { return enrollment_token_; }

  bool StoreDmToken(const std::string& dm_token) override {
    dm_token_ = dm_token;
    return true;
  }

  bool DeleteDmToken() override {
    dm_token_.clear();
    return true;
  }

  std::string GetDmToken() const override { return dm_token_; }

 private:
  std::string enrollment_token_;
  std::string dm_token_;
};

class TestEventLogger : public EnterpriseCompanionEventLogger {
 public:
  const std::vector<EnterpriseCompanionStatus>& registration_events() {
    return registration_events_;
  }
  const std::vector<EnterpriseCompanionStatus>& policy_fetch_events() {
    return policy_fetch_events_;
  }

  void LogRegisterPolicyAgentEvent(
      base::Time start_time,
      StatusCallback callback,
      const EnterpriseCompanionStatus& status) override {
    registration_events_.push_back(status);
    std::move(callback).Run(status);
  }

  void LogPolicyFetchEvent(base::Time start_time,
                           StatusCallback callback,
                           const EnterpriseCompanionStatus& status) override {
    policy_fetch_events_.push_back(status);
    std::move(callback).Run(status);
  }

  void Flush(base::OnceClosure callback) override { std::move(callback).Run(); }

 private:
  ~TestEventLogger() override = default;

  std::vector<EnterpriseCompanionStatus> registration_events_;
  std::vector<EnterpriseCompanionStatus> policy_fetch_events_;
};

}  // namespace

class DMClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(storage_dir_.CreateUniqueTempDir());

    std::unique_ptr<TestTokenService> test_token_service =
        std::make_unique<TestTokenService>();
    test_token_service_ = test_token_service.get();
    mock_cloud_policy_client_ =
        new MockCloudPolicyClient(&fake_device_management_service_);
    dm_storage_ = base::MakeRefCounted<TestDMStorage>(
        storage_dir_.GetPath(), std::move(test_token_service));
    dm_client_ = CreateDMClient(
        base::BindLambdaForTesting([&](policy::DeviceManagementService*) {
          return base::WrapUnique(static_cast<policy::CloudPolicyClient*>(
              mock_cloud_policy_client_));
        }),
        dm_storage_, mock_policy_fetch_response_validator_.Get(),
        CreateDeviceManagementServiceConfig(),
        /*task_timeout=*/base::Milliseconds(250));
  }

  base::test::TaskEnvironment environment_;
  base::ScopedTempDir storage_dir_;
  policy::MockJobCreationHandler mock_job_creation_handler_;
  policy::FakeDeviceManagementService fake_device_management_service_ =
      policy::FakeDeviceManagementService(&mock_job_creation_handler_);
  scoped_refptr<TestDMStorage> dm_storage_;
  // |test_token_service_| and |mock_cloud_policy_client_| are pointers to
  // objects owned by |dm_client_|. They must be destructed before the client to
  // avoid raw_ptr from complaining about dangling pointers.
  std::unique_ptr<DMClient> dm_client_;
  raw_ptr<TestTokenService> test_token_service_ = nullptr;
  raw_ptr<MockCloudPolicyClient> mock_cloud_policy_client_ = nullptr;
  base::MockCallback<PolicyFetchResponseValidator>
      mock_policy_fetch_response_validator_;
  scoped_refptr<TestEventLogger> test_event_logger_ =
      base::MakeRefCounted<TestEventLogger>();

  // Ensure a valid registration state by storing an enrollment token, fake
  // DM token, and configuring the cloud policy client.
  void EnsureRegistered() {
    test_token_service_->StoreEnrollmentToken(kFakeEnrollmentToken);
    test_token_service_->StoreDmToken(kFakeDMToken);
    mock_cloud_policy_client_->dm_token_ = kFakeDMToken;
    ASSERT_TRUE(mock_cloud_policy_client_->is_registered());
  }

  void SetMockPolicyFetchResponseValidatorResult(
      policy::CloudPolicyValidatorBase::Status status) {
    EXPECT_CALL(mock_policy_fetch_response_validator_, Run)
        .WillRepeatedly(
            [status](const std::string&, const std::string&, const std::string&,
                     int64_t,
                     const enterprise_management::PolicyFetchResponse&) {
              auto result = std::make_unique<
                  policy::CloudPolicyValidatorBase::ValidationResult>();
              result->status = status;
              return result;
            });
  }
};

TEST_F(DMClientTest, RegisterDeviceSuccess) {
  test_token_service_->StoreEnrollmentToken(kFakeEnrollmentToken);
  EXPECT_CALL(*mock_cloud_policy_client_,
              RegisterPolicyAgentWithEnrollmentToken(kFakeEnrollmentToken,
                                                     kFakeDeviceId, _))
      .WillOnce([&] {
        mock_cloud_policy_client_->SetDMToken(kFakeDMToken);
        mock_cloud_policy_client_->NotifyRegistrationStateChanged();
      });

  base::RunLoop run_loop;
  dm_client_->RegisterPolicyAgent(
      test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.ok());
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();

  EXPECT_EQ(test_token_service_->GetDmToken(), kFakeDMToken);
  EXPECT_THAT(test_event_logger_->registration_events(),
              ElementsAre(EnterpriseCompanionStatus::Success()));
  EXPECT_TRUE(test_event_logger_->policy_fetch_events().empty());
}

TEST_F(DMClientTest, RegisterDeviceFailure) {
  test_token_service_->StoreEnrollmentToken(kFakeEnrollmentToken);
  EXPECT_CALL(*mock_cloud_policy_client_,
              RegisterPolicyAgentWithEnrollmentToken(kFakeEnrollmentToken,
                                                     kFakeDeviceId, _))
      .WillOnce([&] {
        mock_cloud_policy_client_->SetStatus(
            policy::DM_STATUS_SERVICE_INVALID_SERIAL_NUMBER);
        mock_cloud_policy_client_->NotifyClientError();
      });

  base::RunLoop run_loop;
  dm_client_->RegisterPolicyAgent(
      test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.EqualsDeviceManagementStatus(
            policy::DM_STATUS_SERVICE_INVALID_SERIAL_NUMBER));
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();

  EXPECT_TRUE(test_token_service_->GetDmToken().empty());
  EXPECT_THAT(test_event_logger_->registration_events(),
              ElementsAre(EnterpriseCompanionStatus::FromDeviceManagementStatus(
                  policy::DM_STATUS_SERVICE_INVALID_SERIAL_NUMBER)));
  EXPECT_TRUE(test_event_logger_->policy_fetch_events().empty());
}

TEST_F(DMClientTest, RegistrationRemovesPolicies) {
  // Store some policies, a DM token must be preset to serialize the data.
  test_token_service_->StoreDmToken(kFakeDMToken);
  ::enterprise_management::PolicyFetchResponse fake_response;
  ::enterprise_management::PolicyData fake_policy_data;
  fake_policy_data.set_policy_value(kPolicyValue1);
  fake_response.set_policy_data(fake_policy_data.SerializeAsString());
  ASSERT_TRUE(dm_storage_->CanPersistPolicies());
  ASSERT_TRUE(dm_storage_->PersistPolicies(
      {{kPolicyType1, fake_response.SerializeAsString()}}));
  ASSERT_TRUE(dm_storage_->ReadPolicyData(kPolicyType1));

  // Delete the DM token to ensure registration is not skipped.
  test_token_service_->DeleteDmToken();
  test_token_service_->StoreEnrollmentToken(kFakeEnrollmentToken);
  EXPECT_CALL(*mock_cloud_policy_client_,
              RegisterPolicyAgentWithEnrollmentToken(kFakeEnrollmentToken,
                                                     kFakeDeviceId, _))
      .WillOnce([&] {
        mock_cloud_policy_client_->SetDMToken(kFakeDMToken);
        mock_cloud_policy_client_->NotifyRegistrationStateChanged();
      });

  // Register the device. All policies should be removed as a side effect.
  base::RunLoop run_loop;
  dm_client_->RegisterPolicyAgent(
      test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.ok());
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();

  EXPECT_FALSE(dm_storage_->ReadPolicyData(kPolicyType1));
  EXPECT_THAT(test_event_logger_->registration_events(),
              ElementsAre(EnterpriseCompanionStatus::Success()));
  EXPECT_TRUE(test_event_logger_->policy_fetch_events().empty());
}

TEST_F(DMClientTest, RegistrationSkippedNoEnrollmentToken) {
  EXPECT_CALL(*mock_cloud_policy_client_,
              RegisterPolicyAgentWithEnrollmentToken)
      .Times(0);

  base::RunLoop run_loop;
  dm_client_->RegisterPolicyAgent(
      test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.ok());
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();

  EXPECT_TRUE(test_token_service_->GetDmToken().empty());
}

TEST_F(DMClientTest, RegistrationSkippedAlreadyManaged) {
  EnsureRegistered();
  EXPECT_CALL(*mock_cloud_policy_client_,
              RegisterPolicyAgentWithEnrollmentToken)
      .Times(0);

  base::RunLoop run_loop;
  dm_client_->RegisterPolicyAgent(
      test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.ok());
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();

  EXPECT_EQ(test_token_service_->GetDmToken(), kFakeDMToken);
  // Skipping registration should result in no logged events.
  EXPECT_TRUE(test_event_logger_->registration_events().empty());
  EXPECT_TRUE(test_event_logger_->policy_fetch_events().empty());
}

// If a malformed DM token has been persisted to DM storage, DM client should
// ignore it and attempt to register using the enrollment token.
TEST_F(DMClientTest, MalformedDMTokenCanReregister) {
  test_token_service_->StoreEnrollmentToken(kFakeEnrollmentToken);
  // Tokens which aren't serializable as HTTP header values are invalid.
  test_token_service_->StoreDmToken("malformed\r\n\0token");
  EXPECT_CALL(*mock_cloud_policy_client_,
              RegisterPolicyAgentWithEnrollmentToken(kFakeEnrollmentToken,
                                                     kFakeDeviceId, _))
      .WillOnce([&] {
        mock_cloud_policy_client_->SetDMToken(kFakeDMToken);
        mock_cloud_policy_client_->NotifyRegistrationStateChanged();
      });

  base::RunLoop run_loop;
  dm_client_->RegisterPolicyAgent(
      test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.ok());
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();

  EXPECT_EQ(test_token_service_->GetDmToken(), kFakeDMToken);
  EXPECT_THAT(test_event_logger_->registration_events(),
              ElementsAre(EnterpriseCompanionStatus::Success()));
  EXPECT_TRUE(test_event_logger_->policy_fetch_events().empty());
}

TEST_F(DMClientTest, RegisterDeviceMalformedEnrollmentToken) {
  // Tokens which aren't serializable as HTTP header values are invalid.
  test_token_service_->StoreEnrollmentToken("malformed\r\n\0token");

  base::RunLoop run_loop;
  dm_client_->RegisterPolicyAgent(
      test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.EqualsApplicationError(
            ApplicationError::kInvalidEnrollmentToken));
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();

  EXPECT_TRUE(test_token_service_->GetDmToken().empty());
  EXPECT_THAT(test_event_logger_->registration_events(),
              ElementsAre(EnterpriseCompanionStatus(
                  ApplicationError::kInvalidEnrollmentToken)));
  EXPECT_TRUE(test_event_logger_->policy_fetch_events().empty());
}

TEST_F(DMClientTest, PoliciesPersistedThroughSkippedRegistration) {
  EnsureRegistered();

  ::enterprise_management::PolicyFetchResponse fake_response;
  ::enterprise_management::PolicyData fake_policy_data;
  fake_policy_data.set_policy_value(kPolicyValue1);
  fake_response.set_policy_data(fake_policy_data.SerializeAsString());
  ASSERT_TRUE(dm_storage_->CanPersistPolicies());
  ASSERT_TRUE(dm_storage_->PersistPolicies(
      {{kPolicyType1, fake_response.SerializeAsString()}}));
  ASSERT_TRUE(dm_storage_->ReadPolicyData(kPolicyType1));

  // Delete the DM token to ensure registration is not skipped.
  EXPECT_CALL(*mock_cloud_policy_client_,
              RegisterPolicyAgentWithEnrollmentToken)
      .Times(0);

  // Registration should be skipped as DM token is still present.
  base::RunLoop run_loop;
  dm_client_->RegisterPolicyAgent(
      test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.ok());
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();

  EXPECT_EQ(test_token_service_->GetDmToken(), kFakeDMToken);
  EXPECT_TRUE(dm_storage_->ReadPolicyData(kPolicyType1));
  // Skipping registration should result in no logged events.
  EXPECT_TRUE(test_event_logger_->registration_events().empty());
  EXPECT_TRUE(test_event_logger_->policy_fetch_events().empty());
}

TEST_F(DMClientTest, FetchPoliciesFailsIfNotRegistered) {
  base::RunLoop run_loop;
  dm_client_->FetchPolicies(
      PolicyFetchReason::kTest, test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.EqualsApplicationError(
            ApplicationError::kRegistrationPreconditionFailed));
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();

  std::unique_ptr<device_management_storage::CachedPolicyInfo>
      cached_policy_info = dm_storage_->GetCachedPolicyInfo();
  EXPECT_TRUE(cached_policy_info->public_key().empty());
  EXPECT_FALSE(cached_policy_info->has_key_version());
  EXPECT_EQ(cached_policy_info->timestamp(), 0);
  EXPECT_TRUE(test_event_logger_->registration_events().empty());
  EXPECT_THAT(test_event_logger_->policy_fetch_events(),
              ElementsAre(EnterpriseCompanionStatus(
                  ApplicationError::kRegistrationPreconditionFailed)));
}

TEST_F(DMClientTest, FetchPoliciesFailsIfDMStorageCannotPersist) {
  EnsureRegistered();
  dm_storage_->SetCanPersistPolicies(false);

  base::RunLoop run_loop;
  dm_client_->FetchPolicies(
      PolicyFetchReason::kTest, test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.EqualsApplicationError(
            ApplicationError::kPolicyPersistenceImpossible));
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();

  std::unique_ptr<device_management_storage::CachedPolicyInfo>
      cached_policy_info = dm_storage_->GetCachedPolicyInfo();
  EXPECT_TRUE(cached_policy_info->public_key().empty());
  EXPECT_FALSE(cached_policy_info->has_key_version());
  EXPECT_EQ(cached_policy_info->timestamp(), 0);
  EXPECT_TRUE(test_event_logger_->registration_events().empty());
  EXPECT_THAT(test_event_logger_->policy_fetch_events(),
              ElementsAre(EnterpriseCompanionStatus(
                  ApplicationError::kPolicyPersistenceImpossible)));
}

TEST_F(DMClientTest, FetchPoliciesFailsIfCloudPolicyClientFails) {
  EnsureRegistered();

  EXPECT_CALL(*mock_cloud_policy_client_, FetchPolicy).WillOnce([&] {
    mock_cloud_policy_client_->SetStatus(
        policy::DM_STATUS_SERVICE_MANAGEMENT_TOKEN_INVALID);
    mock_cloud_policy_client_->NotifyPolicyFetched();
  });

  base::RunLoop run_loop;
  dm_client_->FetchPolicies(
      PolicyFetchReason::kTest, test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.EqualsDeviceManagementStatus(
            policy::DM_STATUS_SERVICE_MANAGEMENT_TOKEN_INVALID));
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();

  std::unique_ptr<device_management_storage::CachedPolicyInfo>
      cached_policy_info = dm_storage_->GetCachedPolicyInfo();
  EXPECT_TRUE(cached_policy_info->public_key().empty());
  EXPECT_FALSE(cached_policy_info->has_key_version());
  EXPECT_EQ(cached_policy_info->timestamp(), 0);
  EXPECT_TRUE(test_event_logger_->registration_events().empty());
  EXPECT_THAT(test_event_logger_->policy_fetch_events(),
              ElementsAre(EnterpriseCompanionStatus::FromDeviceManagementStatus(
                  policy::DM_STATUS_SERVICE_MANAGEMENT_TOKEN_INVALID)));
}

TEST_F(DMClientTest, FetchPoliciesFailsIfFetchResultInvalid) {
  EnsureRegistered();

  EXPECT_CALL(*mock_cloud_policy_client_, FetchPolicy).WillOnce([&] {
    mock_cloud_policy_client_->SetPolicy(
        kPolicyType1, /*settings_entity_id=*/"",
        enterprise_management::PolicyFetchResponse());
    mock_cloud_policy_client_->NotifyPolicyFetched();
  });
  EXPECT_CALL(*mock_cloud_policy_client_,
              UploadPolicyValidationReport(_, _, _, _, _, _))
      .WillOnce([&](policy::CloudPolicyValidatorBase::Status,
                    const std::vector<policy::ValueValidationIssue>&,
                    policy::ValidationAction, const std::string&,
                    const std::string&,
                    policy::CloudPolicyClient::ResultCallback callback) {
        std::move(callback).Run(policy::CloudPolicyClient::Result(
            policy::DeviceManagementStatus::DM_STATUS_SUCCESS));
      });
  SetMockPolicyFetchResponseValidatorResult(
      policy::CloudPolicyValidatorBase::VALIDATION_POLICY_PARSE_ERROR);

  base::RunLoop run_loop;
  dm_client_->FetchPolicies(
      PolicyFetchReason::kTest, test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.EqualsCloudPolicyValidationResult(
            policy::CloudPolicyValidatorBase::VALIDATION_POLICY_PARSE_ERROR));
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();

  std::unique_ptr<device_management_storage::CachedPolicyInfo>
      cached_policy_info = dm_storage_->GetCachedPolicyInfo();
  EXPECT_TRUE(cached_policy_info->public_key().empty());
  EXPECT_FALSE(cached_policy_info->has_key_version());
  EXPECT_EQ(cached_policy_info->timestamp(), 0);
  EXPECT_TRUE(test_event_logger_->registration_events().empty());
  EXPECT_THAT(
      test_event_logger_->policy_fetch_events(),
      ElementsAre(EnterpriseCompanionStatus::FromCloudPolicyValidationResult(
          policy::CloudPolicyValidatorBase::VALIDATION_POLICY_PARSE_ERROR)));
}

// If the fetched policies fail validation, the policy cache should be cleared.
TEST_F(DMClientTest, PolicyCacheClearedIfFetchResultInvalid) {
  // Store some policies, a DM token must be preset to serialize the data.
  EnsureRegistered();
  ::enterprise_management::PolicyFetchResponse fake_response;
  ::enterprise_management::PolicyData fake_policy_data;
  fake_policy_data.set_policy_value(kPolicyValue1);
  fake_response.set_policy_data(fake_policy_data.SerializeAsString());
  ASSERT_TRUE(dm_storage_->CanPersistPolicies());
  ASSERT_TRUE(dm_storage_->PersistPolicies(
      {{kPolicyType1, fake_response.SerializeAsString()}}));
  ASSERT_TRUE(dm_storage_->ReadPolicyData(kPolicyType1));

  EXPECT_CALL(*mock_cloud_policy_client_, FetchPolicy).WillOnce([&] {
    mock_cloud_policy_client_->SetPolicy(
        kPolicyType1, /*settings_entity_id=*/"",
        enterprise_management::PolicyFetchResponse());
    mock_cloud_policy_client_->NotifyPolicyFetched();
  });
  EXPECT_CALL(*mock_cloud_policy_client_,
              UploadPolicyValidationReport(_, _, _, _, _, _))
      .WillOnce([&](policy::CloudPolicyValidatorBase::Status,
                    const std::vector<policy::ValueValidationIssue>&,
                    policy::ValidationAction, const std::string&,
                    const std::string&,
                    policy::CloudPolicyClient::ResultCallback callback) {
        std::move(callback).Run(policy::CloudPolicyClient::Result(
            policy::DeviceManagementStatus::DM_STATUS_SUCCESS));
      });
  SetMockPolicyFetchResponseValidatorResult(
      policy::CloudPolicyValidatorBase::VALIDATION_BAD_SIGNATURE);

  base::RunLoop run_loop;
  dm_client_->FetchPolicies(
      PolicyFetchReason::kTest, test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.EqualsCloudPolicyValidationResult(
            policy::CloudPolicyValidatorBase::VALIDATION_BAD_SIGNATURE));
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();

  EXPECT_FALSE(dm_storage_->ReadPolicyData(kPolicyType1));
}

TEST_F(DMClientTest, FetchPoliciesFailsIfResultCannotBePersisted) {
  EnsureRegistered();
  dm_storage_->SetWillPersistPolicies(false);

  EXPECT_CALL(*mock_cloud_policy_client_, FetchPolicy).WillOnce([&] {
    mock_cloud_policy_client_->SetPolicy(
        kPolicyType1, /*settings_entity_id=*/"",
        enterprise_management::PolicyFetchResponse());
    mock_cloud_policy_client_->NotifyPolicyFetched();
  });
  SetMockPolicyFetchResponseValidatorResult(
      policy::CloudPolicyValidatorBase::VALIDATION_OK);

  base::RunLoop run_loop;
  dm_client_->FetchPolicies(
      PolicyFetchReason::kTest, test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.EqualsApplicationError(
            ApplicationError::kPolicyPersistenceFailed));
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();

  std::unique_ptr<device_management_storage::CachedPolicyInfo>
      cached_policy_info = dm_storage_->GetCachedPolicyInfo();
  EXPECT_TRUE(cached_policy_info->public_key().empty());
  EXPECT_FALSE(cached_policy_info->has_key_version());
  EXPECT_EQ(cached_policy_info->timestamp(), 0);
  EXPECT_TRUE(test_event_logger_->registration_events().empty());
  EXPECT_THAT(test_event_logger_->policy_fetch_events(),
              ElementsAre(EnterpriseCompanionStatus(
                  ApplicationError::kPolicyPersistenceFailed)));
}

TEST_F(DMClientTest, FetchPoliciesSuccess) {
  EnsureRegistered();

  enterprise_management::PublicKeyVerificationData key_verification_data;
  key_verification_data.set_new_public_key(kPublicKey1);
  key_verification_data.set_new_public_key_version(kPublicKey1Version);
  enterprise_management::PolicyData data1;
  data1.set_timestamp(kTimestamp1);
  data1.set_policy_type(kPolicyType1);
  data1.set_policy_value(kPolicyValue1);
  enterprise_management::PolicyFetchResponse response1;
  response1.set_policy_data(data1.SerializeAsString());
  response1.set_new_public_key_verification_data(
      key_verification_data.SerializeAsString());

  enterprise_management::PolicyData data2;
  data2.set_timestamp(kTimestamp1);
  data2.set_policy_type(kPolicyType2);
  data2.set_policy_value(kPolicyValue2);
  enterprise_management::PolicyFetchResponse response2;
  response2.set_policy_data(data2.SerializeAsString());

  EXPECT_CALL(*mock_cloud_policy_client_, FetchPolicy).WillOnce([&] {
    mock_cloud_policy_client_->SetPolicy(kPolicyType1, "", response1);
    mock_cloud_policy_client_->SetPolicy(kPolicyType2, "", response2);
    mock_cloud_policy_client_->NotifyPolicyFetched();
  });
  SetMockPolicyFetchResponseValidatorResult(
      policy::CloudPolicyValidatorBase::VALIDATION_OK);

  base::RunLoop run_loop;
  dm_client_->FetchPolicies(
      PolicyFetchReason::kTest, test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.ok());
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();

  std::unique_ptr<device_management_storage::CachedPolicyInfo>
      cached_policy_info = dm_storage_->GetCachedPolicyInfo();
  EXPECT_EQ(cached_policy_info->public_key(), kPublicKey1);
  EXPECT_EQ(cached_policy_info->key_version(), kPublicKey1Version);
  EXPECT_EQ(cached_policy_info->timestamp(), kTimestamp1);

  std::optional<enterprise_management::PolicyData> read_data1 =
      dm_storage_->ReadPolicyData(kPolicyType1);
  ASSERT_TRUE(read_data1);
  EXPECT_EQ(read_data1->SerializeAsString(), data1.SerializeAsString());

  std::optional<enterprise_management::PolicyData> read_data2 =
      dm_storage_->ReadPolicyData(kPolicyType2);
  ASSERT_TRUE(read_data2);
  EXPECT_EQ(read_data2->SerializeAsString(), data2.SerializeAsString());

  EXPECT_TRUE(test_event_logger_->registration_events().empty());
  EXPECT_THAT(test_event_logger_->policy_fetch_events(),
              ElementsAre(EnterpriseCompanionStatus::Success()));
}

TEST_F(DMClientTest, FetchPoliciesOverwrite) {
  EnsureRegistered();

  // The responses from the first policy fetch will populate cached info.
  enterprise_management::PublicKeyVerificationData key_verification_data;
  key_verification_data.set_new_public_key(kPublicKey1);
  key_verification_data.set_new_public_key_version(kPublicKey1Version);
  enterprise_management::PolicyData data1;
  data1.set_timestamp(kTimestamp1);
  data1.set_policy_type(kPolicyType1);
  data1.set_policy_value(kPolicyValue1);
  enterprise_management::PolicyFetchResponse response1;
  response1.set_policy_data(data1.SerializeAsString());
  response1.set_new_public_key_verification_data(
      key_verification_data.SerializeAsString());

  enterprise_management::PolicyData data2;
  data2.set_timestamp(kTimestamp1);
  data2.set_policy_type(kPolicyType2);
  data2.set_policy_value(kPolicyValue2);
  enterprise_management::PolicyFetchResponse response2;
  response2.set_policy_data(data2.SerializeAsString());

  // The response from a subsequence policy fetch whose response does not
  // contain a new public key. The cached information should not change.
  enterprise_management::PolicyData data3;
  data3.set_timestamp(kTimestamp2);
  data3.set_policy_type(kPolicyType1);
  data3.set_policy_value(kPolicyValue3);
  enterprise_management::PolicyFetchResponse response3;
  response3.set_policy_data(data3.SerializeAsString());

  SetMockPolicyFetchResponseValidatorResult(
      policy::CloudPolicyValidatorBase::VALIDATION_OK);

  testing::InSequence expect_calls_in_sequence;
  EXPECT_CALL(*mock_cloud_policy_client_, FetchPolicy).WillOnce([&] {
    mock_cloud_policy_client_->SetPolicy(kPolicyType1, "", response1);
    mock_cloud_policy_client_->SetPolicy(kPolicyType2, "", response2);
    mock_cloud_policy_client_->NotifyPolicyFetched();
  });
  EXPECT_CALL(*mock_cloud_policy_client_, FetchPolicy).WillOnce([&] {
    mock_cloud_policy_client_->SetPolicy(kPolicyType1, "", response3);
    mock_cloud_policy_client_->NotifyPolicyFetched();
  });

  base::RunLoop first_fetch_loop;
  dm_client_->FetchPolicies(
      PolicyFetchReason::kTest, test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.ok());
        test_event_logger_->Flush(first_fetch_loop.QuitClosure());
      }));
  first_fetch_loop.Run();

  std::unique_ptr<device_management_storage::CachedPolicyInfo>
      cached_policy_info = dm_storage_->GetCachedPolicyInfo();
  EXPECT_EQ(cached_policy_info->public_key(), kPublicKey1);
  EXPECT_EQ(cached_policy_info->key_version(), kPublicKey1Version);
  EXPECT_EQ(cached_policy_info->timestamp(), kTimestamp1);

  base::RunLoop second_fetch_loop;
  dm_client_->FetchPolicies(
      PolicyFetchReason::kTest, test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.ok());
        test_event_logger_->Flush(second_fetch_loop.QuitClosure());
      }));
  second_fetch_loop.Run();

  cached_policy_info = dm_storage_->GetCachedPolicyInfo();
  EXPECT_EQ(cached_policy_info->public_key(), kPublicKey1);
  EXPECT_EQ(cached_policy_info->key_version(), kPublicKey1Version);
  EXPECT_EQ(cached_policy_info->timestamp(), kTimestamp1);

  std::optional<enterprise_management::PolicyData> read_data1 =
      dm_storage_->ReadPolicyData(kPolicyType1);
  ASSERT_TRUE(read_data1);
  EXPECT_EQ(read_data1->SerializeAsString(), data3.SerializeAsString());

  std::optional<enterprise_management::PolicyData> read_data2 =
      dm_storage_->ReadPolicyData(kPolicyType2);
  ASSERT_TRUE(read_data2);
  EXPECT_EQ(read_data2->SerializeAsString(), data2.SerializeAsString());

  EXPECT_TRUE(test_event_logger_->registration_events().empty());
  EXPECT_THAT(test_event_logger_->policy_fetch_events(),
              ElementsAre(EnterpriseCompanionStatus::Success(),
                          EnterpriseCompanionStatus::Success()));
}

// Tests that the DM token is removed if the server requests the device to be
// reset.
TEST_F(DMClientTest, FetchPoliciesReset) {
  EnsureRegistered();

  EXPECT_CALL(*mock_cloud_policy_client_, FetchPolicy).WillOnce([&] {
    mock_cloud_policy_client_->SetStatus(
        policy::DM_STATUS_SERVICE_DEVICE_NEEDS_RESET);
    mock_cloud_policy_client_->dm_token_.clear();
    mock_cloud_policy_client_->NotifyClientError();
    mock_cloud_policy_client_->NotifyRegistrationStateChanged();
  });

  base::RunLoop run_loop;
  dm_client_->FetchPolicies(
      PolicyFetchReason::kScheduled, test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.EqualsDeviceManagementStatus(
            policy::DM_STATUS_SERVICE_DEVICE_NEEDS_RESET));
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();

  EXPECT_TRUE(test_token_service_->GetDmToken().empty());
  EXPECT_TRUE(test_event_logger_->registration_events().empty());
  EXPECT_THAT(test_event_logger_->policy_fetch_events(),
              ElementsAre(EnterpriseCompanionStatus::FromDeviceManagementStatus(
                  policy::DM_STATUS_SERVICE_DEVICE_NEEDS_RESET)));
}

// Tests that the DM token is replaced with the invalid token if the server
// indicates that the device is not found.
TEST_F(DMClientTest, FetchPoliciesInvalidation) {
  EnsureRegistered();

  EXPECT_CALL(*mock_cloud_policy_client_, FetchPolicy).WillOnce([&] {
    mock_cloud_policy_client_->SetStatus(
        policy::DM_STATUS_SERVICE_DEVICE_NOT_FOUND);
    mock_cloud_policy_client_->dm_token_.clear();
    mock_cloud_policy_client_->NotifyClientError();
    mock_cloud_policy_client_->NotifyRegistrationStateChanged();
  });

  base::RunLoop run_loop;
  dm_client_->FetchPolicies(
      PolicyFetchReason::kTest, test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.EqualsDeviceManagementStatus(
            policy::DM_STATUS_SERVICE_DEVICE_NOT_FOUND));
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();

  EXPECT_TRUE(dm_storage_->IsDeviceDeregistered());
  EXPECT_TRUE(test_event_logger_->registration_events().empty());
  EXPECT_THAT(test_event_logger_->policy_fetch_events(),
              ElementsAre(EnterpriseCompanionStatus::FromDeviceManagementStatus(
                  policy::DM_STATUS_SERVICE_DEVICE_NOT_FOUND)));
}

// Tests that the client is able to sequence multiple concurrent requests.
TEST_F(DMClientTest, OverlappingCallsSequenced) {
  using enum PolicyFetchReason;

  test_token_service_->StoreEnrollmentToken(kFakeEnrollmentToken);
  auto respond_to_enroll = [&] {
    mock_cloud_policy_client_->SetDMToken(kFakeDMToken);
    mock_cloud_policy_client_->NotifyRegistrationStateChanged();
  };
  auto respond_to_fetch = [&] {
    mock_cloud_policy_client_->SetStatus(policy::DM_STATUS_SUCCESS);
    mock_cloud_policy_client_->NotifyPolicyFetched();
  };
  testing::InSequence expect_calls_in_sequence;
  EXPECT_CALL(*mock_cloud_policy_client_,
              RegisterPolicyAgentWithEnrollmentToken)
      .WillOnce(respond_to_enroll);
  EXPECT_CALL(*mock_cloud_policy_client_, FetchPolicy(kScheduled))
      .WillOnce(respond_to_fetch);
  EXPECT_CALL(*mock_cloud_policy_client_, FetchPolicy(kUserRequest))
      .WillOnce(respond_to_fetch);
  EXPECT_CALL(*mock_cloud_policy_client_, FetchPolicy(kUnspecified))
      .WillOnce(respond_to_fetch);

  base::RunLoop wait_for_all_tasks_complete;
  base::RepeatingClosure barrier =
      base::BarrierClosure(4, wait_for_all_tasks_complete.QuitClosure());
  dm_client_->RegisterPolicyAgent(
      test_event_logger_,
      base::BindLambdaForTesting(
          [&barrier](const EnterpriseCompanionStatus& status) {
            barrier.Run();
          }));
  for (const PolicyFetchReason reason : {
           kScheduled,
           kUserRequest,
           kUnspecified,
       }) {
    dm_client_->FetchPolicies(
        reason, test_event_logger_,
        base::BindLambdaForTesting(
            [&barrier](const EnterpriseCompanionStatus& status) {
              barrier.Run();
            }));
  }
  wait_for_all_tasks_complete.Run();
}

// Tests that the client can avoid hanging if CloudPolicyClient doesn't respond.
TEST_F(DMClientTest, HandlesTaskTimeout) {
  using enum PolicyFetchReason;

  EnsureRegistered();

  EXPECT_CALL(*mock_cloud_policy_client_, FetchPolicy(kScheduled));

  base::RunLoop run_loop;
  dm_client_->FetchPolicies(
      kScheduled, test_event_logger_,
      base::BindLambdaForTesting([&](const EnterpriseCompanionStatus& status) {
        EXPECT_TRUE(status.EqualsApplicationError(
            ApplicationError::kCloudPolicyClientTimeout));
        test_event_logger_->Flush(run_loop.QuitClosure());
      }));
  run_loop.Run();
}

// If a task times out, future tasks shouldn't be broken.
TEST_F(DMClientTest, HandlesTasksAfterTimeout) {
  using enum PolicyFetchReason;

  EnsureRegistered();

  EXPECT_CALL(*mock_cloud_policy_client_, FetchPolicy(kScheduled));
  EXPECT_CALL(*mock_cloud_policy_client_, FetchPolicy(kUserRequest))
      .WillOnce([&] {
        mock_cloud_policy_client_->SetStatus(policy::DM_STATUS_SUCCESS);
        mock_cloud_policy_client_->NotifyPolicyFetched();
      });

  {
    base::RunLoop run_loop;
    dm_client_->FetchPolicies(
        kScheduled, test_event_logger_,
        base::BindLambdaForTesting(
            [&](const EnterpriseCompanionStatus& status) {
              EXPECT_TRUE(status.EqualsApplicationError(
                  ApplicationError::kCloudPolicyClientTimeout));
              test_event_logger_->Flush(run_loop.QuitClosure());
            }));
    run_loop.Run();
  }

  {
    base::RunLoop run_loop;
    dm_client_->FetchPolicies(
        kUserRequest, test_event_logger_,
        base::BindLambdaForTesting(
            [&](const EnterpriseCompanionStatus& status) {
              EXPECT_TRUE(status.ok());
              test_event_logger_->Flush(run_loop.QuitClosure());
            }));
    run_loop.Run();
  }
}

}  // namespace enterprise_companion
