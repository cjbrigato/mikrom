// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "google_apis/gaia/oauth2_mint_access_token_fetcher_adapter.h"

#include <string>
#include <string_view>
#include <vector>

#include "base/check.h"
#include "base/containers/span.h"
#include "base/memory/ref_counted.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/strcat.h"
#include "google_apis/gaia/gaia_auth_util.h"
#include "google_apis/gaia/gaia_constants.h"
#include "google_apis/gaia/google_service_auth_error.h"
#include "google_apis/gaia/oauth2_access_token_consumer.h"
#include "google_apis/gaia/oauth2_access_token_fetcher.h"
#include "google_apis/gaia/oauth2_mint_token_flow.h"
#include "google_apis/gaia/token_binding_response_encryption_error.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace {
void RecordEncryptionError(TokenBindingResponseEncryptionError error) {
  base::UmaHistogramEnumeration(
      "Signin.OAuth2MintToken.BoundFetchEncryptionError", error);
}

std::string_view GetAssertionTypeHistogramSuffix(
    const std::string& binding_key_assertion) {
  if (binding_key_assertion.empty()) {
    return ".NoAssertion";
  }

  if (binding_key_assertion == GaiaConstants::kTokenBindingAssertionSentinel) {
    return ".ChallengeSentinel";
  }

  if (binding_key_assertion ==
      GaiaConstants::kTokenBindingAssertionFailedPlaceholder) {
    return ".AssertionFailed";
  }

  return ".SignedAssertion";
}

void RecordFetchAuthError(const GoogleServiceAuthError& error,
                          const std::string& binding_key_assertion) {
  static constexpr std::string_view kFetchAuthErrorHistogram =
      "Signin.OAuth2MintToken.BoundFetchAuthError";
  base::UmaHistogramEnumeration(kFetchAuthErrorHistogram, error.state(),
                                GoogleServiceAuthError::NUM_STATES);

  base::UmaHistogramEnumeration(
      base::StrCat({kFetchAuthErrorHistogram,
                    GetAssertionTypeHistogramSuffix(binding_key_assertion)}),
      error.state(), GoogleServiceAuthError::NUM_STATES);
}
}  // namespace

OAuth2MintAccessTokenFetcherAdapter::OAuth2MintAccessTokenFetcherAdapter(
    OAuth2AccessTokenConsumer* consumer,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const GaiaId& user_gaia_id,
    const std::string& refresh_token,
    const std::string& device_id,
    const std::string& client_version,
    const std::string& client_channel)
    : OAuth2AccessTokenFetcher(consumer),
      url_loader_factory_(std::move(url_loader_factory)),
      user_gaia_id_(user_gaia_id),
      refresh_token_(refresh_token),
      device_id_(device_id),
      client_version_(client_version),
      client_channel_(client_channel) {}

OAuth2MintAccessTokenFetcherAdapter::~OAuth2MintAccessTokenFetcherAdapter() =
    default;

void OAuth2MintAccessTokenFetcherAdapter::Start(
    const std::string& client_id,
    const std::string& client_secret,
    const std::vector<std::string>& scopes) {
  if (binding_key_assertion_.empty()) {
    // The sentinel should be attached only if the `refresh_token_` is bound.
    // For now, `OAuth2MintAccessTokenFetcherAdapter` is only used with bound
    // tokens, so we can attach it unconditionally. This needs to be revised in
    // the future.
    binding_key_assertion_ = GaiaConstants::kTokenBindingAssertionSentinel;
  }
  std::string bound_oauth_token = gaia::CreateBoundOAuthToken(
      user_gaia_id_, refresh_token_, binding_key_assertion_);
  auto params = OAuth2MintTokenFlow::Parameters::CreateForClientFlow(
      client_id, std::vector<std::string_view>(scopes.begin(), scopes.end()),
      client_version_, client_channel_, device_id_, bound_oauth_token);
  if (mint_token_flow_factory_for_testing_) {
    mint_token_flow_ =
        mint_token_flow_factory_for_testing_.Run(this, std::move(params));
  } else {
    mint_token_flow_ =
        std::make_unique<OAuth2MintTokenFlow>(this, std::move(params));
  }
  mint_token_flow_->Start(url_loader_factory_, refresh_token_);
}

void OAuth2MintAccessTokenFetcherAdapter::CancelRequest() {
  mint_token_flow_.reset();
}

void OAuth2MintAccessTokenFetcherAdapter::SetBindingKeyAssertion(
    std::string assertion) {
  CHECK(!assertion.empty());
  binding_key_assertion_ = std::move(assertion);
}

void OAuth2MintAccessTokenFetcherAdapter::SetTokenDecryptor(
    TokenDecryptor decryptor) {
  CHECK(!decryptor.is_null());
  token_decryptor_ = std::move(decryptor);
}

void OAuth2MintAccessTokenFetcherAdapter::
    SetOAuth2MintTokenFlowFactoryForTesting(
        OAuth2MintTokenFlowFactory factory) {
  mint_token_flow_factory_for_testing_ = std::move(factory);
}

void OAuth2MintAccessTokenFetcherAdapter::OnMintTokenSuccess(
    const OAuth2MintTokenFlow::MintTokenResult& result) {
  std::string decrypted_token;
  if (result.is_token_encrypted) {
    if (token_decryptor_.is_null()) {
      RecordEncryptionError(
          TokenBindingResponseEncryptionError::kResponseUnexpectedlyEncrypted);
      RecordMetricsAndFireError(
          GoogleServiceAuthError::FromUnexpectedServiceResponse(
              "Unexpectedly received an encrypted token"));
      return;
    }
    std::string decryption_result = token_decryptor_.Run(result.access_token);
    if (decryption_result.empty()) {
      RecordEncryptionError(
          TokenBindingResponseEncryptionError::kDecryptionFailed);
      RecordMetricsAndFireError(
          GoogleServiceAuthError::FromUnexpectedServiceResponse(
              "Failed to decrypt token"));
      return;
    } else {
      RecordEncryptionError(
          TokenBindingResponseEncryptionError::kSuccessfullyDecrypted);
    }
    decrypted_token = std::move(decryption_result);
  } else {
    RecordEncryptionError(
        TokenBindingResponseEncryptionError::kSuccessNoEncryption);
    decrypted_token = std::move(result.access_token);
  }
  // The token will expire in `time_to_live` seconds. Take a 10% error margin to
  // prevent reusing a token too close to its expiration date.
  base::Time expiration_time = base::Time::Now() + result.time_to_live * 9 / 10;
  OAuth2AccessTokenConsumer::TokenResponse::Builder response_builder;
  response_builder.WithAccessToken(decrypted_token)
      .WithExpirationTime(expiration_time);
  RecordFetchAuthError(GoogleServiceAuthError::AuthErrorNone(),
                       binding_key_assertion_);
  FireOnGetTokenSuccess(response_builder.build());
}
void OAuth2MintAccessTokenFetcherAdapter::OnMintTokenFailure(
    const GoogleServiceAuthError& error) {
  RecordMetricsAndFireError(error);
}
void OAuth2MintAccessTokenFetcherAdapter::OnRemoteConsentSuccess(
    const RemoteConsentResolutionData& resolution_data) {
  RecordMetricsAndFireError(
      GoogleServiceAuthError::FromScopeLimitedUnrecoverableErrorReason(
          GoogleServiceAuthError::ScopeLimitedUnrecoverableErrorReason::
              kRemoteConsentResolutionRequired));
}

void OAuth2MintAccessTokenFetcherAdapter::RecordMetricsAndFireError(
    const GoogleServiceAuthError& error) {
  RecordFetchAuthError(error, binding_key_assertion_);
  FireOnGetTokenFailure(error);
}
