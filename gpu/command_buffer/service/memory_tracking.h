// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_SERVICE_MEMORY_TRACKING_H_
#define GPU_COMMAND_BUFFER_SERVICE_MEMORY_TRACKING_H_

#include <stdint.h>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "gpu/command_buffer/common/command_buffer_id.h"
#include "gpu/gpu_export.h"
#include "gpu/ipc/common/gpu_peak_memory.h"

namespace base {
class SequencedTaskRunner;
}  // namespace base

namespace gpu {

// A MemoryTracker is used to propagate per-ContextGroup memory usage
// statistics to the global GpuMemoryManager.
class GPU_EXPORT MemoryTracker {
 public:
  // Observe all changes in memory notified to this MemoryTracker.
  // Used by GpuChannelManager::GpuPeakMemoryMonitor only.
  class Observer : public base::RefCountedThreadSafe<Observer> {
   public:
    Observer() = default;

    Observer(const Observer&) = delete;
    Observer& operator=(const Observer&) = delete;

    virtual void OnMemoryAllocatedChange(
        CommandBufferId id,
        uint64_t old_size,
        uint64_t new_size,
        GpuPeakMemoryAllocationSource source) = 0;

   protected:
    friend class base::RefCountedThreadSafe<Observer>;
    virtual ~Observer() = default;
  };

  virtual ~MemoryTracker();

  MemoryTracker(CommandBufferId command_buffer_id,
                uint64_t client_tracing_id,
                scoped_refptr<gpu::MemoryTracker::Observer> peak_memory_monitor,
                GpuPeakMemoryAllocationSource source);

  // For testing only.
  MemoryTracker();

  virtual void TrackMemoryAllocatedChange(int64_t delta);

  virtual uint64_t GetSize() const;

  // Raw ID identifying the GPU client for whom memory is being allocated.
  virtual int ClientId() const;

  // Tracing id which identifies the GPU client for whom memory is being
  // allocated.
  virtual uint64_t ClientTracingId() const;

  // Returns an ID that uniquely identifies the context group.
  virtual uint64_t ContextGroupTracingId() const;

 private:
  const CommandBufferId command_buffer_id_;
  const uint64_t client_tracing_id_;
  const scoped_refptr<gpu::MemoryTracker::Observer> peak_memory_monitor_;
  const GpuPeakMemoryAllocationSource allocation_source_;

  uint64_t mem_traker_size_ GUARDED_BY(memory_tracker_lock_) = 0;
  mutable base::Lock memory_tracker_lock_;
};

// A MemoryTypeTracker tracks the use of a particular type of memory (buffer,
// texture, or renderbuffer) and forward the result to a specified
// MemoryTracker. MemoryTypeTracker is thread-safe, but it must not outlive the
// MemoryTracker which will be notified on the sequence the MemoryTypeTracker
// was created on (if base::SequencedTaskRunner::HasCurrentDefault()), or on the
// task runner specified (for testing).
class GPU_EXPORT MemoryTypeTracker {
 public:
  explicit MemoryTypeTracker(MemoryTracker* memory_tracker);
  // For testing.
  MemoryTypeTracker(MemoryTracker* memory_tracker,
                    scoped_refptr<base::SequencedTaskRunner> task_runner);

  MemoryTypeTracker(const MemoryTypeTracker&) = delete;
  MemoryTypeTracker& operator=(const MemoryTypeTracker&) = delete;

  ~MemoryTypeTracker();

  const MemoryTracker* memory_tracker() const { return memory_tracker_; }

  void TrackMemAlloc(size_t bytes);
  void TrackMemFree(size_t bytes);
  size_t GetMemRepresented() const;

 private:
  void TrackMemoryAllocatedChange(int64_t delta);

  const raw_ptr<MemoryTracker, DanglingUntriaged> memory_tracker_;

  size_t mem_represented_ GUARDED_BY(lock_) = 0;
  mutable base::Lock lock_;

  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  base::WeakPtrFactory<MemoryTypeTracker> weak_ptr_factory_;
};

}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_SERVICE_MEMORY_TRACKING_H_
