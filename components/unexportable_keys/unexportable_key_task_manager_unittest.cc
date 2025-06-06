// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/unexportable_keys/unexportable_key_task_manager.h"

#include <variant>

#include "base/memory/scoped_refptr.h"
#include "base/test/gmock_expected_support.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/token.h"
#include "base/types/expected.h"
#include "components/unexportable_keys/background_task_priority.h"
#include "components/unexportable_keys/mock_unexportable_key.h"
#include "components/unexportable_keys/ref_counted_unexportable_signing_key.h"
#include "components/unexportable_keys/service_error.h"
#include "components/unexportable_keys/unexportable_key_id.h"
#include "crypto/scoped_fake_unexportable_key_provider.h"
#include "crypto/signature_verifier.h"
#include "crypto/unexportable_key.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace unexportable_keys {

using testing::ElementsAreArray;
using testing::Return;

namespace {
// Result histograms:
constexpr std::string_view kGenerateKeyTaskResultHistogramName =
    "Crypto.UnexportableKeys.BackgroundTaskResult.GenerateKey";
constexpr std::string_view kFromWrappedKeyTaskResultHistogramName =
    "Crypto.UnexportableKeys.BackgroundTaskResult.FromWrappedKey";
constexpr std::string_view kSignTaskResultHistogramName =
    "Crypto.UnexportableKeys.BackgroundTaskResult.Sign";
// Retries histograms:
constexpr std::string_view kGenerateKeyTaskRetriesSuccessHistogramName =
    "Crypto.UnexportableKeys.BackgroundTaskRetries.GenerateKey.Success";
constexpr std::string_view kFromWrappedKeyTaskRetriesSuccessHistogramName =
    "Crypto.UnexportableKeys.BackgroundTaskRetries.FromWrappedKey.Success";
constexpr std::string_view kSignTaskRetriesSuccessHistogramName =
    "Crypto.UnexportableKeys.BackgroundTaskRetries.Sign.Success";
constexpr std::string_view kGenerateKeyTaskRetriesFailureHistogramName =
    "Crypto.UnexportableKeys.BackgroundTaskRetries.GenerateKey.Failure";
constexpr std::string_view kFromWrappedKeyTaskRetriesFailureHistogramName =
    "Crypto.UnexportableKeys.BackgroundTaskRetries.FromWrappedKey.Failure";
constexpr std::string_view kSignTaskRetriesFailureHistogramName =
    "Crypto.UnexportableKeys.BackgroundTaskRetries.Sign.Failure";
}  // namespace

class UnexportableKeyTaskManagerTest : public testing::Test {
 public:
  UnexportableKeyTaskManagerTest() = default;
  ~UnexportableKeyTaskManagerTest() override = default;

  void RunBackgroundTasks() { task_environment_.RunUntilIdle(); }

  UnexportableKeyTaskManager& task_manager() { return task_manager_; }

  void DisableKeyProvider() {
    // Using `emplace()` to destroy the existing scoped object before
    // constructing a new one.
    scoped_key_provider_.emplace<crypto::ScopedNullUnexportableKeyProvider>();
  }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadPoolExecutionMode::
          QUEUED};  // QUEUED - tasks don't run until `RunUntilIdle()` is
                    // called.
  // Provides a fake key provider by default.
  std::variant<crypto::ScopedFakeUnexportableKeyProvider,
               crypto::ScopedNullUnexportableKeyProvider>
      scoped_key_provider_;
  UnexportableKeyTaskManager task_manager_{
      crypto::UnexportableKeyProvider::Config()};
};

TEST_F(UnexportableKeyTaskManagerTest, GenerateKeyAsync) {
  base::HistogramTester histogram_tester;
  base::test::TestFuture<
      ServiceErrorOr<scoped_refptr<RefCountedUnexportableSigningKey>>>
      future;
  auto supported_algorithm = {crypto::SignatureVerifier::ECDSA_SHA256};

  task_manager().GenerateSigningKeySlowlyAsync(
      supported_algorithm, BackgroundTaskPriority::kBestEffort,
      future.GetCallback());
  EXPECT_FALSE(future.IsReady());
  RunBackgroundTasks();

  EXPECT_TRUE(future.IsReady());
  EXPECT_THAT(future.Get(), base::test::ValueIs(::testing::NotNull()));
  EXPECT_THAT(
      histogram_tester.GetAllSamples(kGenerateKeyTaskResultHistogramName),
      testing::ElementsAre(base::Bucket(kNoServiceErrorForMetrics, 1)));
  EXPECT_THAT(histogram_tester.GetAllSamples(
                  kGenerateKeyTaskRetriesSuccessHistogramName),
              testing::ElementsAre(base::Bucket(0, 1)));
}

TEST_F(UnexportableKeyTaskManagerTest,
       GenerateKeyAsyncFailureUnsupportedAlgorithm) {
  base::HistogramTester histogram_tester;
  base::test::TestFuture<
      ServiceErrorOr<scoped_refptr<RefCountedUnexportableSigningKey>>>
      future;
  // RSA_PKCS1_SHA1 is not supported by the protocol, so the key generation
  // should fail.
  auto unsupported_algorithm = {crypto::SignatureVerifier::RSA_PKCS1_SHA1};

  task_manager().GenerateSigningKeySlowlyAsync(
      unsupported_algorithm, BackgroundTaskPriority::kBestEffort,
      future.GetCallback());
  RunBackgroundTasks();

  EXPECT_EQ(future.Get(),
            base::unexpected(ServiceError::kAlgorithmNotSupported));
  EXPECT_THAT(
      histogram_tester.GetAllSamples(kGenerateKeyTaskResultHistogramName),
      testing::ElementsAre(
          base::Bucket(ServiceError::kAlgorithmNotSupported, 1)));
  EXPECT_THAT(histogram_tester.GetAllSamples(
                  kGenerateKeyTaskRetriesFailureHistogramName),
              testing::ElementsAre(base::Bucket(0, 1)));
}

TEST_F(UnexportableKeyTaskManagerTest, GenerateKeyAsyncFailureNoKeyProvider) {
  base::HistogramTester histogram_tester;
  base::test::TestFuture<
      ServiceErrorOr<scoped_refptr<RefCountedUnexportableSigningKey>>>
      future;
  auto supported_algorithm = {crypto::SignatureVerifier::ECDSA_SHA256};

  DisableKeyProvider();
  task_manager().GenerateSigningKeySlowlyAsync(
      supported_algorithm, BackgroundTaskPriority::kBestEffort,
      future.GetCallback());
  RunBackgroundTasks();

  EXPECT_EQ(future.Get(), base::unexpected(ServiceError::kNoKeyProvider));
  EXPECT_THAT(
      histogram_tester.GetAllSamples(kGenerateKeyTaskResultHistogramName),
      testing::ElementsAre(base::Bucket(ServiceError::kNoKeyProvider, 1)));
  EXPECT_THAT(histogram_tester.GetAllSamples(
                  kGenerateKeyTaskRetriesFailureHistogramName),
              testing::ElementsAre(base::Bucket(0, 1)));
}

TEST_F(UnexportableKeyTaskManagerTest, FromWrappedKeyAsync) {
  // First, generate a new signing key.
  base::test::TestFuture<
      ServiceErrorOr<scoped_refptr<RefCountedUnexportableSigningKey>>>
      generate_key_future;
  auto supported_algorithm = {crypto::SignatureVerifier::ECDSA_SHA256};
  task_manager().GenerateSigningKeySlowlyAsync(
      supported_algorithm, BackgroundTaskPriority::kBestEffort,
      generate_key_future.GetCallback());
  RunBackgroundTasks();
  ASSERT_OK_AND_ASSIGN(scoped_refptr<RefCountedUnexportableSigningKey> key,
                       generate_key_future.Get());
  std::vector<uint8_t> wrapped_key = key->key().GetWrappedKey();

  // Second, unwrap the newly generated key.
  base::HistogramTester histogram_tester;
  base::test::TestFuture<
      ServiceErrorOr<scoped_refptr<RefCountedUnexportableSigningKey>>>
      unwrap_key_future;

  task_manager().FromWrappedSigningKeySlowlyAsync(
      wrapped_key, BackgroundTaskPriority::kBestEffort,
      unwrap_key_future.GetCallback());
  EXPECT_FALSE(unwrap_key_future.IsReady());
  RunBackgroundTasks();

  EXPECT_TRUE(unwrap_key_future.IsReady());
  ASSERT_OK_AND_ASSIGN(auto unwrapped_key, unwrap_key_future.Get());
  EXPECT_NE(unwrapped_key, nullptr);
  // New key should have a unique ID.
  EXPECT_NE(unwrapped_key->id(), key->id());
  // Public key should be the same for both keys.
  EXPECT_EQ(key->key().GetSubjectPublicKeyInfo(),
            unwrapped_key->key().GetSubjectPublicKeyInfo());
  EXPECT_THAT(
      histogram_tester.GetAllSamples(kFromWrappedKeyTaskResultHistogramName),
      testing::ElementsAre(base::Bucket(kNoServiceErrorForMetrics, 1)));
  EXPECT_THAT(histogram_tester.GetAllSamples(
                  kFromWrappedKeyTaskRetriesSuccessHistogramName),
              testing::ElementsAre(base::Bucket(0, 1)));
}

TEST_F(UnexportableKeyTaskManagerTest, FromWrappedKeyAsyncFailureEmptyKey) {
  base::HistogramTester histogram_tester;
  base::test::TestFuture<
      ServiceErrorOr<scoped_refptr<RefCountedUnexportableSigningKey>>>
      future;
  std::vector<uint8_t> empty_wrapped_key;

  task_manager().FromWrappedSigningKeySlowlyAsync(
      empty_wrapped_key, BackgroundTaskPriority::kBestEffort,
      future.GetCallback());
  RunBackgroundTasks();

  EXPECT_EQ(future.Get(), base::unexpected(ServiceError::kCryptoApiFailed));
  EXPECT_THAT(
      histogram_tester.GetAllSamples(kFromWrappedKeyTaskResultHistogramName),
      testing::ElementsAre(base::Bucket(ServiceError::kCryptoApiFailed, 1)));
  EXPECT_THAT(histogram_tester.GetAllSamples(
                  kFromWrappedKeyTaskRetriesFailureHistogramName),
              testing::ElementsAre(base::Bucket(0, 1)));
}

TEST_F(UnexportableKeyTaskManagerTest,
       FromWrappedKeyAsyncFailureNoKeyProvider) {
  // First, obtain a valid wrapped key.
  base::test::TestFuture<
      ServiceErrorOr<scoped_refptr<RefCountedUnexportableSigningKey>>>
      generate_key_future;
  auto supported_algorithm = {crypto::SignatureVerifier::ECDSA_SHA256};
  task_manager().GenerateSigningKeySlowlyAsync(
      supported_algorithm, BackgroundTaskPriority::kBestEffort,
      generate_key_future.GetCallback());
  RunBackgroundTasks();
  ASSERT_OK_AND_ASSIGN(scoped_refptr<RefCountedUnexportableSigningKey> key,
                       generate_key_future.Get());
  std::vector<uint8_t> wrapped_key = key->key().GetWrappedKey();

  // Second, emulate the key provider being not available and check that
  // `FromWrappedSigningKeySlowlyAsync()` returns a corresponding error.
  base::HistogramTester histogram_tester;
  base::test::TestFuture<
      ServiceErrorOr<scoped_refptr<RefCountedUnexportableSigningKey>>>
      future;

  DisableKeyProvider();
  task_manager().FromWrappedSigningKeySlowlyAsync(
      wrapped_key, BackgroundTaskPriority::kBestEffort, future.GetCallback());
  RunBackgroundTasks();

  EXPECT_EQ(future.Get(), base::unexpected(ServiceError::kNoKeyProvider));
  EXPECT_THAT(
      histogram_tester.GetAllSamples(kFromWrappedKeyTaskResultHistogramName),
      testing::ElementsAre(base::Bucket(ServiceError::kNoKeyProvider, 1)));
  EXPECT_THAT(histogram_tester.GetAllSamples(
                  kFromWrappedKeyTaskRetriesFailureHistogramName),
              testing::ElementsAre(base::Bucket(0, 1)));
}

TEST_F(UnexportableKeyTaskManagerTest, SignAsync) {
  // First, generate a new signing key.
  base::test::TestFuture<
      ServiceErrorOr<scoped_refptr<RefCountedUnexportableSigningKey>>>
      generate_key_future;
  auto supported_algorithm = {crypto::SignatureVerifier::ECDSA_SHA256};
  task_manager().GenerateSigningKeySlowlyAsync(
      supported_algorithm, BackgroundTaskPriority::kBestEffort,
      generate_key_future.GetCallback());
  RunBackgroundTasks();
  ASSERT_OK_AND_ASSIGN(auto key, generate_key_future.Get());

  // Second, sign some data with the key.
  base::HistogramTester histogram_tester;
  base::test::TestFuture<ServiceErrorOr<std::vector<uint8_t>>> sign_future;
  std::vector<uint8_t> data = {4, 8, 15, 16, 23, 42};
  task_manager().SignSlowlyAsync(key, data, BackgroundTaskPriority::kBestEffort,
                                 /*max_retries=*/0, sign_future.GetCallback());
  EXPECT_FALSE(sign_future.IsReady());
  RunBackgroundTasks();
  EXPECT_TRUE(sign_future.IsReady());
  ASSERT_OK_AND_ASSIGN(const auto signed_data, sign_future.Get());
  EXPECT_THAT(histogram_tester.GetAllSamples(kSignTaskResultHistogramName),
              testing::ElementsAre(base::Bucket(kNoServiceErrorForMetrics, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples(kSignTaskRetriesSuccessHistogramName),
      testing::ElementsAre(base::Bucket(0, 1)));

  // Also verify that the signature was generated correctly.
  crypto::SignatureVerifier verifier;
  ASSERT_TRUE(verifier.VerifyInit(key->key().Algorithm(), signed_data,
                                  key->key().GetSubjectPublicKeyInfo()));
  verifier.VerifyUpdate(data);
  EXPECT_TRUE(verifier.VerifyFinal());
}

TEST_F(UnexportableKeyTaskManagerTest, SignAsyncNullKey) {
  base::HistogramTester histogram_tester;
  base::test::TestFuture<ServiceErrorOr<std::vector<uint8_t>>> sign_future;
  std::vector<uint8_t> data = {4, 8, 15, 16, 23, 42};

  task_manager().SignSlowlyAsync(nullptr, data,
                                 BackgroundTaskPriority::kBestEffort,
                                 /*max_retries=*/0, sign_future.GetCallback());
  RunBackgroundTasks();

  EXPECT_EQ(sign_future.Get(), base::unexpected(ServiceError::kKeyNotFound));
  EXPECT_THAT(
      histogram_tester.GetAllSamples(kSignTaskResultHistogramName),
      testing::ElementsAre(base::Bucket(ServiceError::kKeyNotFound, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples(kSignTaskRetriesFailureHistogramName),
      testing::ElementsAre(base::Bucket(0, 1)));
}

TEST_F(UnexportableKeyTaskManagerTest, RetrySignAsyncWithSuccess) {
  auto key = std::make_unique<MockUnexportableKey>();
  std::vector<uint8_t> data = {4, 8, 15, 16, 23, 42};
  EXPECT_CALL(*key, SignSlowly(ElementsAreArray(data)))
      .WillOnce(Return(std::nullopt))
      .WillOnce(Return(std::nullopt))
      .WillOnce(Return(std::vector<uint8_t>{1, 2, 3}));
  auto ref_counted_key = base::MakeRefCounted<RefCountedUnexportableSigningKey>(
      std::move(key), UnexportableKeyId());

  base::HistogramTester histogram_tester;
  base::test::TestFuture<ServiceErrorOr<std::vector<uint8_t>>> sign_future;
  task_manager().SignSlowlyAsync(ref_counted_key, data,
                                 BackgroundTaskPriority::kBestEffort,
                                 /*max_retries=*/2, sign_future.GetCallback());
  RunBackgroundTasks();
  EXPECT_THAT(sign_future.Get(), base::test::HasValue());
  EXPECT_THAT(histogram_tester.GetAllSamples(kSignTaskResultHistogramName),
              testing::ElementsAre(base::Bucket(kNoServiceErrorForMetrics, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples(kSignTaskRetriesSuccessHistogramName),
      testing::ElementsAre(base::Bucket(2, 1)));
}

TEST_F(UnexportableKeyTaskManagerTest, RetrySignAsyncWithFailure) {
  auto key = std::make_unique<MockUnexportableKey>();
  std::vector<uint8_t> data = {0, 1, 1, 2, 3, 5, 8};
  EXPECT_CALL(*key, SignSlowly(ElementsAreArray(data)))
      .Times(3)
      .WillRepeatedly(Return(std::nullopt));
  auto ref_counted_key = base::MakeRefCounted<RefCountedUnexportableSigningKey>(
      std::move(key), UnexportableKeyId());

  base::HistogramTester histogram_tester;
  base::test::TestFuture<ServiceErrorOr<std::vector<uint8_t>>> sign_future;
  task_manager().SignSlowlyAsync(ref_counted_key, data,
                                 BackgroundTaskPriority::kBestEffort,
                                 /*max_retries=*/2, sign_future.GetCallback());
  RunBackgroundTasks();
  EXPECT_EQ(sign_future.Get(),
            base::unexpected(ServiceError::kCryptoApiFailed));
  EXPECT_THAT(
      histogram_tester.GetAllSamples(kSignTaskResultHistogramName),
      testing::ElementsAre(base::Bucket(ServiceError::kCryptoApiFailed, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples(kSignTaskRetriesFailureHistogramName),
      testing::ElementsAre(base::Bucket(2, 1)));
}

}  // namespace unexportable_keys
