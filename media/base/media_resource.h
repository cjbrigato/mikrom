// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_MEDIA_RESOURCE_H_
#define MEDIA_BASE_MEDIA_RESOURCE_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "media/base/demuxer_stream.h"
#include "media/base/media_export.h"

namespace media {

// Abstract class that defines how to retrieve "media resources" in
// DemuxerStream form.
//
// The derived classes must return a non-null value for the getter method
// associated with their type, and return a null/empty value for other getters.
class MEDIA_EXPORT MediaResource {
 public:
  MediaResource();

  MediaResource(const MediaResource&) = delete;
  MediaResource& operator=(const MediaResource&) = delete;

  virtual ~MediaResource();

  // Returns a collection of available DemuxerStream objects.
  //   NOTE: Once a DemuxerStream pointer is returned from GetStream it is
  //   guaranteed to stay valid for as long as the Demuxer/MediaResource
  //   is alive. But make no assumption that once GetStream returned a non-null
  //   pointer for some stream type then all subsequent calls will also return
  //   non-null pointer for the same stream type. In MSE Javascript code can
  //   remove SourceBuffer from a MediaSource at any point and this will make
  //   some previously existing streams inaccessible/unavailable.
  virtual std::vector<DemuxerStream*> GetAllStreams() = 0;

  // A helper function that return the first stream of the given `type` if one
  // exists or a null pointer if there is no streams of that type.
  DemuxerStream* GetFirstStream(DemuxerStream::Type type);
};

}  // namespace media

#endif  // MEDIA_BASE_MEDIA_RESOURCE_H_
