// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/paint/skottie_transfer_cache_entry.h"

#include <utility>

#include "base/containers/span.h"
#include "cc/paint/skottie_wrapper.h"

namespace cc {

ClientSkottieTransferCacheEntry::ClientSkottieTransferCacheEntry(
    scoped_refptr<SkottieWrapper> skottie)
    : skottie_(std::move(skottie)) {}

ClientSkottieTransferCacheEntry::~ClientSkottieTransferCacheEntry() = default;

uint32_t ClientSkottieTransferCacheEntry::Id() const {
  return skottie_->id();
}

uint32_t ClientSkottieTransferCacheEntry::SerializedSize() const {
  return skottie_->raw_data().size();
}

bool ClientSkottieTransferCacheEntry::Serialize(
    base::span<uint8_t> data) const {
  data.copy_prefix_from(skottie_->raw_data());
  return true;
}

ServiceSkottieTransferCacheEntry::ServiceSkottieTransferCacheEntry() = default;
ServiceSkottieTransferCacheEntry::~ServiceSkottieTransferCacheEntry() = default;

size_t ServiceSkottieTransferCacheEntry::CachedSize() const {
  return cached_size_;
}

bool ServiceSkottieTransferCacheEntry::Deserialize(
    GrDirectContext* context,
    skgpu::graphite::Recorder* graphite_recorder,
    base::span<const uint8_t> data) {
  skottie_ = SkottieWrapper::UnsafeCreateNonSerializable(data);
  cached_size_ = data.size();
  return skottie_->is_valid();
}

}  // namespace cc
