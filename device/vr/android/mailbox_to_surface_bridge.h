// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_VR_ANDROID_MAILBOX_TO_SURFACE_BRIDGE_H_
#define DEVICE_VR_ANDROID_MAILBOX_TO_SURFACE_BRIDGE_H_

#include "base/functional/callback_forward.h"
#include "gpu/command_buffer/common/shared_image_usage.h"

namespace gfx {
enum class BufferFormat : uint8_t;
class ColorSpace;
class GpuFence;
struct GpuMemoryBufferHandle;
class Size;
}  // namespace gfx

namespace gpu {
class ClientSharedImage;
struct SyncToken;
}  // namespace gpu

namespace device {
class MailboxToSurfaceBridge {
 public:
  virtual ~MailboxToSurfaceBridge() {}

  // Returns true if the GPU process connection is established and ready to use.
  // Equivalent to waiting for on_initialized to be called.
  virtual bool IsConnected() = 0;

  // Asynchronously creates and binds context provider to the current thread
  // (making it the GL thread) and calls the callback on the GL thread.
  virtual void CreateAndBindContextProvider(base::OnceClosure callback) = 0;

  // All other public methods below must be called on the GL thread
  // (except when marked otherwise).

  virtual void GenSyncToken(gpu::SyncToken* out_sync_token) = 0;

  virtual void WaitSyncToken(const gpu::SyncToken& sync_token) = 0;

  // Copies a GpuFence from the local context to the GPU process,
  // and issues a server wait for it.
  virtual void WaitForClientGpuFence(gfx::GpuFence&) = 0;

  // Creates a GpuFence in the GPU process after the supplied sync_token
  // completes, and copies it for use in the local context. This is
  // asynchronous, the callback receives the GpuFence once it's available.
  virtual void CreateGpuFence(
      const gpu::SyncToken& sync_token,
      base::OnceCallback<void(std::unique_ptr<gfx::GpuFence>)> callback) = 0;

  // Creates a shared image bound to |buffer_handle|. Returns the shared image
  // and populates |sync_token|.
  // Caller must call DestroySharedImage to free the shared image. Backing keeps
  // reference to the |buffer_handle|.
  virtual scoped_refptr<gpu::ClientSharedImage> CreateSharedImage(
      gfx::GpuMemoryBufferHandle buffer_handle,
      gfx::BufferFormat buffer_format,
      const gfx::Size& size,
      const gfx::ColorSpace& color_space,
      gpu::SharedImageUsageSet usage,
      gpu::SyncToken& sync_token) = 0;

  // Destroys a shared image created by CreateSharedImage. The sync_token
  // argument must have been updated to a sync token after the last use of the
  // shared image.
  virtual void DestroySharedImage(
      const gpu::SyncToken& sync_token,
      scoped_refptr<gpu::ClientSharedImage> shared_image) = 0;
};

class MailboxToSurfaceBridgeFactory {
 public:
  virtual ~MailboxToSurfaceBridgeFactory() {}

  virtual std::unique_ptr<device::MailboxToSurfaceBridge> Create() const = 0;
};

}  // namespace device

#endif  // DEVICE_VR_ANDROID_MAILBOX_TO_SURFACE_BRIDGE_H_
