// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_BASE_UTIL_H_
#define REMOTING_BASE_UTIL_H_

#include <string>
#include <string_view>

#include "third_party/webrtc/modules/desktop_capture/desktop_geometry.h"

namespace remoting {

// Return a string that contains the current date formatted as 'MMDD/HHMMSS:'.
std::string GetTimestampString();

int RoundToTwosMultiple(int x);

// Align the sides of the rectangle to multiples of 2 (expanding outwards).
webrtc::DesktopRect AlignRect(const webrtc::DesktopRect& rect);

// Align the left and right sides of the rect using the required data alignment
// of the CPU. This is useful when the rect is to be processed using CPU
// intrinsics that require aligned data.
webrtc::DesktopRect GetRowAlignedRect(const webrtc::DesktopRect rect,
                                      int max_right);

// Replaces every occurrence of "\n" in a string by "\r\n".
std::string ReplaceLfByCrLf(std::string_view in);

// Replaces every occurrence of "\r\n" in a string by "\n".
std::string ReplaceCrLfByLf(std::string_view in);

bool DoesRectContain(const webrtc::DesktopRect& a,
                     const webrtc::DesktopRect& b);

}  // namespace remoting

#endif  // REMOTING_BASE_UTIL_H_
