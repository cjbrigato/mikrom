// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40285824): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include <stdint.h>

#include "media/ffmpeg/ffmpeg_common.h"
#include "media/filters/in_memory_url_protocol.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {

auto kData = std::to_array<uint8_t>({0x01, 0x02, 0x03, 0x04});

TEST(InMemoryUrlProtocolTest, ReadFromLargeBuffer) {
  InMemoryUrlProtocol protocol(kData, false);

  uint8_t out[sizeof(kData)];
  EXPECT_EQ(4, protocol.Read(sizeof(out), out));
  EXPECT_EQ(0, memcmp(out, kData.data(), sizeof(out)));
}

TEST(InMemoryUrlProtocolTest, ReadWithNegativeSize) {
  InMemoryUrlProtocol protocol(kData, false);

  uint8_t out[sizeof(kData)];
  EXPECT_EQ(AVERROR(EIO), protocol.Read(-2, out));
}

TEST(InMemoryUrlProtocolTest, ReadWithZeroSize) {
  InMemoryUrlProtocol protocol(kData, false);

  uint8_t out;
  EXPECT_EQ(0, protocol.Read(0, &out));
}

TEST(InMemoryUrlProtocolTest, SetPosition) {
  InMemoryUrlProtocol protocol(kData, false);

  EXPECT_FALSE(protocol.SetPosition(-1));
  EXPECT_FALSE(protocol.SetPosition(sizeof(kData) + 1));

  uint8_t out;
  EXPECT_TRUE(protocol.SetPosition(sizeof(kData)));
  EXPECT_EQ(AVERROR_EOF, protocol.Read(1, &out));

  int i = sizeof(kData) / 2;
  EXPECT_TRUE(protocol.SetPosition(i));
  EXPECT_EQ(1, protocol.Read(1, &out));
  EXPECT_EQ(kData[i], out);
}

}  // namespace media
