// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/fonts/shaping/shaping_line_breaker.h"

#include <unicode/uscript.h>

#include "base/time/time.h"
#include "base/timer/lap_timer.h"
#include "third_party/blink/renderer/platform/fonts/font.h"
#include "third_party/blink/renderer/platform/fonts/font_cache.h"
#include "third_party/blink/renderer/platform/fonts/font_test_utilities.h"
#include "third_party/blink/renderer/platform/fonts/shaping/shape_result_test_info.h"
#include "third_party/blink/renderer/platform/fonts/shaping/shape_result_view.h"
#include "third_party/blink/renderer/platform/text/text_break_iterator.h"
#include "third_party/blink/renderer/platform/text/text_run.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "testing/perf/perf_result_reporter.h"

namespace blink {
namespace {

static const int kTimeLimitMillis = 2000;
static const int kWarmupRuns = 5;
static const int kTimeCheckInterval = 10;

constexpr char kMetricPrefixShapingLineBreaker[] = "ShapingLineBreaker.";
constexpr char kMetricThroughput[] = "throughput";

perf_test::PerfResultReporter SetUpReporter(const std::string& story) {
  perf_test::PerfResultReporter reporter(kMetricPrefixShapingLineBreaker,
                                         story);
  reporter.RegisterImportantMetric(kMetricThroughput, "runs/s");
  return reporter;
}

class HarfBuzzShapingLineBreaker : public ShapingLineBreaker {
  STACK_ALLOCATED();

 public:
  HarfBuzzShapingLineBreaker(const HarfBuzzShaper* shaper,
                             const Font* font,
                             const ShapeResult* result,
                             const LazyLineBreakIterator* break_iterator,
                             const Hyphenation* hyphenation)
      : ShapingLineBreaker(result, break_iterator, hyphenation, font),
        shaper_(shaper),
        font_(font) {}

 protected:
  const ShapeResult* Shape(unsigned start,
                           unsigned end,
                           ShapeOptions options) final {
    return shaper_->Shape(font_, GetShapeResult().Direction(), start, end);
  }

  const HarfBuzzShaper* shaper_;
  const Font* font_;
};

LayoutUnit ShapeText(ShapingLineBreaker* breaker,
                     LayoutUnit available_space,
                     unsigned string_length) {
  unsigned break_offset = 0;
  LayoutUnit total_width;
  ShapingLineBreaker::Result result;
  while (break_offset < string_length) {
    const ShapeResultView* shape_result =
        breaker->ShapeLine(break_offset, available_space, &result);
    break_offset = result.break_offset;
    total_width += shape_result->SnappedWidth();
  }
  return total_width;
}

}  // anonymous namespace

class ShapingLineBreakerPerfTest : public testing::Test {
 public:
  ShapingLineBreakerPerfTest()
      : timer_(kWarmupRuns,
               base::Milliseconds(kTimeLimitMillis),
               kTimeCheckInterval) {}

  void SetUp() override { font_description.SetComputedSize(12.0); }

  void TearDown() override {}

  FontCachePurgePreventer font_cache_purge_preventer;
  FontDescription font_description;
  base::LapTimer timer_;
};

TEST_F(ShapingLineBreakerPerfTest, ShapeLatinText) {
  Font* font = MakeGarbageCollected<Font>(font_description);

  // "My Brother's Keeper?"
  // By William Arthur Dunkerley (John Oxenham)
  // In the public domain.
  String string(
      "\"Am I my brother's keeper?\""
      "Yes, of a truth!"
      "Thine asking is thine answer."
      "That self-condemning cry of Cain"
      "Has been the plea of every selfish soul since then,"
      "Which hath its brother slain."
      "God's word is plain,"
      "And doth thy shrinking soul arraign."
      ""
      "Thy brother's keeper?"
      "Yea, of a truth thou art!"
      "For if not--who?"
      "Are ye not both,--both thou and he"
      "Of God's great family?"
      "How rid thee of thy soul's responsibility?"
      "For every ill in all the world"
      "Each soul is sponsor and account must bear."
      "And He, and he thy brother of despair,"
      "Claim, of thy overmuch, their share."
      ""
      "Thou hast had good, and he the strangled days;"
      "But now,--the old things pass."
      "No longer of thy grace"
      "Is he content to live in evil case"
      "For the anointing of thy shining face."
      "The old things pass.--Beware lest ye pass with them,"
      "And your place"
      "Become an emptiness!"
      ""
      "Beware!    Lest, when the \"Have-nots\" claim,"
      "From those who have, their rightful share,"
      "Thy borders be swept bare"
      "As by the final flame."
      "Better to share before than after."
      "\"After?\" ...    For thee may be no after!"
      "Only the howl of mocking laughter"
      "At thy belated care.    Make no mistake!--"
      "\"After\" will be too late."
      "When once the \"Have-nots\" claim ...    they take."
      "\"After!\" ...    When that full claim is made,"
      "You and your golden gods may all lie dead."
      ""
      "Set now your house in order,"
      "Ere it be too late!"
      "For, once the storm of hate"
      "Be loosed, no man shall stay it till"
      "Its thirst has slaked its fill,"
      "And you, poor victims of this last \"too late,\""
      "Shall in the shadows mourn your lost estate.");
  unsigned len = string.length();
  LazyLineBreakIterator break_iterator(string, AtomicString("en-US"),
                                       LineBreakType::kNormal);
  TextDirection direction = TextDirection::kLtr;

  HarfBuzzShaper shaper(string);
  const ShapeResult* reference_result = shaper.Shape(font, direction);
  HarfBuzzShapingLineBreaker reference_breaker(&shaper, font, reference_result,
                                               &break_iterator, nullptr);

  LayoutUnit available_width_px(500);
  LayoutUnit expected_width =
      ShapeText(&reference_breaker, available_width_px, len);

  timer_.Reset();
  do {
    const ShapeResult* result = shaper.Shape(font, direction);
    HarfBuzzShapingLineBreaker breaker(&shaper, font, result, &break_iterator,
                                       nullptr);

    LayoutUnit width = ShapeText(&breaker, available_width_px, len);
    EXPECT_EQ(expected_width, width);
    timer_.NextLap();
  } while (!timer_.HasTimeLimitExpired());

  SetUpReporter("latin_text")
      .AddResult(kMetricThroughput, timer_.LapsPerSecond());
}

}  // namespace blink
