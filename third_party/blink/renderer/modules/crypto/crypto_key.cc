/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/modules/crypto/crypto_key.h"

#include "base/numerics/safe_conversions.h"
#include "third_party/blink/public/platform/web_crypto_algorithm_params.h"
#include "third_party/blink/public/platform/web_crypto_key_algorithm.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/renderer/bindings/core/v8/to_v8_traits.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_object_builder.h"
#include "third_party/blink/renderer/core/typed_arrays/dom_typed_array.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/crypto_result.h"

namespace blink {

namespace {

const char* KeyTypeToString(WebCryptoKeyType type) {
  switch (type) {
    case kWebCryptoKeyTypeSecret:
      return "secret";
    case kWebCryptoKeyTypePublic:
      return "public";
    case kWebCryptoKeyTypePrivate:
      return "private";
  }
  NOTREACHED();
}

struct KeyUsageMapping {
  WebCryptoKeyUsage value;
  const char* const name;
};

// The order of this array is the same order that will appear in
// CryptoKey.usages. It must be kept ordered as described by the Web Crypto
// spec.
const KeyUsageMapping kKeyUsageMappings[] = {
    {kWebCryptoKeyUsageEncrypt, "encrypt"},
    {kWebCryptoKeyUsageDecrypt, "decrypt"},
    {kWebCryptoKeyUsageSign, "sign"},
    {kWebCryptoKeyUsageVerify, "verify"},
    {kWebCryptoKeyUsageDeriveKey, "deriveKey"},
    {kWebCryptoKeyUsageDeriveBits, "deriveBits"},
    {kWebCryptoKeyUsageWrapKey, "wrapKey"},
    {kWebCryptoKeyUsageUnwrapKey, "unwrapKey"},
};

static_assert(kEndOfWebCryptoKeyUsage == (1 << 7) + 1,
              "keyUsageMappings needs to be updated");

const char* KeyUsageToString(WebCryptoKeyUsage usage) {
  for (const auto& mapping : kKeyUsageMappings) {
    if (mapping.value == usage) {
      return mapping.name;
    }
  }
  NOTREACHED();
}

WebCryptoKeyUsageMask KeyUsageStringToMask(const String& usage_string) {
  for (const auto& mapping : kKeyUsageMappings) {
    if (mapping.name == usage_string) {
      return mapping.value;
    }
  }
  return 0;
}

class DictionaryBuilder : public WebCryptoKeyAlgorithmDictionary {
  STACK_ALLOCATED();

 public:
  explicit DictionaryBuilder(V8ObjectBuilder& builder) : builder_(builder) {}

  void SetString(const char* property_name, const char* value) override {
    builder_.AddString(property_name, value);
  }

  void SetUint(const char* property_name, unsigned value) override {
    builder_.AddNumber(property_name, value);
  }

  void SetAlgorithm(const char* property_name,
                    const WebCryptoAlgorithm& algorithm) override {
    DCHECK_EQ(algorithm.ParamsType(), kWebCryptoAlgorithmParamsTypeNone);

    V8ObjectBuilder algorithm_value(builder_.GetScriptState());
    algorithm_value.AddString(
        "name", WebCryptoAlgorithm::LookupAlgorithmInfo(algorithm.Id())->name);
    builder_.Add(property_name, algorithm_value);
  }

  void SetUint8Array(const char* property_name,
                     const std::vector<unsigned char>& vector) override {
    builder_.Add(property_name, DOMUint8Array::Create(vector));
  }

 private:
  V8ObjectBuilder& builder_;
};

}  // namespace

CryptoKey::~CryptoKey() = default;

CryptoKey::CryptoKey(const WebCryptoKey& key) : key_(key) {}

String CryptoKey::type() const {
  return KeyTypeToString(key_.GetType());
}

bool CryptoKey::extractable() const {
  return key_.Extractable();
}

ScriptObject CryptoKey::algorithm(ScriptState* script_state) {
  V8ObjectBuilder object_builder(script_state);
  DictionaryBuilder dictionary_builder(object_builder);
  key_.Algorithm().WriteToDictionary(&dictionary_builder);
  return object_builder.ToScriptObject();
}

// FIXME: This creates a new javascript array each time. What should happen
//        instead is return the same (immutable) array. (Javascript callers can
//        distinguish this by doing an == test on the arrays and seeing they are
//        different).
ScriptObject CryptoKey::usages(ScriptState* script_state) {
  Vector<String> result;
  for (const auto& mapping : kKeyUsageMappings) {
    WebCryptoKeyUsage usage = mapping.value;
    if (key_.Usages() & usage)
      result.push_back(KeyUsageToString(usage));
  }

  return ScriptObject(
      script_state->GetIsolate(),
      ToV8Traits<IDLSequence<IDLString>>::ToV8(script_state, result));
}

bool CryptoKey::CanBeUsedForAlgorithm(const WebCryptoAlgorithm& algorithm,
                                      WebCryptoKeyUsage usage,
                                      CryptoResult* result) const {
  // This order of tests on keys is done throughout the WebCrypto spec when
  // testing if a key can be used for an algorithm.
  //
  // For instance here are the steps as written for encrypt():
  //
  // https://w3c.github.io/webcrypto/Overview.html#dfn-SubtleCrypto-method-encrypt
  //
  // (8) If the name member of normalizedAlgorithm is not equal to the name
  //     attribute of the [[algorithm]] internal slot of key then throw an
  //     InvalidAccessError.
  //
  // (9) If the [[usages]] internal slot of key does not contain an entry
  //     that is "encrypt", then throw an InvalidAccessError.

  if (key_.Algorithm().Id() != algorithm.Id()) {
    result->CompleteWithError(kWebCryptoErrorTypeInvalidAccess,
                              "key.algorithm does not match that of operation");
    return false;
  }

  if (!(key_.Usages() & usage)) {
    result->CompleteWithError(kWebCryptoErrorTypeInvalidAccess,
                              "key.usages does not permit this operation");
    return false;
  }

  return true;
}

bool CryptoKey::ParseFormat(const String& format_string,
                            WebCryptoKeyFormat& format,
                            ExceptionState& exception_state) {
  // There are few enough values that testing serially is fast enough.
  if (format_string == "raw") {
    format = kWebCryptoKeyFormatRaw;
    return true;
  }
  if (format_string == "pkcs8") {
    format = kWebCryptoKeyFormatPkcs8;
    return true;
  }
  if (format_string == "spki") {
    format = kWebCryptoKeyFormatSpki;
    return true;
  }
  if (format_string == "jwk") {
    format = kWebCryptoKeyFormatJwk;
    return true;
  }

  exception_state.ThrowTypeError("Invalid keyFormat argument");
  return false;
}

bool CryptoKey::ParseUsageMask(const Vector<String>& usages,
                               WebCryptoKeyUsageMask& mask,
                               ExceptionState& exception_state) {
  mask = 0;
  for (wtf_size_t i = 0; i < usages.size(); ++i) {
    WebCryptoKeyUsageMask usage = KeyUsageStringToMask(usages[i]);
    if (!usage) {
      exception_state.ThrowTypeError("Invalid keyUsages argument");
      return false;
    }
    mask |= usage;
  }
  return true;
}

}  // namespace blink
