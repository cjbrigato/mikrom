// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_PEERCONNECTION_PEER_CONNECTION_UTIL_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_PEERCONNECTION_PEER_CONNECTION_UTIL_H_

#include <stdint.h>

#include "third_party/blink/renderer/core/dom/dom_high_res_time_stamp.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/modules/modules_export.h"

namespace blink {

// Returns a DOMHighResTimeStamp relative to Performance.timeOrigin.
MODULES_EXPORT DOMHighResTimeStamp
CalculateRTCEncodedFrameTimestamp(ExecutionContext*, base::TimeTicks);

// Converts DOMHighResTimeStamp relative to Performance.timeOrigin to a
// base::TimeTicks.
MODULES_EXPORT base::TimeTicks RTCEncodedFrameTimestampToTimeTicks(
    ExecutionContext*,
    DOMHighResTimeStamp);

// Returns a DOMHighResTimeStamp equivalent to the given delta
MODULES_EXPORT DOMHighResTimeStamp
CalculateRTCEncodedFrameTimeDelta(ExecutionContext*, base::TimeDelta);

// These functions convert between an audio level in -dBov and a linear double
// value in the [0.0-1.0] range. The dBov audio level is in the [0,127] range
// as defined in RFC 6464.
MODULES_EXPORT double ToLinearAudioLevel(uint8_t audio_level_dbov);
MODULES_EXPORT uint8_t FromLinearAudioLevel(double linear_audio_level);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_PEERCONNECTION_PEER_CONNECTION_UTIL_H_
