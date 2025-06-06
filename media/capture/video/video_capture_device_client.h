// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_CAPTURE_VIDEO_VIDEO_CAPTURE_DEVICE_CLIENT_H_
#define MEDIA_CAPTURE_VIDEO_VIDEO_CAPTURE_DEVICE_CLIENT_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <vector>

#include "base/feature_list.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/thread_collision_warner.h"
#include "build/build_config.h"
#include "media/capture/capture_export.h"
#include "media/capture/mojom/video_effects_manager.mojom.h"
#include "media/capture/video/video_capture_device.h"
#include "media/capture/video/video_frame_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/video_effects/public/cpp/buildflags.h"

#if BUILDFLAG(ENABLE_VIDEO_EFFECTS)
#include "media/capture/video/video_capture_effects_processor.h"
#include "services/video_effects/public/mojom/video_effects_processor.mojom-forward.h"
#endif  // BUILDFLAG(ENABLE_VIDEO_EFFECTS)

namespace gpu {
class ClientSharedImage;
}

namespace media {
class VideoCaptureBufferPool;
class VideoFrameReceiver;
class VideoCaptureJpegDecoder;

using VideoCaptureJpegDecoderFactoryCB =
    base::OnceCallback<std::unique_ptr<VideoCaptureJpegDecoder>()>;

#if BUILDFLAG(IS_MAC)
CAPTURE_EXPORT BASE_DECLARE_FEATURE(kFallbackToSharedMemoryIfNotNv12OnMac);
#endif

// Structure used to inject dependencies required for the
// `VideoCaptureDeviceClient` to apply video effects.
class CAPTURE_EXPORT VideoEffectsContext {
 public:
  VideoEffectsContext();

#if BUILDFLAG(ENABLE_VIDEO_EFFECTS)
  explicit VideoEffectsContext(
      mojo::PendingRemote<video_effects::mojom::VideoEffectsProcessor>
          processor_remote,
      mojo::PendingRemote<media::mojom::ReadonlyVideoEffectsManager>
          readonly_manager_remote);
#endif

  ~VideoEffectsContext();

  VideoEffectsContext(const VideoEffectsContext& other) = delete;
  VideoEffectsContext& operator=(VideoEffectsContext& other) = delete;

  VideoEffectsContext(VideoEffectsContext&& other);
  VideoEffectsContext& operator=(VideoEffectsContext&& other);

#if BUILDFLAG(ENABLE_VIDEO_EFFECTS)
  mojo::PendingRemote<video_effects::mojom::VideoEffectsProcessor>&&
  TakeVideoEffectsProcessor();

  mojo::PendingRemote<media::mojom::ReadonlyVideoEffectsManager>&&
  TakeReadonlyVideoEffectsManager();
#endif

 private:
#if BUILDFLAG(ENABLE_VIDEO_EFFECTS)
  mojo::PendingRemote<video_effects::mojom::VideoEffectsProcessor>
      video_effects_processor_;

  mojo::PendingRemote<media::mojom::ReadonlyVideoEffectsManager>
      readonly_video_effects_manager_;
#endif
};

// Implementation of VideoCaptureDevice::Client that uses a buffer pool
// to provide buffers and converts incoming data to the I420 format for
// consumption by a given VideoFrameReceiver. If
// |optional_jpeg_decoder_factory_callback| is provided, the
// VideoCaptureDeviceClient will attempt to use it for decoding of MJPEG frames.
// Otherwise, it will use libyuv to perform MJPEG to I420 conversion in
// software.
//
// Methods of this class may be called from any thread, and in practice will
// often be called on some auxiliary thread depending on the platform and the
// device type; including, for example, the DirectShow thread on Windows, the
// v4l2_thread on Linux, and the UI thread for tab capture.
// The owner is responsible for making sure that the instance outlives these
// calls.
class CAPTURE_EXPORT VideoCaptureDeviceClient :
#if BUILDFLAG(ENABLE_VIDEO_EFFECTS)
    public media::mojom::VideoEffectsConfigurationObserver,
#endif
    public VideoCaptureDevice::Client {
 public:
#if BUILDFLAG(IS_CHROMEOS)
  VideoCaptureDeviceClient(
      std::unique_ptr<VideoFrameReceiver> receiver,
      scoped_refptr<VideoCaptureBufferPool> buffer_pool,
      VideoCaptureJpegDecoderFactoryCB jpeg_decoder_factory_callback);
#else
  VideoCaptureDeviceClient(
      std::unique_ptr<VideoFrameReceiver> receiver,
      scoped_refptr<VideoCaptureBufferPool> buffer_pool,
      std::optional<VideoEffectsContext> video_effects_context);
#endif  // BUILDFLAG(IS_CHROMEOS)

  VideoCaptureDeviceClient(const VideoCaptureDeviceClient&) = delete;
  VideoCaptureDeviceClient& operator=(const VideoCaptureDeviceClient&) = delete;

  ~VideoCaptureDeviceClient() override;

  static Buffer MakeBufferStruct(
      scoped_refptr<VideoCaptureBufferPool> buffer_pool,
      int buffer_id,
      int frame_feedback_id);

  // VideoCaptureDevice::Client implementation.
  void OnCaptureConfigurationChanged() override;
  void OnIncomingCapturedData(
      const uint8_t* data,
      int length,
      const VideoCaptureFormat& frame_format,
      const gfx::ColorSpace& color_space,
      int clockwise_rotation,
      bool flip_y,
      base::TimeTicks reference_time,
      base::TimeDelta timestamp,
      std::optional<base::TimeTicks> capture_begin_timestamp,
      const std::optional<VideoFrameMetadata>& metadata,
      int frame_feedback_id) override;
  void OnIncomingCapturedImage(
      scoped_refptr<gpu::ClientSharedImage> shared_image,
      const VideoCaptureFormat& frame_format,
      int clockwise_rotation,
      base::TimeTicks reference_time,
      base::TimeDelta timestamp,
      std::optional<base::TimeTicks> capture_begin_timestamp,
      const std::optional<VideoFrameMetadata>& metadata,
      int frame_feedback_id) override;
  void OnIncomingCapturedExternalBuffer(
      CapturedExternalVideoBuffer buffer,
      base::TimeTicks reference_time,
      base::TimeDelta timestamp,
      std::optional<base::TimeTicks> capture_begin_timestamp,
      const gfx::Rect& visible_rect,
      const std::optional<VideoFrameMetadata>& metadata) override;
  ReserveResult ReserveOutputBuffer(const gfx::Size& dimensions,
                                    VideoPixelFormat format,
                                    int frame_feedback_id,
                                    Buffer* buffer,
                                    int* require_new_buffer_id,
                                    int* retire_old_buffer_id) override;
  void OnIncomingCapturedBuffer(
      Buffer buffer,
      const VideoCaptureFormat& format,
      base::TimeTicks reference_time,
      base::TimeDelta timestamp,
      std::optional<base::TimeTicks> capture_begin_timestamp,
      const std::optional<VideoFrameMetadata>& metadata) override;
  void OnIncomingCapturedBufferExt(
      Buffer buffer,
      const VideoCaptureFormat& format,
      const gfx::ColorSpace& color_space,
      base::TimeTicks reference_time,
      base::TimeDelta timestamp,
      std::optional<base::TimeTicks> capture_begin_timestamp,
      gfx::Rect visible_rect,
      const std::optional<VideoFrameMetadata>& additional_metadata) override;
  void OnError(VideoCaptureError error,
               const base::Location& from_here,
               const std::string& reason) override;
  void OnFrameDropped(VideoCaptureFrameDropReason reason) override;
  void OnLog(const std::string& message) override;
  double GetBufferPoolUtilization() const override;
  void OnStarted() override;

 private:
  VideoCaptureDevice::Client::ReserveResult CreateReadyFrameFromExternalBuffer(
      CapturedExternalVideoBuffer buffer,
      base::TimeTicks reference_time,
      base::TimeDelta timestamp,
      std::optional<base::TimeTicks> capture_begin_timestamp,
      const gfx::Rect& visible_rect,
      const std::optional<VideoFrameMetadata>& metadata,
      ReadyFrameInBuffer* ready_buffer);

  // A branch of OnIncomingCapturedData for Y16 frame_format.pixel_format.
  void OnIncomingCapturedY16Data(
      const uint8_t* data,
      int length,
      const VideoCaptureFormat& frame_format,
      base::TimeTicks reference_time,
      base::TimeDelta timestamp,
      std::optional<base::TimeTicks> capture_begin_timestamp,
      const std::optional<VideoFrameMetadata>& metadata,
      int frame_feedback_id);

#if BUILDFLAG(ENABLE_VIDEO_EFFECTS)
  // media::mojom::VideoEffectsConfigurationObserver impl.
  void OnConfigurationChanged(
      media::mojom::VideoEffectsConfigurationPtr configuration) override;

  bool ShouldApplyVideoEffects() const;

  std::optional<VideoCaptureDevice::Client::Buffer> ReserveEffectsOutputBuffer(
      const VideoCaptureFormat& format,
      const int frame_feedback_id);

  void OnPostProcessDone(base::expected<PostProcessDoneInfo,
                                        video_effects::mojom::PostProcessError>
                             post_process_info_or_error);
#endif

  // The receiver to which we post events.
  const std::unique_ptr<VideoFrameReceiver> receiver_;
  std::vector<int> buffer_ids_known_by_receiver_;

#if BUILDFLAG(IS_CHROMEOS)
  VideoCaptureJpegDecoderFactoryCB optional_jpeg_decoder_factory_callback_;
  std::unique_ptr<VideoCaptureJpegDecoder> external_jpeg_decoder_;
  base::OnceClosure on_started_using_gpu_cb_;
#endif  // BUILDFLAG(IS_CHROMEOS)

  // The pool of shared-memory buffers used for capturing.
  const scoped_refptr<VideoCaptureBufferPool> buffer_pool_;

  VideoPixelFormat last_captured_pixel_format_;

#if BUILDFLAG(ENABLE_VIDEO_EFFECTS)
  bool has_active_effects_ = false;
  scoped_refptr<base::SequencedTaskRunner> effects_processor_task_runner_;
  std::unique_ptr<VideoCaptureEffectsProcessor> effects_processor_;
  mojo::Remote<media::mojom::ReadonlyVideoEffectsManager>
      readonly_effects_manager_remote_;
  mojo::Receiver<media::mojom::VideoEffectsConfigurationObserver>
      effects_configuration_observer_{this};
#endif  // !BUILDFLAG(ENABLE_VIDEO_EFFECTS)

  // Thread collision warner to ensure that producer-facing API is not called
  // concurrently. Producers are allowed to call from multiple threads, but not
  // concurrently.
  DFAKE_MUTEX(call_from_producer_);

#if BUILDFLAG(ENABLE_VIDEO_EFFECTS)
  // Must be last:
  base::WeakPtrFactory<VideoCaptureDeviceClient> weak_ptr_factory_{this};
#endif
};

}  // namespace media

#endif  // MEDIA_CAPTURE_VIDEO_VIDEO_CAPTURE_DEVICE_CLIENT_H_
