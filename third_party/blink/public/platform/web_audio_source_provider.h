/*
 * Copyright (C) 2011, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_PUBLIC_PLATFORM_WEB_AUDIO_SOURCE_PROVIDER_H_
#define THIRD_PARTY_BLINK_PUBLIC_PLATFORM_WEB_AUDIO_SOURCE_PROVIDER_H_

#include <vector>

namespace blink {

class WebAudioSourceProviderClient;

// Abstract interface for a pull-model client.
class WebAudioSourceProvider {
 public:
  virtual ~WebAudioSourceProvider() = default;

  // ProvideInput() gets called repeatedly to render time-slices of a continuous
  // audio stream. May be called from any thread.
  virtual void ProvideInput(const std::vector<float*>& audio_data,
                            int number_of_frames) = 0;

  // If a client is set, we call it back when the audio format is available.
  // Must always be called from the same thread. I.e., once called on a thread,
  // all future calls to SetClient must be issued from that same thread.
  virtual void SetClient(WebAudioSourceProviderClient*) {}
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_PUBLIC_PLATFORM_WEB_AUDIO_SOURCE_PROVIDER_H_
