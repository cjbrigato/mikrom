// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_VIDEO_CAPTURE_PUBLIC_CPP_MOCK_VIDEO_SOURCE_H_
#define SERVICES_VIDEO_CAPTURE_PUBLIC_CPP_MOCK_VIDEO_SOURCE_H_

#include "media/capture/mojom/video_effects_manager.mojom.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/video_capture/public/mojom/video_frame_handler.mojom.h"
#include "services/video_capture/public/mojom/video_source.mojom.h"
#include "services/video_effects/public/cpp/buildflags.h"
#include "testing/gmock/include/gmock/gmock.h"

#if BUILDFLAG(ENABLE_VIDEO_EFFECTS)
#include "services/video_effects/public/mojom/video_effects_processor.mojom-forward.h"
#endif

namespace video_capture {

class MockVideoSource : public video_capture::mojom::VideoSource {
 public:
  MockVideoSource();
  ~MockVideoSource() override;

  MOCK_METHOD(
      void,
      CreatePushSubscription,
      (mojo::PendingRemote<video_capture::mojom::VideoFrameHandler> subscriber,
       const media::VideoCaptureParams& requested_settings,
       bool force_reopen_with_new_settings,
       mojo::PendingReceiver<video_capture::mojom::PushVideoStreamSubscription>
           subscription,
       CreatePushSubscriptionCallback callback),
      (override));

#if BUILDFLAG(ENABLE_VIDEO_EFFECTS)
  MOCK_METHOD(
      void,
      RegisterVideoEffectsProcessor,
      (mojo::PendingRemote<video_effects::mojom::VideoEffectsProcessor>));
#endif

  MOCK_METHOD(void,
              RegisterReadonlyVideoEffectsManager,
              (mojo::PendingRemote<media::mojom::ReadonlyVideoEffectsManager>));
};

}  // namespace video_capture

#endif  // SERVICES_VIDEO_CAPTURE_PUBLIC_CPP_MOCK_VIDEO_SOURCE_H_
