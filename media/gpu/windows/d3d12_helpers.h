// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_WINDOWS_D3D12_HELPERS_H_
#define MEDIA_GPU_WINDOWS_D3D12_HELPERS_H_

#include <array>

#include "base/containers/span.h"
#include "base/memory/raw_span.h"
#include "media/base/limits.h"
#include "media/base/video_codecs.h"
#include "media/gpu/h264_dpb.h"
#include "media/gpu/media_gpu_export.h"
#include "media/gpu/windows/d3d_com_defs.h"
#include "media/parsers/h265_parser.h"
#include "media/parsers/vp9_parser.h"
#include "third_party/abseil-cpp/absl/container/inlined_vector.h"
#include "third_party/libgav1/src/src/utils/constants.h"

namespace media {

struct D3D12HeapProperties {
  constexpr static D3D12_HEAP_PROPERTIES kDefault{D3D12_HEAP_TYPE_DEFAULT};
  constexpr static D3D12_HEAP_PROPERTIES kUpload{D3D12_HEAP_TYPE_UPLOAD};
  constexpr static D3D12_HEAP_PROPERTIES kReadback{D3D12_HEAP_TYPE_READBACK};
};

// Manages reference frame buffers, for reference frame descriptors to index on.
class MEDIA_GPU_EXPORT D3D12ReferenceFrameList {
 public:
  explicit D3D12ReferenceFrameList(ComD3D12VideoDecoderHeap heap);
  ~D3D12ReferenceFrameList();

  void WriteTo(D3D12_VIDEO_DECODE_REFERENCE_FRAMES* dest);

  void emplace(size_t index, ID3D12Resource* resource, UINT subresource);

 private:
  // Max size of picture_buffers_ that D3D11VideoDecoder may create.
  static constexpr size_t kMaxSize =
      std::max({static_cast<size_t>(H264DPB::kDPBMaxSize),
                static_cast<size_t>(/*H265*/ kMaxDpbSize),
                static_cast<size_t>(kVp9NumRefFrames),
                static_cast<size_t>(libgav1::kNumReferenceFrameTypes)}) +
      limits::kMaxVideoFrames + 2;

  ComD3D12VideoDecoderHeap heap_;
  size_t size_ = 0;
  // D3D12_VIDEO_DECODE_REFERENCE_FRAMES has ID3D12Resource** ppTexture2Ds, so
  // we have to store raw pointer array here. The pointers are only passed to
  // D3D12 API and we never dereference it in Chromium.
  // The lifetime of these |resources_| is managed by |D3D11VideoDecoder|'s
  // |picture_buffers_|. When picture buffers are invalidated, the
  // D3D12VideoDecoderWrapper and its ID3D12VideoDecoder instance will be
  // destroyed ensuring these values are no longer referenced.
  std::array<ID3D12Resource*, kMaxSize> resources_;
  std::array<UINT, kMaxSize> subresources_;
  // D3D12_VIDEO_DECODE_REFERENCE_FRAMES also has ID3D12VideoDecoderHeap**
  // ppHeaps. The items in |heaps_| are always |heap_.Get()|.
  std::array<ID3D12VideoDecoderHeap*, kMaxSize> heaps_;
};

// A scoped class managing the |Map()| and |Unmap()| of a |ID3D12Resource|. The
// instances of this class are expected to live shortly since it relies on the
// validity of the underlying |ID3D12Resource|.
// Note: Since the Map() operation may fail, the |data()| must be checked
// before use.
class MEDIA_GPU_EXPORT ScopedD3D12ResourceMap {
 public:
  ScopedD3D12ResourceMap();
  ~ScopedD3D12ResourceMap();

  ScopedD3D12ResourceMap(const ScopedD3D12ResourceMap& other) = delete;
  ScopedD3D12ResourceMap& operator=(const ScopedD3D12ResourceMap& other) =
      delete;

  ScopedD3D12ResourceMap(ScopedD3D12ResourceMap&& other) noexcept;
  ScopedD3D12ResourceMap& operator=(ScopedD3D12ResourceMap&& other) noexcept;

  bool Map(ID3D12Resource* resource,
           UINT subresource = 0,
           const D3D12_RANGE* read_range = nullptr);

  // The mapped memory span. It can be empty if there is no successful |Map()|
  // call.
  base::span<uint8_t> data() const { return data_; }

  // |Unmap()| the resource with given |written_range|, if it is not committed.
  void Commit(const D3D12_RANGE* written_range = nullptr);

 private:
  Microsoft::WRL::ComPtr<ID3D12Resource> resource_ = nullptr;
  UINT subresource_ = 0;
  base::raw_span<uint8_t> data_;
};

MEDIA_GPU_EXPORT ComD3D12Device CreateD3D12Device(IDXGIAdapter* adapter);

// In D3D12 resource barriers, subresource id is not only being composed of mip
// level id and array index id, but also plane id. This method is to create an
// vector of transition barriers for all planes if there is more than one (like
// Y plane and UV plane for NV12).
MEDIA_GPU_EXPORT absl::InlinedVector<D3D12_RESOURCE_BARRIER, 2>
CreateD3D12TransitionBarriersForAllPlanes(ID3D12Resource* resource,
                                          UINT subresource,
                                          D3D12_RESOURCE_STATES state_before,
                                          D3D12_RESOURCE_STATES state_after);

// Get the profile GUID for creating ID3D12VideoDecoder. The |bitdepth| and
// |chroma_sampling| will only be checked when the |profile| is
// HEVCPROFILE_REXT.
MEDIA_GPU_EXPORT GUID
GetD3D12VideoDecodeGUID(VideoCodecProfile profile,
                        uint8_t bitdepth,
                        VideoChromaSampling chroma_sampling);

}  // namespace media

#endif  // MEDIA_GPU_WINDOWS_D3D12_HELPERS_H_
