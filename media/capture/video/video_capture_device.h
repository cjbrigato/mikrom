// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// VideoCaptureDevice is the abstract base class for realizing video capture
// device support in Chromium. It provides the interface for OS dependent
// implementations.
// The class is created and functions are invoked on a thread owned by
// VideoCaptureManager. Capturing is done on other threads, depending on the OS
// specific implementation.

#ifndef MEDIA_CAPTURE_VIDEO_VIDEO_CAPTURE_DEVICE_H_
#define MEDIA_CAPTURE_VIDEO_VIDEO_CAPTURE_DEVICE_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>

#include "base/files/file.h"
#include "base/functional/callback.h"
#include "base/memory/unsafe_shared_memory_region.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/token.h"
#include "build/build_config.h"
#include "media/base/video_frame.h"
#include "media/capture/capture_export.h"
#include "media/capture/mojom/image_capture.mojom.h"
#include "media/capture/mojom/video_capture_types.mojom.h"
#include "media/capture/video/video_capture_buffer_handle.h"
#include "media/capture/video/video_capture_device_descriptor.h"
#include "media/capture/video/video_capture_feedback.h"
#include "media/capture/video_capture_types.h"
#include "ui/gfx/gpu_memory_buffer_handle.h"

#if BUILDFLAG(IS_WIN)
#include <mfobjects.h>
#include <wrl/client.h>
#endif

namespace base {
class Location;
}  // namespace base

namespace gpu {
class ClientSharedImage;
}

namespace media {

class CAPTURE_EXPORT VideoFrameConsumerFeedbackObserver {
 public:
  virtual ~VideoFrameConsumerFeedbackObserver() {}

  // During processing of a video frame, consumers may report back their
  // utilization level to the source device. The device may use this information
  // to adjust the rate of data it pushes out. Values are interpreted as
  // follows:
  // Less than 0.0 is meaningless and should be ignored.  1.0 indicates a
  // maximum sustainable utilization.  Greater than 1.0 indicates the consumer
  // is likely to stall or drop frames if the data volume is not reduced.
  //
  // Example: In a system that encodes and transmits video frames over the
  // network, this value can be used to indicate whether sufficient CPU
  // is available for encoding and/or sufficient bandwidth is available for
  // transmission over the network.  The maximum of the two utilization
  // measurements would be used as feedback.
  virtual void OnUtilizationReport(media::VideoCaptureFeedback feedback) {}
};

struct CAPTURE_EXPORT CapturedExternalVideoBuffer {
  CapturedExternalVideoBuffer(gfx::GpuMemoryBufferHandle handle,
                              VideoCaptureFormat format,
                              gfx::ColorSpace color_space);

#if BUILDFLAG(IS_WIN)
  CapturedExternalVideoBuffer(Microsoft::WRL::ComPtr<IMFMediaBuffer> imf_buffer,
                              gfx::GpuMemoryBufferHandle handle,
                              VideoCaptureFormat format,
                              gfx::ColorSpace color_space);
#endif

  CapturedExternalVideoBuffer(CapturedExternalVideoBuffer&& other);
  CapturedExternalVideoBuffer& operator=(CapturedExternalVideoBuffer&& other);

  CapturedExternalVideoBuffer(const CapturedExternalVideoBuffer&) = delete;
  CapturedExternalVideoBuffer& operator=(const CapturedExternalVideoBuffer&) =
      delete;

  ~CapturedExternalVideoBuffer();

#if BUILDFLAG(IS_WIN)
  Microsoft::WRL::ComPtr<IMFMediaBuffer> imf_buffer;
#endif
  gfx::GpuMemoryBufferHandle handle;
  VideoCaptureFormat format;
  gfx::ColorSpace color_space;
};

class CAPTURE_EXPORT VideoCaptureDevice
    : public VideoFrameConsumerFeedbackObserver {
 public:
  // Interface defining the methods that clients of VideoCapture must have. It
  // is actually two-in-one: clients may implement OnIncomingCapturedData() or
  // ReserveOutputBuffer() + OnIncomingCapturedVideoFrame(), or all of them.
  // All methods may be called as soon as AllocateAndStart() of the
  // corresponding VideoCaptureDevice is invoked. The methods for buffer
  // reservation and frame delivery may be called from arbitrary threads but
  // are guaranteed to be called non-concurrently. The status reporting methods
  // (OnStarted, OnLog, OnError) may be called concurrently.
  class CAPTURE_EXPORT Client {
   public:
    // Struct bundling several parameters being passed between a
    // VideoCaptureDevice and its VideoCaptureDevice::Client.
    struct CAPTURE_EXPORT Buffer {
     public:
      // Destructor-only interface for encapsulating scoped access permission to
      // a Buffer.
      class CAPTURE_EXPORT ScopedAccessPermission {
       public:
        virtual ~ScopedAccessPermission() {}
      };

      class CAPTURE_EXPORT HandleProvider {
       public:
        virtual ~HandleProvider() {}

        // Duplicate as an writable (unsafe) shared memory region.
        virtual base::UnsafeSharedMemoryRegion DuplicateAsUnsafeRegion() = 0;

        // Access a |VideoCaptureBufferHandle| for local, writable memory.
        virtual std::unique_ptr<VideoCaptureBufferHandle>
        GetHandleForInProcessAccess() = 0;

        // Clone a |GpuMemoryBufferHandle| for IPC.
        virtual gfx::GpuMemoryBufferHandle GetGpuMemoryBufferHandle() = 0;
      };

      Buffer();
      Buffer(int buffer_id,
             int frame_feedback_id,
             std::unique_ptr<HandleProvider> handle_provider,
             std::unique_ptr<ScopedAccessPermission> access_permission);
      ~Buffer();
      Buffer(Buffer&& other);
      Buffer& operator=(Buffer&& other);

      int id;
      int frame_feedback_id;
      std::unique_ptr<HandleProvider> handle_provider;
      std::unique_ptr<ScopedAccessPermission> access_permission;

      // Some buffer types may be preemptively mapped in the capturer if
      // requested by the consumer.
      // This is used to notify the client that a shared memory region
      // associated with the buffer is valid.
      bool is_premapped = false;
    };

    // Result code for calls to ReserveOutputBuffer()
    enum class ReserveResult {
      kSucceeded,
      kMaxBufferCountExceeded,
      kAllocationFailed
    };

    virtual ~Client() {}

    // The configuration of the VideoCaptureDevice has changed.
    virtual void OnCaptureConfigurationChanged() = 0;

    // Captured a new video frame, data for which is pointed to by |data|.
    //
    // The format of the frame is described by |frame_format|, and is assumed to
    // be tightly packed. This method will try to reserve an output buffer and
    // copy from |data| into the output buffer. If no output buffer is
    // available, the frame will be silently dropped. |reference_time| is
    // system clock time when we detect the capture happens, it is used for
    // Audio/Video sync, not an exact presentation time for playout, because it
    // could contain noise. |timestamp| measures the ideal time span between the
    // first frame in the stream and the current frame; however, the time source
    // is determined by the platform's device driver and is often not the system
    // clock, or even has a drift with respect to system clock.
    // |frame_feedback_id| is an identifier that allows clients to refer back to
    // this particular frame when reporting consumer feedback via
    // OnConsumerReportingUtilization(). This identifier is needed because
    // frames are consumed asynchronously and multiple frames can be "in flight"
    // at the same time.
    virtual void OnIncomingCapturedData(
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
        int frame_feedback_id) = 0;
    // Convenience wrapper that passes in 0 as |frame_feedback_id|.
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
        const std::optional<VideoFrameMetadata>& metadata);

    // Captured a new video frame, data for which is stored in the
    // shared image pointed to by |shared_image|.  The format of the frame is
    // described by |frame_format|.  Since the shared image pointed to by
    // |shared_image| may be allocated with some size/address alignment
    // requirement, this method takes into consideration the size and offset of
    // each plane in |shared_image| when creating the content of the output
    // buffer. |clockwise_rotation|, |reference_time|, |timestamp|, and
    // |frame_feedback_id| serve the same purposes as in OnIncomingCapturedData.
    virtual void OnIncomingCapturedImage(
        scoped_refptr<gpu::ClientSharedImage> shared_image,
        const VideoCaptureFormat& frame_format,
        int clockwise_rotation,
        base::TimeTicks reference_time,
        base::TimeDelta timestamp,
        std::optional<base::TimeTicks> capture_begin_timestamp,
        const std::optional<VideoFrameMetadata>& metadata,
        int frame_feedback_id) = 0;
    // Convenience wrapper that passes in 0 as |frame_feedback_id|.
    void OnIncomingCapturedImage(
        scoped_refptr<gpu::ClientSharedImage> shared_image,
        const VideoCaptureFormat& frame_format,
        int clockwise_rotation,
        base::TimeTicks reference_time,
        base::TimeDelta timestamp,
        std::optional<base::TimeTicks> capture_begin_timestamp,
        const std::optional<VideoFrameMetadata>& metadata);

    // Captured a new video frame. The data for this frame is in
    // |buffer.handle|, which is owned by the platform-specific capture device.
    // It is the responsibility of the implementation to prevent the buffer in
    // |buffer.handle| from being reused by the external capturer. In practice,
    // this is used only on macOS, the external capturer maintains a
    // CVPixelBufferPool, and gfx::ScopedInUseIOSurface is used to prevent reuse
    // of buffers until all consumers have consumed them. |visible_rect|
    // specifies the region in the memory pointed to by |buffer.handle| that
    // contains the captured content. |metadata| is used for storing an initial
    // metadata and if not provided, then the initial metadata used would be the
    // default values of VideoFrameMetadata.
    virtual void OnIncomingCapturedExternalBuffer(
        CapturedExternalVideoBuffer buffer,
        base::TimeTicks reference_time,
        base::TimeDelta timestamp,
        std::optional<base::TimeTicks> capture_begin_timestamp,
        const gfx::Rect& visible_rect,
        const std::optional<VideoFrameMetadata>& metadata) = 0;

    // Reserve an output buffer into which contents can be captured directly.
    // The returned |buffer| will always be allocated with a memory size
    // suitable for holding a packed video frame with pixels of |format| format,
    // of |dimensions| frame dimensions. It is permissible for |dimensions| to
    // be zero; in which case the returned Buffer does not guarantee memory
    // backing, but functions as a reservation for external input for the
    // purposes of buffer throttling. |require_new_buffer_id| and
    // |retire_old_buffer_id| returns the NEW and/or RETIRED buffer id which
    // needs to notify the buffer pool synchronizer to register or retire the
    // buffer object.
    //
    // The buffer stays reserved for use by the caller as long as it
    // holds on to the contained |buffer_read_write_permission|.
    [[nodiscard]] virtual ReserveResult ReserveOutputBuffer(
        const gfx::Size& dimensions,
        VideoPixelFormat format,
        int frame_feedback_id,
        Buffer* buffer,
        int* require_new_buffer_id,
        int* retire_old_buffer_id) = 0;

    // Provides VCD::Client with a populated Buffer containing the content of
    // the next video frame. The |buffer| must originate from an earlier call to
    // ReserveOutputBuffer().
    // See OnIncomingCapturedData for details of |reference_time| and
    // |timestamp|.
    virtual void OnIncomingCapturedBuffer(
        Buffer buffer,
        const VideoCaptureFormat& format,
        base::TimeTicks reference_time,
        base::TimeDelta timestamp,
        std::optional<base::TimeTicks> capture_begin_timestamp,
        const std::optional<VideoFrameMetadata>& metadata) = 0;

    // Extended version of OnIncomingCapturedBuffer() allowing clients to
    // pass a custom |visible_rect| and |additional_metadata|.
    virtual void OnIncomingCapturedBufferExt(
        Buffer buffer,
        const VideoCaptureFormat& format,
        const gfx::ColorSpace& color_space,
        base::TimeTicks reference_time,
        base::TimeDelta timestamp,
        std::optional<base::TimeTicks> capture_begin_timestamp,
        gfx::Rect visible_rect,
        const std::optional<VideoFrameMetadata>& additional_metadata) = 0;

    // An error has occurred that cannot be handled and VideoCaptureDevice must
    // be StopAndDeAllocate()-ed. |reason| is a text description of the error.
    virtual void OnError(VideoCaptureError error,
                         const base::Location& from_here,
                         const std::string& reason) = 0;

    virtual void OnFrameDropped(VideoCaptureFrameDropReason reason) = 0;

    // VideoCaptureDevice requests the |message| to be logged.
    virtual void OnLog(const std::string& message) {}

    // Returns the current buffer pool utilization, in the range 0.0 (no buffers
    // are in use by producers or consumers) to 1.0 (all buffers are in use).
    virtual double GetBufferPoolUtilization() const = 0;

    // VideoCaptureDevice reports it's successfully started.
    virtual void OnStarted() = 0;
  };

  ~VideoCaptureDevice() override;

  // Prepares the video capturer for use. StopAndDeAllocate() must be called
  // before the object is deleted.
  virtual void AllocateAndStart(const VideoCaptureParams& params,
                                std::unique_ptr<Client> client) = 0;

  // In cases where the video capturer self-pauses (e.g., a screen capturer
  // where the screen's content has not changed in a while), consumers may call
  // this to request a "refresh frame" be delivered to the Client.  This is used
  // in a number of circumstances, such as:
  //
  //   1. An additional consumer of video frames is starting up and requires a
  //      first frame (as opposed to not receiving a frame for an indeterminate
  //      amount of time).
  //   2. A few repeats of the same frame would allow a lossy video encoder to
  //      improve the video quality of unchanging content.
  //
  // The default implementation is a no-op. VideoCaptureDevice implementations
  // are not required to honor this request, especially if they do not
  // self-pause and/or if honoring the request would cause them to exceed their
  // configured maximum frame rate. Any VideoCaptureDevice that does self-pause,
  // however, should provide an implementation of this method that makes
  // reasonable attempts to honor these requests.
  //
  // Note: This should only be called after AllocateAndStart() and before
  // StopAndDeAllocate(). Otherwise, its behavior is undefined.
  virtual void RequestRefreshFrame() {}

  // Optionally suspends frame delivery. The VideoCaptureDevice may or may not
  // honor this request. Thus, the caller cannot assume frame delivery will
  // actually stop. Even if frame delivery is suspended, this might not take
  // effect immediately.
  //
  // The purpose of this is to quickly place the device into a state where it's
  // resource utilization is minimized while there are no frame consumers; and
  // then quickly resume once a frame consumer is present.
  //
  // Note: This should only be called after AllocateAndStart() and before
  // StopAndDeAllocate(). Otherwise, its behavior is undefined.
  virtual void MaybeSuspend() {}

  // Resumes frame delivery, if it was suspended. If frame delivery was not
  // suspended, this is a no-op, and frame delivery will continue.
  //
  // Note: This should only be called after AllocateAndStart() and before
  // StopAndDeAllocate(). Otherwise, its behavior is undefined.
  virtual void Resume() {}

  // Start/stop cropping or restricting a tab-caputre video track.
  //
  // Non-empty |target| sets (or changes) the target, and |type| determines
  // which type of sub-capture mutation is expected.
  //
  // Empty |target| reverts the capture to its original state.
  // In that case, |type| is not generally useful, and is ignored. It can
  // be expected to match the method called from JS - cropTo() or restrictTo().
  //
  // |sub_capture_target_version| must be incremented by at least one for each
  // call. By including it in frame's metadata, Viz informs Blink what was the
  // latest invocation of cropTo() or restrictTo() before a given frame was
  // produced.
  //
  // The callback reports success/failure. It is called on an unspecified
  // thread, it's the caller's responsibility to wrap it (i.e. via BindPostTask)
  // as needed.
  virtual void ApplySubCaptureTarget(
      mojom::SubCaptureTargetType type,
      const base::Token& target,
      uint32_t sub_capture_target_version,
      base::OnceCallback<void(media::mojom::ApplySubCaptureTargetResult)>
          callback);

  // Deallocates the video capturer, possibly asynchronously.
  //
  // This call requires the device to do the following things, eventually: put
  // hardware into a state where other applications could use it, free the
  // memory associated with capture, and delete the |client| pointer passed into
  // AllocateAndStart.
  //
  // If deallocation is done asynchronously, then the device implementation must
  // ensure that a subsequent AllocateAndStart() operation targeting the same ID
  // would be sequenced through the same task runner, so that deallocation
  // happens first.
  virtual void StopAndDeAllocate() = 0;

  // Retrieve the photo capabilities and settings of the device (e.g. zoom
  // levels etc). On success, invokes |callback|. On failure, drops callback
  // without invoking it.
  using GetPhotoStateCallback = base::OnceCallback<void(mojom::PhotoStatePtr)>;
  virtual void GetPhotoState(GetPhotoStateCallback callback);

  // On success, invokes |callback| with value |true|. On failure, drops
  // callback without invoking it.
  using SetPhotoOptionsCallback = base::OnceCallback<void(bool)>;
  virtual void SetPhotoOptions(mojom::PhotoSettingsPtr settings,
                               SetPhotoOptionsCallback callback);

  // Asynchronously takes a photo, possibly reconfiguring the capture objects
  // and/or interrupting the capture flow. Runs |callback|, if the photo was
  // successfully taken. On failure, drops callback without invoking it.
  // Note that |callback| may be run on a thread different than the thread
  // where TakePhoto() was called.
  using TakePhotoCallback = base::OnceCallback<void(mojom::BlobPtr blob)>;
  virtual void TakePhoto(TakePhotoCallback callback);

  // Gets the power line frequency, either from the params if specified by the
  // user or from the current system time zone.
  static PowerLineFrequency GetPowerLineFrequency(
      const VideoCaptureParams& params);

 private:
  // Gets the power line frequency from the current system time zone if this is
  // defined, otherwise returns 0.
  static PowerLineFrequency GetPowerLineFrequencyForLocation();
};

VideoCaptureFrameDropReason ConvertReservationFailureToFrameDropReason(
    VideoCaptureDevice::Client::ReserveResult reserve_result);

}  // namespace media

#endif  // MEDIA_CAPTURE_VIDEO_VIDEO_CAPTURE_DEVICE_H_
