// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SIGNIN_PUBLIC_BASE_SESSION_BINDING_UTILS_H_
#define COMPONENTS_SIGNIN_PUBLIC_BASE_SESSION_BINDING_UTILS_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "crypto/signature_verifier.h"

class GURL;
class HybridEncryptionKey;

namespace base {
class Time;
}

namespace signin {

// Converts a known algorithm string into
// `crypto::SignatureVerifier::SignatureAlgorithm`. Returns std::nullopt if
// algorithm is not recognized.
std::optional<crypto::SignatureVerifier::SignatureAlgorithm>
SignatureAlgorithmFromString(std::string_view algorithm);

// Parses the space-separated list of algorithms into a vector of
// `crypto::SignatureVerifier::SignatureAlgorithm`. Unrecognized algorithms
// aren't included in the result.
std::vector<crypto::SignatureVerifier::SignatureAlgorithm>
ParseSignatureAlgorithmList(std::string_view algorithm_list);

// Creates header and payload parts of a registration JWT.
std::optional<std::string> CreateKeyRegistrationHeaderAndPayloadForTokenBinding(
    std::string_view client_id,
    std::string_view auth_code,
    const GURL& registration_url,
    crypto::SignatureVerifier::SignatureAlgorithm algorithm,
    base::span<const uint8_t> pubkey,
    base::Time timestamp);
std::optional<std::string>
CreateKeyRegistrationHeaderAndPayloadForSessionBinding(
    std::string_view challenge,
    const GURL& registration_url,
    crypto::SignatureVerifier::SignatureAlgorithm algorithm,
    base::span<const uint8_t> pubkey,
    base::Time timestamp);

// Creates header and payload parts of an assertion JWT.
// `ephemeral_public_key` can be empty.
std::optional<std::string> CreateKeyAssertionHeaderAndPayload(
    crypto::SignatureVerifier::SignatureAlgorithm algorithm,
    base::span<const uint8_t> pubkey,
    std::string_view client_id,
    std::string_view challenge,
    const GURL& destination_url,
    std::string_view name_space,
    std::string_view ephemeral_public_key);

// Appends `signature` generated by `algorithm` to provided `header_and_payload`
// to form a complete JWT.
std::optional<std::string> AppendSignatureToHeaderAndPayload(
    std::string_view header_and_payload,
    crypto::SignatureVerifier::SignatureAlgorithm algorithm,
    base::span<const uint8_t> signature);

// Decrypts `base64_encrypted_value` with `ephemeral_key`.
// Returns an empty string if `base64_encrypted_value` is not base64url encoded
// or if the decryption failed.
std::string DecryptValueWithEphemeralKey(
    const HybridEncryptionKey& ephemeral_key,
    std::string_view base64_encrypted_value);

}  // namespace signin

#endif  // COMPONENTS_SIGNIN_PUBLIC_BASE_SESSION_BINDING_UTILS_H_
