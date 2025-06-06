// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_CAST_TEST_FAKE_OPENSCREEN_CLOCK_H_
#define MEDIA_CAST_TEST_FAKE_OPENSCREEN_CLOCK_H_

#include "third_party/openscreen/src/platform/api/time.h"

namespace base {
class TickClock;
}

namespace media::cast {

// Provides an openscreen::ClockNowFunctionPtr backed by a base::TickClock.
// Usage:
//  FakeOpenscreenClock::SetTickClock(&tick_clock);
//  auto openscreen_object = OpenscreenObject(..., &FakeOpenscreenClock::now,
//  ...);
//  FakeOpenscreenClock::ClearTickClock();
class FakeOpenscreenClock {
 public:
  static void SetTickClock(const base::TickClock* clock);
  static void ClearTickClock();
  static openscreen::Clock::time_point now();
};

}  // namespace media::cast

#endif  // MEDIA_CAST_TEST_FAKE_OPENSCREEN_CLOCK_H_
