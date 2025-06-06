// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/common/autofill_regexes.h"

// Keep these tests in sync with
// components/autofill/core/browser/pattern_provider/default_regex_patterns_unittest.cc
// These tests wil be superceded once the pattern provider launches.

#include <stddef.h>

#include <string>
#include <string_view>

#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace autofill {

namespace {

bool MatchesRegex(std::u16string_view input,
                  std::u16string_view regex,
                  std::vector<std::u16string>* groups = nullptr) {
  static base::NoDestructor<AutofillRegexCache> cache(ThreadSafe(true));
  return autofill::MatchesRegex(input, *cache->GetRegexPattern(regex), groups);
}

std::optional<std::vector<std::u16string>> SplitByRegex(
    std::u16string_view input,
    std::string_view regex,
    size_t max_groups) {
  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::RegexPattern> pattern = base::WrapUnique(
      icu::RegexPattern::compile(icu::UnicodeString::fromUTF8(regex),
                                 UREGEX_CASE_INSENSITIVE, status));
  if (U_FAILURE(status)) {
    return std::nullopt;
  }
  return autofill::SplitByRegex(input, *pattern, max_groups);
}

struct InputPatternTestCase {
  const char16_t* const input;
  const char16_t* const pattern;
};

class PositiveSampleTest : public testing::TestWithParam<InputPatternTestCase> {
};

TEST_P(PositiveSampleTest, SampleRegexes) {
  auto test_case = GetParam();
  SCOPED_TRACE(base::UTF16ToUTF8(test_case.input));
  SCOPED_TRACE(base::UTF16ToUTF8(test_case.pattern));
  EXPECT_TRUE(MatchesRegex(test_case.input, test_case.pattern));
}

INSTANTIATE_TEST_SUITE_P(AutofillRegexesTest,
                         PositiveSampleTest,
                         testing::Values(
                             // Empty pattern
                             InputPatternTestCase{u"", u""},
                             InputPatternTestCase{
                                 u"Look, ma' -- a non-empty string!", u""},
                             // Substring
                             InputPatternTestCase{u"string", u"tri"},
                             // Substring at beginning
                             InputPatternTestCase{u"string", u"str"},
                             InputPatternTestCase{u"string", u"^str"},
                             // Substring at end
                             InputPatternTestCase{u"string", u"ring"},
                             InputPatternTestCase{u"string", u"ring$"},
                             // Case-insensitive
                             InputPatternTestCase{u"StRiNg", u"string"}));

class NegativeSampleTest : public testing::TestWithParam<InputPatternTestCase> {
};

TEST_P(NegativeSampleTest, SampleRegexes) {
  auto test_case = GetParam();
  SCOPED_TRACE(base::UTF16ToUTF8(test_case.input));
  SCOPED_TRACE(base::UTF16ToUTF8(test_case.pattern));
  EXPECT_FALSE(MatchesRegex(test_case.input, test_case.pattern));
}

INSTANTIATE_TEST_SUITE_P(AutofillRegexesTest,
                         NegativeSampleTest,
                         testing::Values(
                             // Empty string
                             InputPatternTestCase{
                                 u"", u"Look, ma' -- a non-empty pattern!"},
                             // Substring
                             InputPatternTestCase{u"string", u"trn"},
                             // Substring at beginning
                             InputPatternTestCase{u"string", u" str"},
                             InputPatternTestCase{u"string", u"^tri"},
                             // Substring at end
                             InputPatternTestCase{u"string", u"ring "},
                             InputPatternTestCase{u"string", u"rin$"}));

// Tests for capture groups.
struct CapturePatternTestCase {
  const char16_t* const input;
  const char16_t* const pattern;
  const bool matches;
  const std::vector<std::u16string> groups;
};

class CaptureTest : public testing::TestWithParam<CapturePatternTestCase> {};

TEST_P(CaptureTest, SampleRegexes) {
  auto test_case = GetParam();
  std::vector<std::u16string> groups;
  EXPECT_EQ(test_case.matches,
            MatchesRegex(test_case.input, test_case.pattern, &groups));
  EXPECT_THAT(groups, testing::Eq(test_case.groups));
}

INSTANTIATE_TEST_SUITE_P(
    AutofillRegexes,
    CaptureTest,
    testing::Values(
        // Find substrings in the input.
        CapturePatternTestCase{u"Foo abcde Bar",
                               u"a(b+)c(d+)e",
                               true,
                               {u"abcde", u"b", u"d"}},
        // Deal with optional capture groups.
        CapturePatternTestCase{u"Foo acde Bar",
                               u"a(b+)?c(d+)e",  // There is no b in the input.
                               true,
                               {u"acde", u"", u"d"}},
        // Deal with non-matching capture groups.
        CapturePatternTestCase{u"Foo acde Bar",
                               u"a(b+)c(d+)e",  // There is no b in the input.
                               false,
                               {}}));

TEST(AutofillRegexes, SplitByRegex) {
  EXPECT_EQ(SplitByRegex(u"이영 호", "[", 10), std::nullopt);
  EXPECT_EQ(SplitByRegex(u"이영 호", " ", 10),
            std::vector<std::u16string>({u"이영", u"호"}));
  EXPECT_EQ(SplitByRegex(u"이영 호", " ", 1),
            std::vector<std::u16string>({u"이영 호"}));
  EXPECT_EQ(SplitByRegex(u"regex", " ", 10),
            std::vector<std::u16string>({u"regex"}));
  EXPECT_EQ(SplitByRegex(u"1  2 3", " ", 2),
            std::vector<std::u16string>({u"1", u" 2 3"}));
  EXPECT_EQ(SplitByRegex(u"", " ", 10), std::nullopt);
  EXPECT_EQ(SplitByRegex(u"    ", "\\s*", 10),
            std::vector<std::u16string>({u"", u""}));
  EXPECT_EQ(SplitByRegex(u"abcd", "\\s*", 10),
            std::vector<std::u16string>({u"", u"a", u"b", u"c", u"d", u""}));
  EXPECT_EQ(SplitByRegex(u"", "", 10), std::nullopt);
}

}  // namespace

}  // namespace autofill
