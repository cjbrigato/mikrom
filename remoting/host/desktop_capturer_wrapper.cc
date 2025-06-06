// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/desktop_capturer_wrapper.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "base/check.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/notimplemented.h"
#include "base/threading/thread_checker.h"
#include "build/build_config.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capturer.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_frame.h"
#include "third_party/webrtc/modules/desktop_capture/shared_memory.h"

#if defined(WEBRTC_USE_GIO)
#include "base/notreached.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capture_metadata.h"
#endif

namespace remoting {

DesktopCapturerWrapper::DesktopCapturerWrapper() {
  DETACH_FROM_THREAD(thread_checker_);
}

DesktopCapturerWrapper::~DesktopCapturerWrapper() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
}

void DesktopCapturerWrapper::CreateCapturer(
    base::OnceCallback<std::unique_ptr<webrtc::DesktopCapturer>()> creator) {
  DCHECK(!capturer_);

  capturer_ = std::move(creator).Run();

  if (!capturer_) {
    LOG(ERROR) << "Failed to initialize screen capturer.";
  }
}

void DesktopCapturerWrapper::Start(Callback* callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(!callback_);

  callback_ = callback;

  if (capturer_) {
    capturer_->Start(this);
  }
}

void DesktopCapturerWrapper::SetSharedMemoryFactory(
    std::unique_ptr<webrtc::SharedMemoryFactory> shared_memory_factory) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (capturer_) {
    capturer_->SetSharedMemoryFactory(std::move(shared_memory_factory));
  }
}

void DesktopCapturerWrapper::CaptureFrame() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (capturer_) {
    capturer_->CaptureFrame();
  } else {
    OnCaptureResult(webrtc::DesktopCapturer::Result::ERROR_PERMANENT, nullptr);
  }
}

bool DesktopCapturerWrapper::GetSourceList(SourceList* sources) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  NOTIMPLEMENTED();
  return false;
}

bool DesktopCapturerWrapper::SelectSource(SourceId id) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (capturer_) {
    return capturer_->SelectSource(id);
  }
  return false;
}

void DesktopCapturerWrapper::OnFrameCaptureStart() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  callback_->OnFrameCaptureStart();
}

void DesktopCapturerWrapper::OnCaptureResult(
    webrtc::DesktopCapturer::Result result,
    std::unique_ptr<webrtc::DesktopFrame> frame) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  callback_->OnCaptureResult(result, std::move(frame));
}

void DesktopCapturerWrapper::SetMaxFrameRate(std::uint32_t max_frame_rate) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (capturer_) {
    capturer_->SetMaxFrameRate(max_frame_rate);
  }
}

#if defined(WEBRTC_USE_GIO)
void DesktopCapturerWrapper::GetMetadataAsync(
    base::OnceCallback<void(webrtc::DesktopCaptureMetadata)> callback) {
  NOTREACHED() << "Use DesktopCapturerProxy instead!";
}
#endif

}  // namespace remoting
