// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/peerconnection/rtc_encoded_audio_frame_delegate.h"

#include <optional>
#include <utility>

#include "base/time/time.h"
#include "base/types/expected.h"
#include "third_party/blink/renderer/core/typed_arrays/dom_array_buffer.h"
#include "third_party/blink/renderer/modules/peerconnection/peer_connection_util.h"
#include "third_party/blink/renderer/platform/bindings/exception_code.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/bindings/v8_binding.h"
#include "third_party/blink/renderer/platform/peerconnection/webrtc_util.h"
#include "third_party/webrtc/api/frame_transformer_factory.h"

namespace blink {

static constexpr char kRTCEncodedAudioFrameDetachKey[] = "RTCEncodedAudioFrame";

const void* RTCEncodedAudioFramesAttachment::kAttachmentKey;

RTCEncodedAudioFrameDelegate::RTCEncodedAudioFrameDelegate(
    std::unique_ptr<webrtc::TransformableAudioFrameInterface> webrtc_frame,
    webrtc::ArrayView<const unsigned int> contributing_sources,
    std::optional<uint16_t> sequence_number)
    : webrtc_frame_(std::move(webrtc_frame)),
      contributing_sources_(contributing_sources),
      sequence_number_(sequence_number) {}

uint32_t RTCEncodedAudioFrameDelegate::RtpTimestamp() const {
  base::AutoLock lock(lock_);
  return webrtc_frame_ ? webrtc_frame_->GetTimestamp() : 0;
}

DOMArrayBuffer* RTCEncodedAudioFrameDelegate::CreateDataBuffer(
    v8::Isolate* isolate) const {
  ArrayBufferContents contents;
  {
    base::AutoLock lock(lock_);
    if (!webrtc_frame_) {
      // WebRTC frame already passed, return a detached ArrayBuffer.
      DOMArrayBuffer* buffer = DOMArrayBuffer::Create(
          /*num_elements=*/static_cast<size_t>(0), /*element_byte_size=*/1);
      ArrayBufferContents contents_to_drop;
      NonThrowableExceptionState exception_state;
      buffer->Transfer(isolate,
                       V8AtomicString(isolate, kRTCEncodedAudioFrameDetachKey),
                       contents_to_drop, exception_state);
      return buffer;
    }

    auto data = webrtc_frame_->GetData();
    contents = ArrayBufferContents(
        data.size(), 1, ArrayBufferContents::kNotShared,
        ArrayBufferContents::kDontInitialize,
        ArrayBufferContents::AllocationFailureBehavior::kCrash);
    CHECK(contents.IsValid());
    contents.ByteSpan().copy_from(data);
  }
  return DOMArrayBuffer::Create(std::move(contents));
}

void RTCEncodedAudioFrameDelegate::SetData(const DOMArrayBuffer* data) {
  base::AutoLock lock(lock_);
  if (webrtc_frame_ && data) {
    webrtc_frame_->SetData(webrtc::ArrayView<const uint8_t>(
        static_cast<const uint8_t*>(data->Data()), data->ByteLength()));
  }
}

base::expected<void, String>
RTCEncodedAudioFrameDelegate::SetWebRtcFrameMetadata(
    uint32_t rtp_timestamp,
    std::optional<uint8_t> payload_type,
    std::optional<webrtc::Timestamp> capture_time,
    std::optional<double> linear_audio_level) {
  base::AutoLock lock(lock_);
  if (!webrtc_frame_) {
    return base::unexpected("Underlying webrtc frame doesn't exist.");
  }

  // Payload type always has a current value. The new metadata must match it if
  // SetPayloadType is not supported.
  if (payload_type.has_value() &&
      payload_type != webrtc_frame_->GetPayloadType() &&
      !webrtc_frame_->CanSetPayloadType()) {
    return base::unexpected("payloadType cannot be modified");
  }

  if (capture_time != webrtc_frame_->CaptureTime() &&
      !webrtc_frame_->CanSetCaptureTime()) {
    return base::unexpected("captureTime cannot be modified");
  }

  std::optional<uint8_t> audio_level_dbov;
  if (linear_audio_level.has_value()) {
    audio_level_dbov = FromLinearAudioLevel(*linear_audio_level);
  }
  if (audio_level_dbov != webrtc_frame_->AudioLevel() &&
      !webrtc_frame_->CanSetAudioLevel()) {
    return base::unexpected("audioLevel cannot be modified");
  }

  webrtc_frame_->SetRTPTimestamp(rtp_timestamp);
  if (payload_type.has_value() && webrtc_frame_->CanSetPayloadType()) {
    webrtc_frame_->SetPayloadType(*payload_type);
  }
  if (webrtc_frame_->CanSetCaptureTime()) {
    webrtc_frame_->SetCaptureTime(capture_time);
  }
  if (webrtc_frame_->CanSetAudioLevel()) {
    webrtc_frame_->SetAudioLevel(audio_level_dbov);
  }

  return base::ok();
}

std::optional<uint32_t> RTCEncodedAudioFrameDelegate::Ssrc() const {
  base::AutoLock lock(lock_);
  return webrtc_frame_ ? std::make_optional(webrtc_frame_->GetSsrc())
                       : std::nullopt;
}

std::optional<uint8_t> RTCEncodedAudioFrameDelegate::PayloadType() const {
  base::AutoLock lock(lock_);
  return webrtc_frame_ ? std::make_optional(webrtc_frame_->GetPayloadType())
                       : std::nullopt;
}

std::optional<std::string> RTCEncodedAudioFrameDelegate::MimeType() const {
  base::AutoLock lock(lock_);
  return webrtc_frame_ ? std::make_optional(webrtc_frame_->GetMimeType())
                       : std::nullopt;
}

std::optional<uint16_t> RTCEncodedAudioFrameDelegate::SequenceNumber() const {
  return sequence_number_;
}

Vector<uint32_t> RTCEncodedAudioFrameDelegate::ContributingSources() const {
  return contributing_sources_;
}

std::optional<base::TimeTicks> RTCEncodedAudioFrameDelegate::ReceiveTime()
    const {
  base::AutoLock lock(lock_);
  if (!webrtc_frame_) {
    return std::nullopt;
  }
  return ConvertToOptionalTimeTicks(webrtc_frame_->ReceiveTime());
}

std::optional<base::TimeTicks> RTCEncodedAudioFrameDelegate::CaptureTime()
    const {
  base::AutoLock lock(lock_);
  if (!webrtc_frame_) {
    return std::nullopt;
  }
  return (webrtc_frame_->GetDirection() ==
          webrtc::TransformableFrameInterface::Direction::kReceiver)
             ? ConvertToOptionalTimeTicks(webrtc_frame_->CaptureTime(),
                                          WebRTCFrameNtpEpoch())
             : ConvertToOptionalTimeTicks(webrtc_frame_->CaptureTime());
}

std::optional<base::TimeDelta>
RTCEncodedAudioFrameDelegate::SenderCaptureTimeOffset() const {
  base::AutoLock lock(lock_);
  if (!webrtc_frame_) {
    return std::nullopt;
  }
  return ConvertToOptionalTimeDelta(webrtc_frame_->SenderCaptureTimeOffset());
}

std::optional<double> RTCEncodedAudioFrameDelegate::AudioLevel() const {
  base::AutoLock lock(lock_);
  return webrtc_frame_ && webrtc_frame_->AudioLevel()
             ? std::make_optional(
                   ToLinearAudioLevel(*webrtc_frame_->AudioLevel()))
             : std::nullopt;
}

std::unique_ptr<webrtc::TransformableAudioFrameInterface>
RTCEncodedAudioFrameDelegate::PassWebRtcFrame() {
  base::AutoLock lock(lock_);
  return std::move(webrtc_frame_);
}

std::unique_ptr<webrtc::TransformableAudioFrameInterface>
RTCEncodedAudioFrameDelegate::CloneWebRtcFrame() {
  base::AutoLock lock(lock_);
  if (!webrtc_frame_) {
    return nullptr;
  }
  return webrtc::CloneAudioFrame(webrtc_frame_.get());
}

}  // namespace blink
