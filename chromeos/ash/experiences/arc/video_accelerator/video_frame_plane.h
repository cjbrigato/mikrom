// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_ASH_EXPERIENCES_ARC_VIDEO_ACCELERATOR_VIDEO_FRAME_PLANE_H_
#define CHROMEOS_ASH_EXPERIENCES_ARC_VIDEO_ACCELERATOR_VIDEO_FRAME_PLANE_H_

#include <stdint.h>

namespace arc {

struct VideoFramePlane {
  int32_t offset;  // in bytes
  int32_t stride;  // in bytes
};

}  // namespace arc

#endif  // CHROMEOS_ASH_EXPERIENCES_ARC_VIDEO_ACCELERATOR_VIDEO_FRAME_PLANE_H_
