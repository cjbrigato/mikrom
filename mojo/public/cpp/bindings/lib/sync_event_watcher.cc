// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/351564777): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include "mojo/public/cpp/bindings/sync_event_watcher.h"

#include <utility>

#include "base/check_op.h"
#include "base/memory/scoped_refptr.h"
#include "base/synchronization/waitable_event.h"
#include "third_party/abseil-cpp/absl/container/inlined_vector.h"

namespace mojo {

SyncEventWatcher::SyncEventWatcher(base::WaitableEvent* event,
                                   base::RepeatingClosure callback)
    : event_(event),
      callback_(std::move(callback)),
      registry_(SyncHandleRegistry::current()),
      destroyed_(base::MakeRefCounted<base::RefCountedData<bool>>(false)) {}

SyncEventWatcher::~SyncEventWatcher() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  destroyed_->data = true;
}

void SyncEventWatcher::AllowWokenUpBySyncWatchOnSameThread() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  IncrementRegisterCount();
}

bool SyncEventWatcher::SyncWatch(const bool** stop_flags,
                                 size_t num_stop_flags) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  IncrementRegisterCount();

  // This object may be destroyed during the Wait() call. So we have to preserve
  // the boolean that Wait uses.
  auto destroyed = destroyed_;

  constexpr size_t kFlagStackCapacity = 4;
  absl::InlinedVector<const bool*, kFlagStackCapacity> should_stop_array;
  should_stop_array.push_back(&destroyed->data);
  std::copy(stop_flags, stop_flags + num_stop_flags,
            std::back_inserter(should_stop_array));
  bool result =
      registry_->Wait(should_stop_array.data(), should_stop_array.size());

  // This object has been destroyed.
  if (destroyed->data)
    return false;

  DecrementRegisterCount();
  return result;
}

void SyncEventWatcher::IncrementRegisterCount() {
  if (register_request_count_++ == 0)
    subscription_ = registry_->RegisterEvent(event_, callback_);
}

void SyncEventWatcher::DecrementRegisterCount() {
  DCHECK_GT(register_request_count_, 0u);
  if (--register_request_count_ == 0)
    subscription_.reset();
}

}  // namespace mojo
