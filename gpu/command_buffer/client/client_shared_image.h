// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_CLIENT_CLIENT_SHARED_IMAGE_H_
#define GPU_COMMAND_BUFFER_CLIENT_CLIENT_SHARED_IMAGE_H_

#include <optional>

#include "base/containers/span.h"
#include "base/feature_list.h"
#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/unsafe_shared_memory_pool.h"
#include "base/task/single_thread_task_runner.h"
#include "gpu/command_buffer/client/gpu_memory_buffer_manager.h"
#include "gpu/command_buffer/client/shared_image_interface.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "gpu/command_buffer/common/shared_image_trace_utils.h"
#include "gpu/gpu_export.h"
#include "gpu/ipc/common/exported_shared_image.mojom-shared.h"
#include "gpu/ipc/common/gpu_memory_buffer_handle_info.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkPixmap.h"
#include "ui/gfx/color_space.h"
#include "ui/gfx/gpu_memory_buffer.h"

namespace media {
class VideoFrame;
}  // namespace media

namespace gpu {

namespace gles2 {
class GLES2Interface;
}  // namespace gles2

class ClientSharedImageInterface;
class GpuChannelSharedImageInterface;
class InterfaceBase;
class RasterScopedAccess;
class SharedImageTexture;
class TestSharedImageInterface;

struct ExportedSharedImage;

class GPU_EXPORT ClientSharedImage
    : public base::RefCountedThreadSafe<ClientSharedImage> {
 public:
  // Provides access to the CPU visible memory for the SharedImage if it is
  // being used for CPU READ/WRITE and underlying resource(native buffers/shared
  // memory) is CPU mappable. Memory and strides can be requested for each
  // plane.
  class GPU_EXPORT ScopedMapping {
   public:
    virtual ~ScopedMapping() = default;

    virtual base::span<uint8_t> GetMemoryForPlane(
        const uint32_t plane_index) = 0;

    SkPixmap GetSkPixmapForPlane(const uint32_t plane_index,
                                 SkImageInfo sk_image_info);

    // Returns plane stride.
    virtual size_t Stride(const uint32_t plane_index) = 0;

    // Returns the size of the buffer.
    virtual gfx::Size Size() = 0;

    // Returns BufferFormat.
    virtual gfx::BufferFormat Format() = 0;

    // Returns whether the underlying resource is shared memory.
    virtual bool IsSharedMemory() = 0;

   private:
    friend class ClientSharedImage;

    static std::unique_ptr<ScopedMapping> Create(
        SharedImageMetadata metadata_,
        base::WritableSharedMemoryMapping* mapping);
    static std::unique_ptr<ScopedMapping> Create(
        gfx::GpuMemoryBuffer* gpu_memory_buffer,
        bool is_already_mapped);
    static void StartCreateAsync(
        gfx::GpuMemoryBuffer* gpu_memory_buffer,
        base::OnceCallback<void(std::unique_ptr<ScopedMapping>)> result_cb);
    static void FinishCreateAsync(
        gfx::GpuMemoryBuffer* gpu_memory_buffer,
        base::OnceCallback<void(std::unique_ptr<ScopedMapping>)> result_cb,
        bool success);
  };

  // `sii_holder` must not be null.
  ClientSharedImage(const Mailbox& mailbox,
                    const SharedImageMetadata& metadata,
                    const SyncToken& sync_token,
                    scoped_refptr<SharedImageInterfaceHolder> sii_holder,
                    gfx::GpuMemoryBufferType gmb_type);

  // `sii_holder` must not be null.
  ClientSharedImage(const Mailbox& mailbox,
                    const SharedImageMetadata& metadata,
                    const SyncToken& sync_token,
                    scoped_refptr<SharedImageInterfaceHolder> sii_holder,
                    base::WritableSharedMemoryMapping mapping);

  // `sii_holder` must not be null. |shared_memory_pool| can be null and is only
  // used on windows platform.
  ClientSharedImage(
      const Mailbox& mailbox,
      const SharedImageMetadata& metadata,
      const SyncToken& sync_token,
      GpuMemoryBufferHandleInfo handle_info,
      scoped_refptr<SharedImageInterfaceHolder> sii_holder,
      scoped_refptr<base::UnsafeSharedMemoryPool> shared_memory_pool = nullptr);

  const Mailbox& mailbox() { return mailbox_; }
  viz::SharedImageFormat format() const { return metadata_.format; }
  gfx::Size size() const { return metadata_.size; }
  gfx::ColorSpace color_space() const { return metadata_.color_space; }
  GrSurfaceOrigin surface_origin() const { return metadata_.surface_origin; }
  SkAlphaType alpha_type() const { return metadata_.alpha_type; }
  SharedImageUsageSet usage() { return metadata_.usage; }
  std::optional<gfx::BufferUsage> buffer_usage() { return buffer_usage_; }
  bool is_software() const { return is_software_; }

  bool HasHolder() { return sii_holder_ != nullptr; }

  // Returns a clone of the GpuMemoryBufferHandle associated with this ClientSI.
  // Valid to call only if this instance was created with a non-null
  // GpuMemoryBuffer.
  gfx::GpuMemoryBufferHandle CloneGpuMemoryBufferHandle() const {
    CHECK(gpu_memory_buffer_);
    return gpu_memory_buffer_->CloneHandle();
  }

#if BUILDFLAG(IS_APPLE)
  // Sets the color space in which the native buffer backing this SharedImage
  // should be interpreted when used as an overlay. Note that this will not
  // impact texturing from the buffer. Used only for SharedImages backed by a
  // client-accessible IOSurface.
  void SetColorSpaceOnNativeBuffer(const gfx::ColorSpace& color_space);
#endif

  // Returns the GL texture target to use for this SharedImage.
  uint32_t GetTextureTarget();

  base::trace_event::MemoryAllocatorDumpGuid GetGUIDForTracing() {
    return gpu::GetSharedImageGUIDForTracing(mailbox_);
  }

  // Maps |mailbox| into CPU visible memory and returns a ScopedMapping object
  // which can be used to read/write to the CPU mapped memory. The SharedImage
  // backing this ClientSI must have been created with CPU_READ/CPU_WRITE usage.
  std::unique_ptr<ScopedMapping> Map();

  // Maps |mailbox| into CPU visible memory and returns a ScopedMapping object
  // which can be used to read/write to the CPU mapped memory. The SharedImage
  // backing this ClientSI must have been created with CPU_READ/CPU_WRITE usage.
  // Default implementation is blocking. However, on some platforms, where
  // possible, the implementation is non-blocking and may execute the callback
  // on the GpuMemoryThread. But if no GPU work is necessary, it still may
  // execute the callback immediately in the current sequence. Note: `this` must
  // be kept alive until the result callback is executed.
  void MapAsync(
      base::OnceCallback<void(std::unique_ptr<ScopedMapping>)> result_cb);

  // Returns an unowned copy of the current ClientSharedImage. This function
  // is a temporary workaround for the situation where a ClientSharedImage may
  // have more than one reference when being destroyed.
  // TODO(crbug.com/40286368): Remove this function once ClientSharedImage
  // can properly handle shared image destruction internally.
  scoped_refptr<ClientSharedImage> MakeUnowned();

  ExportedSharedImage Export(bool with_buffer_handle = false);

  // Returns an unowned reference for the underlying shared image backing. The
  // caller should ensure that the original shared image backing created in
  // client process outlives this reference. Note that it is preferable to use
  // SharedImageInterface::ImportSharedImage() instead, which returns an owning
  // reference, where the underlying shared image backing stays alive in gpu
  // process even if original ClientSharedImage goes away.
  static scoped_refptr<ClientSharedImage> ImportUnowned(
      ExportedSharedImage exported_shared_image);

  void UpdateDestructionSyncToken(const gpu::SyncToken& sync_token) {
    destruction_sync_token_ = sync_token;
  }

  // Signals the service-side that the backing of this SharedImage was modified
  // on the CPU or through external devices. `sync_token` can be passed to order
  // the processing of the signal. Returns a SyncToken that the caller can use
  // to ensure that any future service-side accesses to this SharedImage are
  // sequenced with respect to this call being processed.
  gpu::SyncToken BackingWasExternallyUpdated(const gpu::SyncToken& sync_token);

  // Creates a ClientSharedImage that is not associated with any
  // SharedImageInterface for testing.
  static scoped_refptr<ClientSharedImage> CreateForTesting();
  static scoped_refptr<ClientSharedImage> CreateForTesting(
      viz::SharedImageFormat format,
      uint32_t texture_target);
  static scoped_refptr<ClientSharedImage> CreateForTesting(
      SharedImageUsageSet usage);
  static scoped_refptr<ClientSharedImage> CreateForTesting(
      const SharedImageMetadata& metadata,
      uint32_t texture_target);

  static scoped_refptr<ClientSharedImage> CreateForTesting(
      const Mailbox& mailbox,
      const SharedImageMetadata& metadata,
      const SyncToken& sync_token,
      std::unique_ptr<gfx::GpuMemoryBuffer> gpu_memory_buffer,
      gfx::BufferUsage buffer_usage,
      scoped_refptr<SharedImageInterfaceHolder> sii_holder) {
    auto client_si = base::MakeRefCounted<ClientSharedImage>(
        mailbox, metadata, sync_token, sii_holder,
        gpu_memory_buffer->GetType());
    client_si->gpu_memory_buffer_ = std::move(gpu_memory_buffer);
    client_si->buffer_usage_ = buffer_usage;
    return client_si;
  }

  const SyncToken& creation_sync_token() const { return creation_sync_token_; }

  void OnMemoryDump(
      base::trace_event::ProcessMemoryDump* pmd,
      const base::trace_event::MemoryAllocatorDumpGuid& buffer_dump_guid,
      int importance);

  // Creates a GL Texture from the current SharedImage for the provided
  // GLES2Interface.
  std::unique_ptr<SharedImageTexture> CreateGLTexture(
      gles2::GLES2Interface* gl);

  // Creates a RasterScopedAccess object from the current SharedImage for the
  // provided raster interface.
  std::unique_ptr<RasterScopedAccess> BeginRasterAccess(
      InterfaceBase* raster_interface,
      const SyncToken& sync_token,
      bool readonly);

#if BUILDFLAG(IS_WIN)
  // Allows client to indicate the |gpu_memory_buffer_| to pre map its shared
  // memory region internally for performance optimization purposes. It is only
  // used on windows.
  void SetUsePreMappedMemory(bool use_premapped_memory);
#endif

 private:
  friend class base::RefCountedThreadSafe<ClientSharedImage>;
  friend class SharedImageTexture;
  ~ClientSharedImage();

  // Helper class that implements the GpuMemoryBufferManager interface.
  // Note that this is primarily needed for transition to MappableSI where some
  // clients will be using GpuMemoryBufferManager and some will want to use SII
  // instead.
  // TODO(crbug.com/368562234): Once all the clients and tests using
  // GpuMemoryBufferManager are converted to use MappableSI,
  // GpuMemoryBufferManager and all  its implementations might be removed
  // including this.
  class HelperGpuMemoryBufferManager : public gpu::GpuMemoryBufferManager {
   public:
    explicit HelperGpuMemoryBufferManager(
        ClientSharedImage* client_shared_image);

    ~HelperGpuMemoryBufferManager() override;

    // GpuMemoryBufferManager interface implementation.
    // This method should not be used via this interface. Hence marking it as
    // NOTREACHED.
    std::unique_ptr<gfx::GpuMemoryBuffer> CreateGpuMemoryBuffer(
        const gfx::Size& size,
        gfx::BufferFormat format,
        gfx::BufferUsage usage,
        gpu::SurfaceHandle surface_handle,
        base::WaitableEvent* shutdown_event) final;

    void CopyGpuMemoryBufferAsync(
        gfx::GpuMemoryBufferHandle buffer_handle,
        base::UnsafeSharedMemoryRegion memory_region,
        base::OnceCallback<void(bool)> callback) final;

    bool IsConnected() final;

   private:
    // Points to the parent ClientSharedImage. It will be used to access SII via
    // SII holder.
    raw_ptr<ClientSharedImage> client_shared_image_;

    // Allows accessing SharedImageInterface from ClientSharedImage.
    scoped_refptr<SharedImageInterface> GetSharedImageInterface();

    // HelperGpuMemoryBufferManager uses this task runner for
    // CopyGpuMemoryBufferAsync() operations to prevent deadlocks.
    //
    // Deadlock Scenario:
    // 1. Client thread calls CopyGpuMemoryBufferAsync() with a completion
    // callback.
    // 2. Client thread blocks, waiting for an event which is often signaled by
    // the callback.
    // 3. If the copy ran on the client thread, the callback would also need to
    // run on the *same*, now-blocked thread.
    // 4. The callback can't run, the event isn't signaled, and a deadlock
    // occurs.
    //
    // Solution:
    // This dedicated task runner ensures the copy and callback execute
    // independently of the client thread, allowing the callback to signal the
    // event and prevent the deadlock.
    std::optional<scoped_refptr<base::SingleThreadTaskRunner>> task_runner_;
  };

  // This constructor is used only when importing an owned ClientSharedImage,
  // which should only be done via implementations of
  // SharedImageInterface::ImportSharedImage().
  // `sii_holder` must not be null.
  friend class ClientSharedImageInterface;
  friend class GpuChannelSharedImageInterface;
  friend class RasterScopedAccess;
  friend class TestSharedImageInterface;
  friend class media::VideoFrame;
  ClientSharedImage(const Mailbox& mailbox,
                    const SharedImageMetadata& metadata,
                    const SyncToken& sync_token,
                    scoped_refptr<SharedImageInterfaceHolder> sii_holder,
                    uint32_t texture_target);

  ClientSharedImage(ExportedSharedImage exported_si,
                    scoped_refptr<SharedImageInterfaceHolder> sii_holder);

  // This constructor is used only when importing an unowned ClientSharedImage,
  // in which case this ClientSharedImage is not associated with a
  // SharedImageInterface.
  explicit ClientSharedImage(ExportedSharedImage exported_si);

  // VideoFrame needs this info currently for MappableSI.
  // TODO(crbug.com/40263579): Once MappableSI is fully launched for VideoFrame,
  // VF can be refactored to behave like OPAQUE storage which does not need
  // layout info and hence stride. This method will then no longer needed and
  // can be removed.
  size_t GetStrideForVideoFrame(uint32_t plane_index) const {
    CHECK(gpu_memory_buffer_);
    return gpu_memory_buffer_->stride(plane_index);
  }

  // Returns whether the underlying resource is shared memory without needing to
  // Map() the shared image. This method is supposed to be used by VideoFrame
  // temporarily as mentioned above in ::GetStrideForVideoFrame().
  bool IsSharedMemoryForVideoFrame() const {
    CHECK(gpu_memory_buffer_);
    return gpu_memory_buffer_->GetType() ==
           gfx::GpuMemoryBufferType::SHARED_MEMORY_BUFFER;
  }

  bool AsyncMappingIsNonBlocking() const {
    CHECK(gpu_memory_buffer_);
    return gpu_memory_buffer_->AsyncMappingIsNonBlocking();
  }

  // This pair of functions are used by SharedImageTexture to notify
  // ClientSharedImage of the beginning and the end of a scoped access.
  void BeginAccess(bool readonly);
  void EndAccess(bool readonly);

  const Mailbox mailbox_;
  const SharedImageMetadata metadata_;
  SyncToken creation_sync_token_;
  SyncToken destruction_sync_token_;
  // Helper to hold the instance of GpuMemoryBufferManager.
  std::unique_ptr<HelperGpuMemoryBufferManager> gpu_memory_buffer_manager_;
  std::unique_ptr<gfx::GpuMemoryBuffer> gpu_memory_buffer_;
  base::WritableSharedMemoryMapping shared_memory_mapping_;
  std::optional<gfx::BufferUsage> buffer_usage_;
  scoped_refptr<SharedImageInterfaceHolder> sii_holder_;

  bool is_software_ = false;

  // The texture target returned by `GetTextureTarget()`.
  uint32_t texture_target_ = 0;

  // The number of active scoped read accesses.
  unsigned int num_readers_ GUARDED_BY(lock_) = 0;

  // Whether there exists an active scoped write access.
  bool has_writer_ GUARDED_BY(lock_) = false;

  base::Lock lock_;
};

struct GPU_EXPORT ExportedSharedImage {
 public:
  ExportedSharedImage();
  ~ExportedSharedImage();

  ExportedSharedImage(const ExportedSharedImage& other) = delete;
  ExportedSharedImage& operator=(const ExportedSharedImage& other) = delete;
  ExportedSharedImage(ExportedSharedImage&& other);
  ExportedSharedImage& operator=(ExportedSharedImage&& other);

  ExportedSharedImage Clone() const;

 private:
  friend class ClientSharedImage;
  friend class SharedImageInterface;
  friend class ClientSharedImageInterface;
  friend class TestSharedImageInterface;
  friend struct mojo::StructTraits<gpu::mojom::ExportedSharedImageDataView,
                                   ExportedSharedImage>;
  FRIEND_TEST_ALL_PREFIXES(ClientSharedImageTest, ImportUnowned);

  ExportedSharedImage(const Mailbox& mailbox,
                      const SharedImageMetadata& metadata,
                      const SyncToken& sync_token,
                      std::optional<gfx::GpuMemoryBufferHandle> buffer_handle,
                      std::optional<gfx::BufferUsage> buffer_usage,
                      uint32_t texture_target);

  Mailbox mailbox_;
  SharedImageMetadata metadata_;
  SyncToken creation_sync_token_;
  std::optional<gfx::GpuMemoryBufferHandle> buffer_handle_;
  std::optional<gfx::BufferUsage> buffer_usage_;
  uint32_t texture_target_ = 0;
};

class GPU_EXPORT SharedImageTexture {
 public:
  class GPU_EXPORT ScopedAccess {
   public:
    ScopedAccess(const ScopedAccess&) = delete;
    ScopedAccess& operator=(const ScopedAccess&) = delete;
    ScopedAccess(ScopedAccess&&) = delete;
    ScopedAccess& operator=(ScopedAccess&&) = delete;

    ~ScopedAccess();

    unsigned int texture_id() { return texture_->id(); }

    static SyncToken EndAccess(
        std::unique_ptr<ScopedAccess> scoped_shared_image);

   private:
    friend class SharedImageTexture;
    ScopedAccess(SharedImageTexture* texture,
                 const SyncToken& sync_token,
                 bool readonly);
    void DidEndAccess();

    const raw_ptr<SharedImageTexture> texture_;
    const bool readonly_;
    bool is_access_ended_ = false;
  };

  SharedImageTexture(const SharedImageTexture&) = delete;
  SharedImageTexture& operator=(const SharedImageTexture&) = delete;
  SharedImageTexture(SharedImageTexture&&) = delete;
  SharedImageTexture& operator=(SharedImageTexture&&) = delete;

  ~SharedImageTexture();

  std::unique_ptr<ScopedAccess> BeginAccess(const SyncToken& sync_token,
                                            bool readonly);

  void DidEndAccess(bool readonly);
  unsigned int id() { return id_; }

 private:
  friend class ClientSharedImage;
  SharedImageTexture(gles2::GLES2Interface* gl,
                     ClientSharedImage* shared_image);

  const raw_ptr<gles2::GLES2Interface> gl_;
  const raw_ptr<gpu::ClientSharedImage> shared_image_;
  unsigned int id_ = 0;
  bool has_active_access_ = false;
};

class GPU_EXPORT RasterScopedAccess {
 public:
  RasterScopedAccess(const RasterScopedAccess&) = delete;
  RasterScopedAccess& operator=(const RasterScopedAccess&) = delete;
  RasterScopedAccess(RasterScopedAccess&&) = delete;
  RasterScopedAccess& operator=(RasterScopedAccess&&) = delete;

  ~RasterScopedAccess() = default;

  static SyncToken EndAccess(
      std::unique_ptr<RasterScopedAccess> scoped_shared_image);

 private:
  friend class ClientSharedImage;
  RasterScopedAccess(InterfaceBase* raster_interface,
                     ClientSharedImage* shared_image,
                     const SyncToken& sync_token,
                     bool readonly);

  const raw_ptr<InterfaceBase> raster_interface_;
  const raw_ptr<ClientSharedImage> shared_image_;
  bool readonly_;
};

}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_CLIENT_CLIENT_SHARED_IMAGE_H_
