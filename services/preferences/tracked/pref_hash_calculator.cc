// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/preferences/tracked/pref_hash_calculator.h"

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "crypto/hmac.h"
#include "crypto/sha2.h"

namespace {

// Calculates an HMAC of |message| using |key|, encoded as a hexadecimal string.
std::string GetDigestString(const std::string& key,
                            const std::string& message) {
  crypto::HMAC hmac(crypto::HMAC::SHA256);
  std::vector<uint8_t> digest(hmac.DigestLength());
  if (!hmac.Init(key) || !hmac.Sign(message, &digest[0], digest.size())) {
    NOTREACHED();
  }
  return base::HexEncode(digest);
}

void RemoveEmptyValueDictEntries(base::Value::Dict& dict);
void RemoveEmptyValueListEntries(base::Value::List& list);

// Removes empty Dict and List Values from |dict|, potentially nested.
// This function may leave |dict| empty, and |dict| may be empty when passed in.
void RemoveEmptyValueDictEntries(base::Value::Dict& dict) {
  auto it = dict.begin();
  while (it != dict.end()) {
    base::Value& value = it->second;
    if (value.is_list()) {
      base::Value::List& sub_list = value.GetList();
      RemoveEmptyValueListEntries(sub_list);
      if (sub_list.empty()) {
        it = dict.erase(it);
        continue;
      }
    }
    if (value.is_dict()) {
      base::Value::Dict& sub_dict = value.GetDict();
      RemoveEmptyValueDictEntries(sub_dict);
      if (sub_dict.empty()) {
        it = dict.erase(it);
        continue;
      }
    }
    it++;
  }
}

// Removes empty Dict and List Values from |list|, potentially nested.
// This function may leave |list| empty, and |list| may be empty when passed in.
void RemoveEmptyValueListEntries(base::Value::List& list) {
  auto it = list.begin();
  while (it != list.end()) {
    base::Value& item = *it;
    if (item.is_list()) {
      base::Value::List& sub_list = item.GetList();
      RemoveEmptyValueListEntries(sub_list);
      if (sub_list.empty()) {
        it = list.erase(it);
        continue;
      }
    }
    if (item.is_dict()) {
      base::Value::Dict& sub_dict = item.GetDict();
      RemoveEmptyValueDictEntries(sub_dict);
      if (sub_dict.empty()) {
        it = list.erase(it);
        continue;
      }
    }
    it++;
  }
}

// Verifies that |digest_string| is a valid HMAC of |message| using |key|.
// |digest_string| must be encoded as a hexadecimal string.
bool VerifyDigestString(const std::string& key,
                        const std::string& message,
                        const std::string& digest_string) {
  crypto::HMAC hmac(crypto::HMAC::SHA256);
  std::string digest;
  return base::HexStringToString(digest_string, &digest) && hmac.Init(key) &&
         hmac.Verify(message, digest);
}

// Renders |value| as a string. |value| may be NULL, in which case the result
// is an empty string. This method can be expensive and its result should be
// re-used rather than recomputed where possible.

std::string ValueAsString(const base::Value::Dict* value) {
  if (!value)
    return std::string();

  base::Value::Dict dict = value->Clone();
  RemoveEmptyValueDictEntries(dict);
  return base::WriteJson(dict).value_or(std::string());
}

std::string ValueAsString(const base::Value* value) {
  if (!value)
    return std::string();

  if (value->is_dict())
    return ValueAsString(&value->GetDict());

  return base::WriteJson(*value).value_or(std::string());
}

// Concatenates |device_id|, |path|, and |value_as_string| to give the hash
// input.
std::string GetMessage(const std::string& device_id,
                       const std::string& path,
                       const std::string& value_as_string) {
  std::string message;
  message.reserve(device_id.size() + path.size() + value_as_string.size());
  message.append(device_id);
  message.append(path);
  message.append(value_as_string);
  return message;
}

// Concatenates |seed|, |path|, and |value_as_string| to give the SHA256 input
// for the encrypted hash scheme.
std::string GetEncryptedHashMessage(const std::string& seed,
                                    const std::string& path,
                                    const std::string& value_as_string) {
  return base::StrCat({seed, path, value_as_string});
}

}  // namespace

PrefHashCalculator::PrefHashCalculator(const std::string& seed,
                                       const std::string& device_id)
    : seed_(seed), device_id_(device_id) {}

PrefHashCalculator::~PrefHashCalculator() {}

std::string PrefHashCalculator::Calculate(const std::string& path,
                                          const base::Value* value) const {
  return GetDigestString(seed_,
                         GetMessage(device_id_, path, ValueAsString(value)));
}

std::string PrefHashCalculator::Calculate(const std::string& path,
                                          const base::Value::Dict* dict) const {
  return GetDigestString(seed_,
                         GetMessage(device_id_, path, ValueAsString(dict)));
}

PrefHashCalculator::ValidationResult PrefHashCalculator::Validate(
    const std::string& path,
    const base::Value* value,
    const std::string& digest_string) const {
  return Validate(path, ValueAsString(value), digest_string);
}

PrefHashCalculator::ValidationResult PrefHashCalculator::Validate(
    const std::string& path,
    const base::Value::Dict* dict,
    const std::string& digest_string) const {
  return Validate(path, ValueAsString(dict), digest_string);
}

PrefHashCalculator::ValidationResult PrefHashCalculator::Validate(
    const std::string& path,
    const std::string& value_as_string,
    const std::string& digest_string) const {
  if (VerifyDigestString(seed_, GetMessage(device_id_, path, value_as_string),
                         digest_string)) {
    return VALID;
  }
  return INVALID;
}

std::optional<std::string> PrefHashCalculator::CalculateEncryptedHash(
    const std::string& path,
    const base::Value* value,
    const os_crypt_async::Encryptor* encryptor) const {
  DCHECK(encryptor);

  std::string value_as_string = ValueAsString(value);
  std::string message = GetEncryptedHashMessage(seed_, path, value_as_string);
  std::string sha256_hash = crypto::SHA256HashString(message);

  // EncryptString returns raw bytes
  std::optional<std::vector<uint8_t>> encrypted_bytes =
      encryptor->EncryptString(sha256_hash);

  if (!encrypted_bytes) {
    return std::nullopt;
  }

  // Use Base64Encode version that returns a string.
  return base::Base64Encode(*encrypted_bytes);
}

std::optional<std::string> PrefHashCalculator::CalculateEncryptedHash(
    const std::string& path,
    const base::Value::Dict* dict,
    const os_crypt_async::Encryptor* encryptor) const {
  DCHECK(encryptor);
  std::string value_as_string = ValueAsString(dict);
  std::string message = GetEncryptedHashMessage(seed_, path, value_as_string);
  std::string sha256_hash = crypto::SHA256HashString(message);

  // EncryptString returns raw bytes
  std::optional<std::vector<uint8_t>> encrypted_bytes =
      encryptor->EncryptString(sha256_hash);

  if (!encrypted_bytes) {
    return std::nullopt;
  }

  // Use Base64Encode version that returns a string.
  return base::Base64Encode(*encrypted_bytes);
}

PrefHashCalculator::ValidationResult PrefHashCalculator::ValidateEncrypted(
    const std::string& path,
    const base::Value* value,
    const std::string& stored_encrypted_hash_base64,
    const os_crypt_async::Encryptor* encryptor) const {
  DCHECK(encryptor);

  // Base64 decode the stored string back into raw bytes
  std::string encrypted_hash_bytes;
  if (!base::Base64Decode(stored_encrypted_hash_base64,
                          &encrypted_hash_bytes)) {
    return INVALID_ENCRYPTED;
  }

  // Now decrypt the raw bytes
  std::string decrypted_hash_string;
  if (!encryptor->DecryptString(encrypted_hash_bytes, &decrypted_hash_string)) {
    return INVALID_ENCRYPTED;
  }

  // Decryption succeeded, now compare the result.
  std::string value_as_string = ValueAsString(value);
  std::string message = GetEncryptedHashMessage(seed_, path, value_as_string);
  std::string expected_sha256_hash = crypto::SHA256HashString(message);

  if (decrypted_hash_string == expected_sha256_hash) {
    return VALID_ENCRYPTED;
  } else {
    return INVALID_ENCRYPTED;
  }
}
