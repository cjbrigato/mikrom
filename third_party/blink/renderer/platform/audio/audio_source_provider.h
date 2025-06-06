/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_AUDIO_AUDIO_SOURCE_PROVIDER_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_AUDIO_AUDIO_SOURCE_PROVIDER_H_

#include <cstddef>
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"

namespace blink {

class AudioBus;
class AudioSourceProviderClient;

// Abstract base-class for a pull-model client.
class AudioSourceProvider {
  USING_FAST_MALLOC(AudioSourceProvider);

 public:
  AudioSourceProvider() = default;
  AudioSourceProvider(const AudioSourceProvider&) = delete;
  AudioSourceProvider& operator=(const AudioSourceProvider&) = delete;
  virtual ~AudioSourceProvider() = default;

  // provideInput() gets called repeatedly to render time-slices of a continuous
  // audio stream.
  virtual void ProvideInput(AudioBus* bus, int frames_to_process) = 0;

  // If a client is set, we call it back when the audio format is available or
  // changes.
  virtual void SetClient(AudioSourceProviderClient*) = 0;

  // Connect to the destination node.
  virtual void ConnectToDestinationReady() {}
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_AUDIO_AUDIO_SOURCE_PROVIDER_H_
