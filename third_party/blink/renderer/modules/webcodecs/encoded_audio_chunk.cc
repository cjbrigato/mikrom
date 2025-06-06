// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/webcodecs/encoded_audio_chunk.h"

#include <utility>

#include "third_party/blink/renderer/bindings/modules/v8/v8_decrypt_config.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_encoded_audio_chunk_init.h"
#include "third_party/blink/renderer/modules/webcodecs/decrypt_config_util.h"
#include "third_party/blink/renderer/platform/bindings/script_state.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

EncodedAudioChunk* EncodedAudioChunk::Create(ScriptState* script_state,
                                             const EncodedAudioChunkInit* init,
                                             ExceptionState& exception_state) {
  auto array_span = AsSpan<const uint8_t>(init->data());
  auto* isolate = script_state->GetIsolate();

  // Try if we can transfer `init.data` into this chunk without copying it.
  auto buffer_contents = TransferArrayBufferForSpan(
      init->transfer(), array_span, exception_state, isolate);
  if (exception_state.HadException()) {
    return nullptr;
  }

  scoped_refptr<media::DecoderBuffer> buffer;
  if (array_span.empty()) {
    buffer = base::MakeRefCounted<media::DecoderBuffer>(0);
  } else if (buffer_contents.IsValid()) {
    buffer = media::DecoderBuffer::FromExternalMemory(
        std::make_unique<ArrayBufferContentsExternalMemory>(
            std::move(buffer_contents), array_span));
  } else {
    buffer = media::DecoderBuffer::CopyFrom(array_span);
  }
  DCHECK(buffer);

  // Clamp within bounds of our internal TimeDelta-based duration. See
  // media/base/timestamp_constants.h
  auto timestamp = base::Microseconds(init->timestamp());
  if (timestamp == media::kNoTimestamp)
    timestamp = base::TimeDelta::FiniteMin();
  else if (timestamp == media::kInfiniteDuration)
    timestamp = base::TimeDelta::FiniteMax();
  buffer->set_timestamp(timestamp);

  // media::kNoTimestamp corresponds to base::TimeDelta::Min(), and internally
  // denotes the absence of duration. We use base::TimeDelta::FiniteMax() -
  // which is one less than base::TimeDelta::Max() - because
  // base::TimeDelta::Max() is reserved for media::kInfiniteDuration, and is
  // handled differently.
  buffer->set_duration(
      init->hasDuration()
          ? base::Microseconds(std::min(
                uint64_t{base::TimeDelta::FiniteMax().InMicroseconds()},
                init->duration()))
          : media::kNoTimestamp);

  buffer->set_is_key_frame(init->type() == "key");

  if (init->hasDecryptConfig()) {
    auto decrypt_config = CreateMediaDecryptConfig(*init->decryptConfig());
    if (!decrypt_config) {
      exception_state.ThrowDOMException(DOMExceptionCode::kNotSupportedError,
                                        "Unsupported decryptConfig");
      return nullptr;
    }
    buffer->set_decrypt_config(std::move(decrypt_config));
  }

  return MakeGarbageCollected<EncodedAudioChunk>(std::move(buffer));
}

EncodedAudioChunk::EncodedAudioChunk(scoped_refptr<media::DecoderBuffer> buffer)
    : buffer_(std::move(buffer)) {}

V8EncodedAudioChunkType EncodedAudioChunk::type() const {
  return V8EncodedAudioChunkType(buffer_->is_key_frame()
                                     ? V8EncodedAudioChunkType::Enum::kKey
                                     : V8EncodedAudioChunkType::Enum::kDelta);
}

int64_t EncodedAudioChunk::timestamp() const {
  return buffer_->timestamp().InMicroseconds();
}

std::optional<uint64_t> EncodedAudioChunk::duration() const {
  if (buffer_->duration() == media::kNoTimestamp)
    return std::nullopt;
  return buffer_->duration().InMicroseconds();
}

uint64_t EncodedAudioChunk::byteLength() const {
  return buffer_->size();
}

void EncodedAudioChunk::copyTo(const AllowSharedBufferSource* destination,
                               ExceptionState& exception_state) {
  // Validate destination buffer.
  auto dest_wrapper = AsSpan<uint8_t>(destination);
  auto buffer_span = base::span(*buffer_);
  if (dest_wrapper.size() < buffer_span.size()) {
    exception_state.ThrowTypeError("destination is not large enough.");
    return;
  }

  if (buffer_span.empty()) {
    // Calling memcpy with nullptr is UB, even if count is zero.
    return;
  }

  // Copy data.
  dest_wrapper.copy_prefix_from(buffer_span);
}

}  // namespace blink
