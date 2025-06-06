// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40284755): Remove this and spanify to fix the errors.
//
// Note: U8_NEXT couldn't be rewritten using the spanification_tool, because it
// is a macro.
#pragma allow_unsafe_buffers
#endif

#include "base/i18n/streaming_utf8_validator.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversion_utils.h"
#include "base/synchronization/lock.h"
#include "base/task/thread_pool.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/icu/source/common/unicode/utf8.h"

namespace base {
namespace {

// Avoid having to qualify the enum values in the tests.
const StreamingUtf8Validator::State VALID_ENDPOINT =
    StreamingUtf8Validator::VALID_ENDPOINT;
const StreamingUtf8Validator::State VALID_MIDPOINT =
    StreamingUtf8Validator::VALID_MIDPOINT;
const StreamingUtf8Validator::State INVALID = StreamingUtf8Validator::INVALID;

const uint32_t kThoroughTestChunkSize = 1 << 24;

class StreamingUtf8ValidatorThoroughTest : public ::testing::Test {
 protected:
  StreamingUtf8ValidatorThoroughTest() = default;

  // This uses the same logic as base::IsStringUTF8 except it considers
  // non-characters valid (and doesn't require a string as input).
  static bool IsStringUtf8(base::span<const uint8_t> src) {
    size_t char_index = 0;
    while (char_index < src.size()) {
      base_icu::UChar32 code_point;
      U8_NEXT(src, char_index, src.size(), code_point);
      if (!base::IsValidCodepoint(code_point)) {
        return false;
      }
    }
    return true;
  }

  // Converts the passed-in integer to a 4 byte string and then
  // verifies that IsStringUtf8 and StreamingUtf8Validator agree on
  // whether it is valid UTF-8 or not.
  void TestNumber(uint32_t n) const {
    uint8_t test[sizeof n];
    memcpy(test, &n, sizeof n);
    StreamingUtf8Validator validator;
    EXPECT_EQ(IsStringUtf8(test), validator.AddBytes(test) == VALID_ENDPOINT)
        << "Difference of opinion for \""
        << base::StringPrintf("\\x%02X\\x%02X\\x%02X\\x%02X", test[0], test[1],
                              test[2], test[3])
        << "\"";
  }

 public:
  // Tests the 4-byte sequences corresponding to the |size| integers
  // starting at |begin|. This is intended to be run from a worker
  // pool. Signals |all_done_| at the end if it thinks all tasks are
  // finished.
  void TestRange(uint32_t begin, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) {
      TestNumber(begin + i);
    }
    base::AutoLock al(lock_);
    ++tasks_finished_;
    LOG(INFO) << tasks_finished_ << " / " << tasks_dispatched_
              << " tasks done\n";
  }

 protected:
  base::Lock lock_;
  int tasks_dispatched_ = 0;
  int tasks_finished_ = 0;
};

// Enable locally to verify that this class accepts exactly the same set of
// 4-byte strings as ICU-based validation. This tests every possible 4-byte
// string, so it is too slow to run routinely on low-powered machines.
TEST_F(StreamingUtf8ValidatorThoroughTest, DISABLED_TestEverything) {
  base::test::TaskEnvironment task_environment;
  {
    base::AutoLock al(lock_);
    uint32_t begin = 0;
    do {
      base::ThreadPool::PostTask(
          FROM_HERE, {base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
          base::BindOnce(&StreamingUtf8ValidatorThoroughTest::TestRange,
                         base::Unretained(this), begin,
                         kThoroughTestChunkSize));
      ++tasks_dispatched_;
      begin += kThoroughTestChunkSize;
    } while (begin != 0);
  }
}

// These valid and invalid UTF-8 sequences are based on the tests from
// base/strings/string_util_unittest.cc

// All of the strings in |valid| must represent a single codepoint, because
// partial sequences are constructed by taking non-empty prefixes of these
// strings.
const char* const valid[] = {
    "\r",           "\n", "a", "\xc2\x81", "\xe1\x80\xbf", "\xf1\x80\xa0\xbf",
    "\xef\xbb\xbf",  // UTF-8 BOM
};

const char* const* const valid_end = std::end(valid);

const char* const invalid[] = {
    // always invalid bytes
    "\xc0",
    "\xc1",
    "\xf5",
    "\xf6",
    "\xf7",
    "\xf8",
    "\xf9",
    "\xfa",
    "\xfb",
    "\xfc",
    "\xfd",
    "\xfe",
    "\xff",
    // surrogate code points
    "\xed\xa0\x80",
    "\xed\x0a\x8f",
    "\xed\xbf\xbf",
    //
    // overlong sequences
    "\xc0\x80",              // U+0000
    "\xc1\x80",              // "A"
    "\xc1\x81",              // "B"
    "\xe0\x80\x80",          // U+0000
    "\xe0\x82\x80",          // U+0080
    "\xe0\x9f\xbf",          // U+07ff
    "\xf0\x80\x80\x8D",      // U+000D
    "\xf0\x80\x82\x91",      // U+0091
    "\xf0\x80\xa0\x80",      // U+0800
    "\xf0\x8f\xbb\xbf",      // U+FEFF (BOM)
    "\xf8\x80\x80\x80\xbf",  // U+003F
    "\xfc\x80\x80\x80\xa0\xa5",
    //
    // Beyond U+10FFFF
    "\xf4\x90\x80\x80",          // U+110000
    "\xf8\xa0\xbf\x80\xbf",      // 5 bytes
    "\xfc\x9c\xbf\x80\xbf\x80",  // 6 bytes
    //
    // BOMs in UTF-16(BE|LE)
    "\xfe\xff",
    "\xff\xfe",
};

const char* const* const invalid_end = std::end(invalid);

// A ForwardIterator which returns all the non-empty prefixes of the elements of
// "valid".
class PartialIterator {
 public:
  // The constructor returns the first iterator, ie. it is equivalent to
  // begin().
  PartialIterator() : index_(0), prefix_length_(0) { Advance(); }
  // The trivial destructor left intentionally undefined.
  // This is a value type; the default copy constructor and assignment operator
  // generated by the compiler are used.

  static PartialIterator end() { return PartialIterator(std::size(valid), 1); }

  PartialIterator& operator++() {
    Advance();
    return *this;
  }

  std::string_view operator*() const {
    return std::string_view(valid[index_], prefix_length_);
  }

  friend bool operator==(const PartialIterator&,
                         const PartialIterator&) = default;

 private:
  // This constructor is used by the end() method.
  PartialIterator(size_t index, size_t prefix_length)
      : index_(index), prefix_length_(prefix_length) {}

  void Advance() {
    if (index_ < std::size(valid) && prefix_length_ < strlen(valid[index_])) {
      ++prefix_length_;
    }
    while (index_ < std::size(valid) &&
           prefix_length_ == strlen(valid[index_])) {
      ++index_;
      prefix_length_ = 1;
    }
  }

  // The UTF-8 sequence, as an offset into the |valid| array.
  size_t index_;
  size_t prefix_length_;
};

// A test fixture for tests which test one UTF-8 sequence (or invalid
// byte sequence) at a time.
class StreamingUtf8ValidatorSingleSequenceTest : public ::testing::Test {
 protected:
  // Iterator must be convertible when de-referenced to std::string_view.
  template <typename Iterator>
  void CheckRange(Iterator begin,
                  Iterator end,
                  StreamingUtf8Validator::State expected) {
    for (Iterator it = begin; it != end; ++it) {
      StreamingUtf8Validator validator;
      std::string_view sequence = *it;
      EXPECT_EQ(expected, validator.AddBytes(base::as_byte_span(sequence)))
          << "Failed for \"" << sequence << "\"";
    }
  }

  // Adding input a byte at a time should make absolutely no difference.
  template <typename Iterator>
  void CheckRangeByteAtATime(Iterator begin,
                             Iterator end,
                             StreamingUtf8Validator::State expected) {
    for (Iterator it = begin; it != end; ++it) {
      StreamingUtf8Validator validator;
      std::string_view sequence = *it;
      StreamingUtf8Validator::State state = VALID_ENDPOINT;
      for (const auto& cit : sequence) {
        state = validator.AddBytes(base::byte_span_from_ref(cit));
      }
      EXPECT_EQ(expected, state) << "Failed for \"" << sequence << "\"";
    }
  }
};

// A test fixture for tests which test the concatenation of byte sequences.
class StreamingUtf8ValidatorDoubleSequenceTest : public ::testing::Test {
 protected:
  // Check every possible concatenation of byte sequences from two
  // ranges, and verify that the combination matches the expected
  // state.
  template <typename Iterator1, typename Iterator2>
  void CheckCombinations(Iterator1 begin1,
                         Iterator1 end1,
                         Iterator2 begin2,
                         Iterator2 end2,
                         StreamingUtf8Validator::State expected) {
    StreamingUtf8Validator validator;
    for (Iterator1 it1 = begin1; it1 != end1; ++it1) {
      std::string_view c1 = *it1;
      for (Iterator2 it2 = begin2; it2 != end2; ++it2) {
        std::string_view c2 = *it2;
        validator.AddBytes(base::as_byte_span(c1));
        EXPECT_EQ(expected, validator.AddBytes(base::as_byte_span(c2)))
            << "Failed for \"" << c1 << c2 << "\"";
        validator.Reset();
      }
    }
  }
};

TEST(StreamingUtf8ValidatorTest, NothingIsValid) {
  EXPECT_EQ(VALID_ENDPOINT, StreamingUtf8Validator().AddBytes({}));
}

// Because the members of the |valid| array need to be non-zero length
// sequences and are measured with strlen(), |valid| cannot be used it
// to test the NUL character '\0', so the NUL character gets its own
// test.
TEST(StreamingUtf8ValidatorTest, NulIsValid) {
  static const char kNul[] = "\x00";
  EXPECT_EQ(VALID_ENDPOINT,
            StreamingUtf8Validator().AddBytes(byte_span_from_cstring(kNul)));
}

// Just a basic sanity test before we start getting fancy.
TEST(StreamingUtf8ValidatorTest, HelloWorld) {
  static const char kHelloWorld[] = "Hello, World!";
  EXPECT_EQ(VALID_ENDPOINT, StreamingUtf8Validator().AddBytes(
                                byte_span_from_cstring(kHelloWorld)));
}

// Check that the Reset() method works.
TEST(StreamingUtf8ValidatorTest, ResetWorks) {
  StreamingUtf8Validator validator;
  EXPECT_EQ(INVALID, validator.AddBytes(byte_span_from_cstring("\xC0")));
  EXPECT_EQ(INVALID, validator.AddBytes(byte_span_from_cstring("a")));
  validator.Reset();
  EXPECT_EQ(VALID_ENDPOINT, validator.AddBytes(byte_span_from_cstring("a")));
}

TEST_F(StreamingUtf8ValidatorSingleSequenceTest, Valid) {
  CheckRange(valid, valid_end, VALID_ENDPOINT);
}

TEST_F(StreamingUtf8ValidatorSingleSequenceTest, Partial) {
  CheckRange(PartialIterator(), PartialIterator::end(), VALID_MIDPOINT);
}

TEST_F(StreamingUtf8ValidatorSingleSequenceTest, Invalid) {
  CheckRange(invalid, invalid_end, INVALID);
}

TEST_F(StreamingUtf8ValidatorSingleSequenceTest, ValidByByte) {
  CheckRangeByteAtATime(valid, valid_end, VALID_ENDPOINT);
}

TEST_F(StreamingUtf8ValidatorSingleSequenceTest, PartialByByte) {
  CheckRangeByteAtATime(PartialIterator(), PartialIterator::end(),
                        VALID_MIDPOINT);
}

TEST_F(StreamingUtf8ValidatorSingleSequenceTest, InvalidByByte) {
  CheckRangeByteAtATime(invalid, invalid_end, INVALID);
}

TEST_F(StreamingUtf8ValidatorDoubleSequenceTest, ValidPlusValidIsValid) {
  CheckCombinations(valid, valid_end, valid, valid_end, VALID_ENDPOINT);
}

TEST_F(StreamingUtf8ValidatorDoubleSequenceTest, ValidPlusPartialIsPartial) {
  CheckCombinations(valid, valid_end, PartialIterator(), PartialIterator::end(),
                    VALID_MIDPOINT);
}

TEST_F(StreamingUtf8ValidatorDoubleSequenceTest, PartialPlusValidIsInvalid) {
  CheckCombinations(PartialIterator(), PartialIterator::end(), valid, valid_end,
                    INVALID);
}

TEST_F(StreamingUtf8ValidatorDoubleSequenceTest, PartialPlusPartialIsInvalid) {
  CheckCombinations(PartialIterator(), PartialIterator::end(),
                    PartialIterator(), PartialIterator::end(), INVALID);
}

TEST_F(StreamingUtf8ValidatorDoubleSequenceTest, ValidPlusInvalidIsInvalid) {
  CheckCombinations(valid, valid_end, invalid, invalid_end, INVALID);
}

TEST_F(StreamingUtf8ValidatorDoubleSequenceTest, InvalidPlusValidIsInvalid) {
  CheckCombinations(invalid, invalid_end, valid, valid_end, INVALID);
}

TEST_F(StreamingUtf8ValidatorDoubleSequenceTest, InvalidPlusInvalidIsInvalid) {
  CheckCombinations(invalid, invalid_end, invalid, invalid_end, INVALID);
}

TEST_F(StreamingUtf8ValidatorDoubleSequenceTest, InvalidPlusPartialIsInvalid) {
  CheckCombinations(invalid, invalid_end, PartialIterator(),
                    PartialIterator::end(), INVALID);
}

TEST_F(StreamingUtf8ValidatorDoubleSequenceTest, PartialPlusInvalidIsInvalid) {
  CheckCombinations(PartialIterator(), PartialIterator::end(), invalid,
                    invalid_end, INVALID);
}

TEST(StreamingUtf8ValidatorValidateTest, EmptyIsValid) {
  EXPECT_TRUE(StreamingUtf8Validator::Validate(std::string()));
}

TEST(StreamingUtf8ValidatorValidateTest, SimpleValidCase) {
  EXPECT_TRUE(StreamingUtf8Validator::Validate("\xc2\x81"));
}

TEST(StreamingUtf8ValidatorValidateTest, SimpleInvalidCase) {
  EXPECT_FALSE(StreamingUtf8Validator::Validate("\xc0\x80"));
}

TEST(StreamingUtf8ValidatorValidateTest, TruncatedIsInvalid) {
  EXPECT_FALSE(StreamingUtf8Validator::Validate("\xc2"));
}

}  // namespace
}  // namespace base
