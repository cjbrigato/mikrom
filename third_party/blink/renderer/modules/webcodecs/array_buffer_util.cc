// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "third_party/blink/renderer/modules/webcodecs/array_buffer_util.h"

#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_set.h"

namespace blink {

ArrayBufferContents PinSharedArrayBufferContent(
    const AllowSharedBufferSource* buffer_union) {
  ArrayBufferContents result;
  switch (buffer_union->GetContentType()) {
    case AllowSharedBufferSource::ContentType::kArrayBufferAllowShared: {
      auto* buffer = buffer_union->GetAsArrayBufferAllowShared();
      if (buffer && !buffer->IsDetached() && buffer->IsShared()) {
        buffer->Content()->ShareWith(result);
      }
      break;
    }
    case AllowSharedBufferSource::ContentType::kArrayBufferViewAllowShared: {
      auto* view = buffer_union->GetAsArrayBufferViewAllowShared().Get();
      if (view && !view->IsDetached() && view->IsShared()) {
        view->BufferShared()->Content()->ShareWith(result);
      }
      break;
    }
  }
  return result;
}

ArrayBufferContents TransferArrayBufferForSpan(
    const HeapVector<Member<DOMArrayBuffer>>& transfer_list,
    base::span<const uint8_t> data_range,
    ExceptionState& exception_state,
    v8::Isolate* isolate) {
  // Before transferring anything, we check that all the arraybuffers in the
  // list are transferable and there are no duplicates.
  HeapHashSet<Member<DOMArrayBuffer>> seen_buffers;
  for (const Member<DOMArrayBuffer>& array_buffer : transfer_list) {
    if (!array_buffer) {
      continue;
    }

    if (!array_buffer->IsDetachable(isolate) || array_buffer->IsDetached()) {
      exception_state.ThrowDOMException(DOMExceptionCode::kDataCloneError,
                                        "Cannot detach ArrayBuffer");
      return {};
    }

    if (!seen_buffers.insert(array_buffer).is_new_entry) {
      // While inserting we found that the buffer has already been seen.
      exception_state.ThrowDOMException(
          DOMExceptionCode::kDataCloneError,
          "Duplicate ArrayBuffers in the transfer list");
      return {};
    }
  }

  // Transfer all arraybuffers and check if any of them encompass given
  // `data_range`.
  ArrayBufferContents result;
  for (const Member<DOMArrayBuffer>& array_buffer : transfer_list) {
    if (!array_buffer) {
      continue;
    }

    ArrayBufferContents contents;
    if (!array_buffer->Transfer(isolate, contents, exception_state) ||
        !contents.IsValid()) {
      if (exception_state.HadException()) {
        return {};
      }
      continue;
    }

    auto contents_data = contents.ByteSpan();
    if (contents_data.empty() || data_range.empty() ||
        data_range.data() < contents_data.data() ||
        &data_range.back() > &contents_data.back()) {
      // This array buffer doesn't contain `data_range`. Let's ignore it.
      continue;
    }

    if (!result.IsValid()) {
      // We haven't found a matching arraybuffer yet, and this one meets
      // all the criteria. It is our result.
      contents.Transfer(result);
    }
  }
  return result;
}

}  // namespace blink
