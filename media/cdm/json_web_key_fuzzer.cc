// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40285824): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include "media/cdm/json_web_key.h"

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "base/logging.h"

// For disabling noisy logging.
struct Environment {
  Environment() { logging::SetMinLogLevel(logging::LOGGING_FATAL); }
};

Environment* env = new Environment();

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::vector<uint8_t> license(data, data + size);
  std::vector<uint8_t> first_key;
  media::ExtractFirstKeyIdFromLicenseRequest(license, &first_key);

  std::string input(reinterpret_cast<const char*>(data), size);
  media::KeyIdAndKeyPairs keys;
  media::CdmSessionType session_type;
  media::ExtractKeysFromJWKSet(input, &keys, &session_type);

  media::KeyIdList key_ids;
  std::string error_message;
  media::ExtractKeyIdsFromKeyIdsInitData(input, &key_ids, &error_message);

  return 0;
}
