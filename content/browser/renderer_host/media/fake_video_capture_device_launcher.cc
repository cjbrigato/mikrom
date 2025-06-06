// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/media/fake_video_capture_device_launcher.h"

#include <memory>
#include <optional>

#include "base/functional/bind.h"
#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/single_thread_task_runner.h"
#include "base/token.h"
#include "content/public/browser/browser_context.h"
#include "media/capture/mojom/video_capture_types.mojom.h"
#include "media/capture/video/video_capture_buffer_pool_impl.h"
#include "media/capture/video/video_capture_buffer_tracker_factory_impl.h"
#include "media/capture/video/video_capture_device_client.h"
#include "media/capture/video/video_frame_receiver_on_task_runner.h"
#include "services/video_effects/public/cpp/buildflags.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "media/capture/video/chromeos/video_capture_jpeg_decoder.h"
#endif  // BUILDFLAG(IS_CHROMEOS)

namespace {

class FakeLaunchedVideoCaptureDevice
    : public content::LaunchedVideoCaptureDevice {
 public:
  explicit FakeLaunchedVideoCaptureDevice(
      std::unique_ptr<media::VideoCaptureDevice> device)
      : device_(std::move(device)) {}

  ~FakeLaunchedVideoCaptureDevice() override { device_->StopAndDeAllocate(); }

  void GetPhotoState(
      media::VideoCaptureDevice::GetPhotoStateCallback callback) override {
    device_->GetPhotoState(std::move(callback));
  }
  void SetPhotoOptions(
      media::mojom::PhotoSettingsPtr settings,
      media::VideoCaptureDevice::SetPhotoOptionsCallback callback) override {
    device_->SetPhotoOptions(std::move(settings), std::move(callback));
  }
  void TakePhoto(
      media::VideoCaptureDevice::TakePhotoCallback callback) override {
    device_->TakePhoto(std::move(callback));
  }
  void MaybeSuspendDevice() override { device_->MaybeSuspend(); }
  void ResumeDevice() override { device_->Resume(); }
  void ApplySubCaptureTarget(
      media::mojom::SubCaptureTargetType type,
      const base::Token& target,
      uint32_t sub_capture_target_version,
      base::OnceCallback<void(media::mojom::ApplySubCaptureTargetResult)>
          callback) override {
    device_->ApplySubCaptureTarget(type, target, sub_capture_target_version,
                                   std::move(callback));
  }
  void RequestRefreshFrame() override { device_->RequestRefreshFrame(); }
  void SetDesktopCaptureWindowIdAsync(gfx::NativeViewId window_id,
                                      base::OnceClosure done_cb) override {
    // Do nothing.
  }
  void OnUtilizationReport(media::VideoCaptureFeedback feedback) override {
    device_->OnUtilizationReport(feedback);
  }

 private:
  std::unique_ptr<media::VideoCaptureDevice> device_;
};

}  // anonymous namespace

namespace content {

FakeVideoCaptureDeviceLauncher::FakeVideoCaptureDeviceLauncher(
    media::VideoCaptureSystem* system)
    : system_(system) {
  DCHECK(system_);
}

FakeVideoCaptureDeviceLauncher::~FakeVideoCaptureDeviceLauncher() = default;

void FakeVideoCaptureDeviceLauncher::LaunchDeviceAsync(
    const std::string& device_id,
    blink::mojom::MediaStreamType stream_type,
    const media::VideoCaptureParams& params,
    base::WeakPtr<media::VideoFrameReceiver> receiver,
    base::OnceClosure connection_lost_cb,
    Callbacks* callbacks,
    base::OnceClosure done_cb,
#if BUILDFLAG(ENABLE_VIDEO_EFFECTS)
    mojo::PendingRemote<video_effects::mojom::VideoEffectsProcessor>
        video_effects_processor,
#endif
    mojo::PendingRemote<media::mojom::ReadonlyVideoEffectsManager>
        readonly_video_effects_manager) {
  auto device = system_->CreateDevice(device_id).ReleaseDevice();
#if BUILDFLAG(IS_WIN)
  auto buffer_pool = base::MakeRefCounted<media::VideoCaptureBufferPoolImpl>(
      params.buffer_type, 10,
      std::make_unique<media::VideoCaptureBufferTrackerFactoryImpl>(
          system_->GetFactory()->GetDxgiDeviceManager()));
#else
  auto buffer_pool = base::MakeRefCounted<media::VideoCaptureBufferPoolImpl>(
      media::VideoCaptureBufferType::kSharedMemory);
#endif  // BUILDFLAG(IS_WIN)
#if BUILDFLAG(IS_CHROMEOS)
  auto device_client = std::make_unique<media::VideoCaptureDeviceClient>(
      std::make_unique<media::VideoFrameReceiverOnTaskRunner>(
          receiver, base::SingleThreadTaskRunner::GetCurrentDefault()),
      std::move(buffer_pool), base::BindRepeating([]() {
        return std::unique_ptr<media::VideoCaptureJpegDecoder>();
      }));
#else
  auto device_client = std::make_unique<media::VideoCaptureDeviceClient>(
      std::make_unique<media::VideoFrameReceiverOnTaskRunner>(
          receiver, base::SingleThreadTaskRunner::GetCurrentDefault()),
      std::move(buffer_pool), std::nullopt);
#endif  // BUILDFLAG(IS_CHROMEOS)
  device->AllocateAndStart(params, std::move(device_client));
  auto launched_device =
      std::make_unique<FakeLaunchedVideoCaptureDevice>(std::move(device));
  callbacks->OnDeviceLaunched(std::move(launched_device));
}

void FakeVideoCaptureDeviceLauncher::AbortLaunch() {
  // Do nothing.
}

}  // namespace content
