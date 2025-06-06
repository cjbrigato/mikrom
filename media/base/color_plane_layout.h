// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_COLOR_PLANE_LAYOUT_H_
#define MEDIA_BASE_COLOR_PLANE_LAYOUT_H_

#include <stddef.h>
#include <stdint.h>

#include <ostream>

#include "media/base/media_export.h"

namespace media {

// Encapsulates a color plane's memory layout: (stride, offset, size)
// stride: the number of bytes, including padding, used by each row of a plane.
// offset: in bytes of a plane, which stands for the offset of a start point of
//         a color plane from a buffer FD.
// size:   in bytes of a plane. This |size| bytes data must contain all the data
//         a decoder will access (e.g. visible area and padding).
struct MEDIA_EXPORT ColorPlaneLayout {
  ColorPlaneLayout();
  ColorPlaneLayout(size_t stride, size_t offset, size_t size);
  ~ColorPlaneLayout();

  bool operator==(const ColorPlaneLayout& rhs) const;
  bool operator!=(const ColorPlaneLayout& rhs) const;

  size_t stride = 0;
  size_t offset = 0;
  size_t size = 0;
};

// Outputs ColorPlaneLayout to stream.
MEDIA_EXPORT std::ostream& operator<<(std::ostream& ostream,
                                      const ColorPlaneLayout& plane);

}  // namespace media

#endif  // MEDIA_BASE_COLOR_PLANE_LAYOUT_H_
