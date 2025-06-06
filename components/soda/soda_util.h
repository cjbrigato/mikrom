// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SODA_SODA_UTIL_H_
#define COMPONENTS_SODA_SODA_UTIL_H_

#include <string>

#include "media/mojo/mojom/speech_recognizer.mojom.h"

namespace speech {

// Returns whether the Speech On-Device API is supported in
// Chrome. This can depend on e.g. Chrome feature flags, platform/OS, supported
// CPU instructions.
bool IsOnDeviceSpeechRecognitionSupported();

// Returns the on-device speech recognition availability status for a given
// language code (i.e. "en-US"). Speech recognition considered available if the
// Speech On-Device API is supported and the language pack for the corresponding
// language is installed. This utility function must be run in the browser
// process.
media::mojom::AvailabilityStatus IsOnDeviceSpeechRecognitionAvailable(
    const std::string& language);

}  // namespace speech

#endif  // COMPONENTS_SODA_SODA_UTIL_H_
