// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "third_party/blink/renderer/platform/image-decoders/gif/gif_image_decoder.h"

#include <memory>

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/platform/web_data.h"
#include "third_party/blink/renderer/platform/image-decoders/image_decoder_test_helpers.h"
#include "third_party/blink/renderer/platform/wtf/shared_buffer.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

namespace {

const char kWebTestsResourcesDir[] = "web_tests/images/resources";

std::unique_ptr<ImageDecoder> CreateDecoder() {
  return std::make_unique<GIFImageDecoder>(
      ImageDecoder::kAlphaNotPremultiplied, ColorBehavior::kTransformToSRGB,
      ImageDecoder::kNoDecodedImageByteLimit);
}

void TestRepetitionCount(const char* dir,
                         const char* file,
                         int expected_repetition_count) {
  std::unique_ptr<ImageDecoder> decoder = CreateDecoder();
  scoped_refptr<SharedBuffer> data = ReadFileToSharedBuffer(dir, file);
  ASSERT_TRUE(data.get());
  decoder->SetData(data.get(), true);
  EXPECT_EQ(expected_repetition_count, decoder->RepetitionCount());
}

}  // anonymous namespace

TEST(GIFImageDecoderTest, decodeTwoFrames) {
  std::unique_ptr<ImageDecoder> decoder = CreateDecoder();

  scoped_refptr<SharedBuffer> data =
      ReadFileToSharedBuffer(kWebTestsResourcesDir, "animated.gif");
  ASSERT_TRUE(data.get());
  decoder->SetData(data.get(), true);

  ImageFrame* frame = decoder->DecodeFrameBufferAtIndex(0);
  uint32_t generation_id0 = frame->Bitmap().getGenerationID();
  EXPECT_EQ(ImageFrame::kFrameComplete, frame->GetStatus());
  EXPECT_EQ(16, frame->Bitmap().width());
  EXPECT_EQ(16, frame->Bitmap().height());

  frame = decoder->DecodeFrameBufferAtIndex(1);
  uint32_t generation_id1 = frame->Bitmap().getGenerationID();
  EXPECT_EQ(ImageFrame::kFrameComplete, frame->GetStatus());
  EXPECT_EQ(16, frame->Bitmap().width());
  EXPECT_EQ(16, frame->Bitmap().height());
  EXPECT_TRUE(generation_id0 != generation_id1);

  EXPECT_EQ(2u, decoder->FrameCount());
  EXPECT_EQ(kAnimationLoopInfinite, decoder->RepetitionCount());
}

TEST(GIFImageDecoderTest, crbug779261) {
  std::unique_ptr<ImageDecoder> decoder = CreateDecoder();
  scoped_refptr<SharedBuffer> data =
      ReadFileToSharedBuffer(kWebTestsResourcesDir, "crbug779261.gif");
  ASSERT_TRUE(data.get());
  decoder->SetData(data.get(), true);

  for (size_t i = 0; i < decoder->FrameCount(); ++i) {
    // In crbug.com/779261, an independent, transparent frame following an
    // opaque frame failed to decode. This image has an opaque frame 0 with
    // DisposalMethod::kDisposeOverwriteBgcolor, making frame 1, which has
    // transparency, independent and contain alpha.
    const bool has_alpha = 0 == i ? false : true;
    ImageFrame* frame = decoder->DecodeFrameBufferAtIndex(i);
    EXPECT_EQ(ImageFrame::kFrameComplete, frame->GetStatus());
    EXPECT_EQ(has_alpha, frame->HasAlpha());
  }

  EXPECT_FALSE(decoder->Failed());
}

TEST(GIFImageDecoderTest, parseAndDecode) {
  std::unique_ptr<ImageDecoder> decoder = CreateDecoder();

  scoped_refptr<SharedBuffer> data =
      ReadFileToSharedBuffer(kWebTestsResourcesDir, "animated.gif");
  ASSERT_TRUE(data.get());
  decoder->SetData(data.get(), true);

  // This call will parse the entire file.
  EXPECT_EQ(2u, decoder->FrameCount());

  ImageFrame* frame = decoder->DecodeFrameBufferAtIndex(0);
  EXPECT_EQ(ImageFrame::kFrameComplete, frame->GetStatus());
  EXPECT_EQ(16, frame->Bitmap().width());
  EXPECT_EQ(16, frame->Bitmap().height());

  frame = decoder->DecodeFrameBufferAtIndex(1);
  EXPECT_EQ(ImageFrame::kFrameComplete, frame->GetStatus());
  EXPECT_EQ(16, frame->Bitmap().width());
  EXPECT_EQ(16, frame->Bitmap().height());
  EXPECT_EQ(kAnimationLoopInfinite, decoder->RepetitionCount());
}

TEST(GIFImageDecoderTest, parseByteByByte) {
  std::unique_ptr<ImageDecoder> decoder = CreateDecoder();

  const Vector<char> data = ReadFile(kWebTestsResourcesDir, "animated.gif");

  size_t frame_count = 0;

  // Pass data to decoder byte by byte.
  for (size_t length = 1; length <= data.size(); ++length) {
    scoped_refptr<SharedBuffer> temp_data =
        SharedBuffer::Create(base::span(data).first(length));
    decoder->SetData(temp_data.get(), length == data.size());

    EXPECT_LE(frame_count, decoder->FrameCount());
    frame_count = decoder->FrameCount();
  }

  EXPECT_EQ(2u, decoder->FrameCount());

  decoder->DecodeFrameBufferAtIndex(0);
  decoder->DecodeFrameBufferAtIndex(1);
  EXPECT_EQ(kAnimationLoopInfinite, decoder->RepetitionCount());
}

TEST(GIFImageDecoderTest, parseAndDecodeByteByByte) {
  TestByteByByteDecode(&CreateDecoder, kWebTestsResourcesDir,
                       "animated-gif-with-offsets.gif", 5u,
                       kAnimationLoopInfinite);
}

TEST(GIFImageDecoderTest, brokenSecondFrame) {
  std::unique_ptr<ImageDecoder> decoder = CreateDecoder();

  scoped_refptr<SharedBuffer> data =
      ReadFileToSharedBuffer(kDecodersTestingDir, "broken.gif");
  ASSERT_TRUE(data.get());
  decoder->SetData(data.get(), true);

  // One frame is detected but cannot be decoded.
  EXPECT_EQ(1u, decoder->FrameCount());
  ImageFrame* frame = decoder->DecodeFrameBufferAtIndex(1);
  EXPECT_FALSE(frame);
}

TEST(GIFImageDecoderTest, progressiveDecode) {
  TestProgressiveDecoding(&CreateDecoder, kDecodersTestingDir, "radient.gif");
}

TEST(GIFImageDecoderTest, allDataReceivedTruncation) {
  std::unique_ptr<ImageDecoder> decoder = CreateDecoder();

  const Vector<char> data = ReadFile(kWebTestsResourcesDir, "animated.gif");

  ASSERT_GE(data.size(), 10u);
  scoped_refptr<SharedBuffer> temp_data =
      SharedBuffer::Create(base::span(data).first(data.size() - 10));
  decoder->SetData(temp_data.get(), true);

  EXPECT_EQ(2u, decoder->FrameCount());
  EXPECT_FALSE(decoder->Failed());

  decoder->DecodeFrameBufferAtIndex(0);
  EXPECT_FALSE(decoder->Failed());
  decoder->DecodeFrameBufferAtIndex(1);
  EXPECT_FALSE(decoder->Failed());
}

TEST(GIFImageDecoderTest, frameIsComplete) {
  std::unique_ptr<ImageDecoder> decoder = CreateDecoder();

  scoped_refptr<SharedBuffer> data =
      ReadFileToSharedBuffer(kWebTestsResourcesDir, "animated.gif");
  ASSERT_TRUE(data.get());
  decoder->SetData(data.get(), true);

  EXPECT_EQ(2u, decoder->FrameCount());
  EXPECT_FALSE(decoder->Failed());
  EXPECT_TRUE(decoder->FrameIsReceivedAtIndex(0));
  EXPECT_TRUE(decoder->FrameIsReceivedAtIndex(1));
  EXPECT_EQ(kAnimationLoopInfinite, decoder->RepetitionCount());
}

TEST(GIFImageDecoderTest, frameIsCompleteLoading) {
  std::unique_ptr<ImageDecoder> decoder = CreateDecoder();

  const Vector<char> data = ReadFile(kWebTestsResourcesDir, "animated.gif");
  scoped_refptr<SharedBuffer> data_buffer = SharedBuffer::Create(data);

  ASSERT_GE(data.size(), 10u);
  scoped_refptr<SharedBuffer> temp_data =
      SharedBuffer::Create(base::span(data).first(data.size() - 10));
  decoder->SetData(temp_data.get(), false);

  EXPECT_EQ(2u, decoder->FrameCount());
  EXPECT_FALSE(decoder->Failed());
  EXPECT_TRUE(decoder->FrameIsReceivedAtIndex(0));
  EXPECT_FALSE(decoder->FrameIsReceivedAtIndex(1));

  decoder->SetData(data_buffer.get(), true);
  EXPECT_EQ(2u, decoder->FrameCount());
  EXPECT_TRUE(decoder->FrameIsReceivedAtIndex(0));
  EXPECT_TRUE(decoder->FrameIsReceivedAtIndex(1));
}

TEST(GIFImageDecoderTest, badTerminator) {
  scoped_refptr<SharedBuffer> reference_data =
      ReadFileToSharedBuffer(kDecodersTestingDir, "radient.gif");
  scoped_refptr<SharedBuffer> test_data =
      ReadFileToSharedBuffer(kDecodersTestingDir, "radient-bad-terminator.gif");
  ASSERT_TRUE(reference_data.get());
  ASSERT_TRUE(test_data.get());

  std::unique_ptr<ImageDecoder> reference_decoder = CreateDecoder();
  reference_decoder->SetData(reference_data.get(), true);
  EXPECT_EQ(1u, reference_decoder->FrameCount());
  ImageFrame* reference_frame = reference_decoder->DecodeFrameBufferAtIndex(0);
  DCHECK(reference_frame);

  std::unique_ptr<ImageDecoder> test_decoder = CreateDecoder();
  test_decoder->SetData(test_data.get(), true);
  EXPECT_EQ(1u, test_decoder->FrameCount());
  ImageFrame* test_frame = test_decoder->DecodeFrameBufferAtIndex(0);
  DCHECK(test_frame);

  EXPECT_EQ(HashBitmap(reference_frame->Bitmap()),
            HashBitmap(test_frame->Bitmap()));
}

TEST(GIFImageDecoderTest, updateRequiredPreviousFrameAfterFirstDecode) {
  TestUpdateRequiredPreviousFrameAfterFirstDecode(
      &CreateDecoder, kWebTestsResourcesDir, "animated-10color.gif");
}

TEST(GIFImageDecoderTest, randomFrameDecode) {
  // Single frame image.
  TestRandomFrameDecode(&CreateDecoder, kDecodersTestingDir, "radient.gif");
  // Multiple frame images.
  TestRandomFrameDecode(&CreateDecoder, kWebTestsResourcesDir,
                        "animated-gif-with-offsets.gif");
  TestRandomFrameDecode(&CreateDecoder, kWebTestsResourcesDir,
                        "animated-10color.gif");
}

TEST(GIFImageDecoderTest, randomDecodeAfterClearFrameBufferCache) {
  // Single frame image.
  TestRandomDecodeAfterClearFrameBufferCache(
      &CreateDecoder, kDecodersTestingDir, "radient.gif");
  // Multiple frame images.
  TestRandomDecodeAfterClearFrameBufferCache(
      &CreateDecoder, kWebTestsResourcesDir, "animated-gif-with-offsets.gif");
  TestRandomDecodeAfterClearFrameBufferCache(
      &CreateDecoder, kWebTestsResourcesDir, "animated-10color.gif");
}

// The first LZW codes in the image are invalid values that try to create a loop
// in the dictionary. Decoding should fail, but not infinitely loop or corrupt
// memory.
TEST(GIFImageDecoderTest, badInitialCode) {
  scoped_refptr<SharedBuffer> test_data =
      ReadFileToSharedBuffer(kDecodersTestingDir, "bad-initial-code.gif");
  ASSERT_TRUE(test_data.get());

  std::unique_ptr<ImageDecoder> test_decoder = CreateDecoder();
  test_decoder->SetData(test_data.get(), true);
  EXPECT_EQ(1u, test_decoder->FrameCount());
  ASSERT_TRUE(test_decoder->DecodeFrameBufferAtIndex(0));
  EXPECT_TRUE(test_decoder->Failed());
}

// The image has an invalid LZW code that exceeds dictionary size. Decoding
// should fail.
TEST(GIFImageDecoderTest, badCode) {
  scoped_refptr<SharedBuffer> test_data =
      ReadFileToSharedBuffer(kDecodersTestingDir, "bad-code.gif");
  ASSERT_TRUE(test_data.get());

  std::unique_ptr<ImageDecoder> test_decoder = CreateDecoder();
  test_decoder->SetData(test_data.get(), true);
  EXPECT_EQ(1u, test_decoder->FrameCount());
  ASSERT_TRUE(test_decoder->DecodeFrameBufferAtIndex(0));
  EXPECT_TRUE(test_decoder->Failed());
}

TEST(GIFImageDecoderTest, invalidDisposalMethod) {
  std::unique_ptr<ImageDecoder> decoder = CreateDecoder();

  // The image has 2 frames, with disposal method 4 and 5, respectively.
  scoped_refptr<SharedBuffer> data = ReadFileToSharedBuffer(
      kDecodersTestingDir, "invalid-disposal-method.gif");
  ASSERT_TRUE(data.get());
  decoder->SetData(data.get(), true);

  EXPECT_EQ(2u, decoder->FrameCount());
  // Disposal method 4 is converted to ImageFrame::DisposeOverwritePrevious.
  // This is because some specs say method 3 is "overwrite previous", while
  // others say setting the third bit (i.e. method 4) is.
  EXPECT_EQ(ImageFrame::kDisposeOverwritePrevious,
            decoder->DecodeFrameBufferAtIndex(0)->GetDisposalMethod());
  // Unknown disposal methods (5 in this case) are converted to
  // ImageFrame::DisposeKeep.
  EXPECT_EQ(ImageFrame::kDisposeKeep,
            decoder->DecodeFrameBufferAtIndex(1)->GetDisposalMethod());
}

TEST(GIFImageDecoderTest, firstFrameHasGreaterSizeThanScreenSize) {
  const Vector<char> full_data = ReadFile(
      kDecodersTestingDir, "first-frame-has-greater-size-than-screen-size.gif");

  std::unique_ptr<ImageDecoder> decoder;
  gfx::Size frame_size;

  // Compute hashes when the file is truncated.
  for (size_t i = 1; i <= full_data.size(); ++i) {
    decoder = CreateDecoder();
    scoped_refptr<SharedBuffer> data =
        SharedBuffer::Create(base::span(full_data).first(i));
    decoder->SetData(data.get(), i == full_data.size());

    if (decoder->IsSizeAvailable() && !frame_size.width() &&
        !frame_size.height()) {
      frame_size = decoder->DecodedSize();
      continue;
    }

    ASSERT_EQ(frame_size.width(), decoder->DecodedSize().width());
    ASSERT_EQ(frame_size.height(), decoder->DecodedSize().height());
  }
}

TEST(GIFImageDecoderTest, verifyRepetitionCount) {
  // full2loop.gif has 3 frames (it is an animated GIF) and an explicit loop
  // count of 2.
  TestRepetitionCount(kWebTestsResourcesDir, "full2loop.gif", 2);
  // radient.gif has 1 frame (it is a still GIF) and no explicit loop count.
  // For still images, either kAnimationLoopInfinite or kAnimationNone are
  // valid and equivalent, in that the pixels on screen do not change over
  // time. It's arbitrary which one we pick: kAnimationLoopInfinite.
  TestRepetitionCount(kDecodersTestingDir, "radient.gif",
                      kAnimationLoopInfinite);
}

TEST(GIFImageDecoderTest, repetitionCountOfPartialStaticGif) {
  // Data section begins at offset 397 and ends at offset 1033.
  const size_t kOffsetInMiddleOfData = 500u;
  const bool kAllDataReceived = false;

  const Vector<char> full_data = ReadFile(kDecodersTestingDir, "radient.gif");
  scoped_refptr<SharedBuffer> partial_data =
      SharedBuffer::Create(base::span(full_data).first(kOffsetInMiddleOfData));

  std::unique_ptr<ImageDecoder> decoder = CreateDecoder();
  decoder->SetData(partial_data.get(), kAllDataReceived);

  EXPECT_TRUE(decoder->IsSizeAvailable());
  EXPECT_EQ(1u, decoder->FrameCount());

  // TODO(lukasza): This is incorrect - for a static image we should get
  // `kAnimationNone`.  OTOH, the GIF input doesn't specify upfront how
  // many frame there are, so a partially received input is ambiguous
  // wrt whether more animation frames will come or not.
  EXPECT_EQ(kAnimationLoopInfinite, decoder->RepetitionCount());
}

TEST(GIFImageDecoderTest, repetitionCountChangesWhenSeen) {
  const Vector<char> full_data =
      ReadFile(kWebTestsResourcesDir, "animated-10color.gif");
  scoped_refptr<SharedBuffer> full_data_buffer =
      SharedBuffer::Create(full_data);

  // This size must be before the repetition count is encountered in the file.
  const size_t kTruncatedSize = 60;
  ASSERT_TRUE(kTruncatedSize < full_data.size());
  scoped_refptr<SharedBuffer> partial_data =
      SharedBuffer::Create(base::span(full_data).first(kTruncatedSize));

  std::unique_ptr<ImageDecoder> decoder = std::make_unique<GIFImageDecoder>(
      ImageDecoder::kAlphaPremultiplied, ColorBehavior::kTransformToSRGB,
      ImageDecoder::kNoDecodedImageByteLimit);

  decoder->SetData(partial_data.get(), false);
  ASSERT_EQ(kAnimationLoopOnce, decoder->RepetitionCount());
  decoder->SetData(full_data_buffer.get(), true);
  ASSERT_EQ(kAnimationLoopInfinite, decoder->RepetitionCount());
}

TEST(GIFImageDecoderTest, bitmapAlphaType) {
  const Vector<char> full_data = ReadFile(kDecodersTestingDir, "radient.gif");
  scoped_refptr<SharedBuffer> full_data_buffer =
      SharedBuffer::Create(full_data);

  // Empirically chosen truncation size:
  //   a) large enough to produce a partial frame &&
  //   b) small enough to not fully decode the frame
  const size_t kTruncateSize = 800;
  ASSERT_TRUE(kTruncateSize < full_data.size());
  scoped_refptr<SharedBuffer> partial_data =
      SharedBuffer::Create(base::span(full_data).first(kTruncateSize));

  std::unique_ptr<ImageDecoder> premul_decoder =
      std::make_unique<GIFImageDecoder>(ImageDecoder::kAlphaPremultiplied,
                                        ColorBehavior::kTransformToSRGB,
                                        ImageDecoder::kNoDecodedImageByteLimit);
  std::unique_ptr<ImageDecoder> unpremul_decoder =
      std::make_unique<GIFImageDecoder>(ImageDecoder::kAlphaNotPremultiplied,
                                        ColorBehavior::kTransformToSRGB,
                                        ImageDecoder::kNoDecodedImageByteLimit);

  // Partially decoded frame => the frame alpha type is unknown and should
  // reflect the requested format.
  premul_decoder->SetData(partial_data.get(), false);
  ASSERT_TRUE(premul_decoder->FrameCount());
  unpremul_decoder->SetData(partial_data.get(), false);
  ASSERT_TRUE(unpremul_decoder->FrameCount());
  ImageFrame* premul_frame = premul_decoder->DecodeFrameBufferAtIndex(0);
  EXPECT_TRUE(premul_frame &&
              premul_frame->GetStatus() != ImageFrame::kFrameComplete);
  EXPECT_EQ(kPremul_SkAlphaType, premul_frame->Bitmap().alphaType());
  ImageFrame* unpremul_frame = unpremul_decoder->DecodeFrameBufferAtIndex(0);
  EXPECT_TRUE(unpremul_frame &&
              unpremul_frame->GetStatus() != ImageFrame::kFrameComplete);
  EXPECT_EQ(kUnpremul_SkAlphaType, unpremul_frame->Bitmap().alphaType());

  // Fully decoded frame => the frame alpha type is known (opaque).
  premul_decoder->SetData(full_data_buffer.get(), true);
  ASSERT_TRUE(premul_decoder->FrameCount());
  unpremul_decoder->SetData(full_data_buffer.get(), true);
  ASSERT_TRUE(unpremul_decoder->FrameCount());
  premul_frame = premul_decoder->DecodeFrameBufferAtIndex(0);
  EXPECT_TRUE(premul_frame &&
              premul_frame->GetStatus() == ImageFrame::kFrameComplete);
  EXPECT_EQ(kOpaque_SkAlphaType, premul_frame->Bitmap().alphaType());
  unpremul_frame = unpremul_decoder->DecodeFrameBufferAtIndex(0);
  EXPECT_TRUE(unpremul_frame &&
              unpremul_frame->GetStatus() == ImageFrame::kFrameComplete);
  EXPECT_EQ(kOpaque_SkAlphaType, unpremul_frame->Bitmap().alphaType());
}

namespace {
// Needed to exercise ImageDecoder::SetMemoryAllocator, but still does the
// default allocation.
class Allocator final : public SkBitmap::Allocator {
  bool allocPixelRef(SkBitmap* dst) override { return dst->tryAllocPixels(); }
};
}  // namespace

// Ensure that calling SetMemoryAllocator does not short-circuit
// InitializeNewFrame.
TEST(GIFImageDecoderTest, externalAllocator) {
  auto data = ReadFileToSharedBuffer(kWebTestsResourcesDir, "boston.gif");
  ASSERT_TRUE(data.get());

  auto decoder = CreateDecoder();
  decoder->SetData(data.get(), true);

  Allocator allocator;
  decoder->SetMemoryAllocator(&allocator);
  EXPECT_EQ(1u, decoder->FrameCount());
  ImageFrame* frame = decoder->DecodeFrameBufferAtIndex(0);
  decoder->SetMemoryAllocator(nullptr);

  ASSERT_TRUE(frame);
  EXPECT_EQ(gfx::Rect(decoder->Size()), frame->OriginalFrameRect());
  EXPECT_FALSE(frame->HasAlpha());
}

TEST(GIFImageDecoderTest, recursiveDecodeFailure) {
  const Vector<char> data =
      ReadFile(kWebTestsResourcesDir, "count-down-color-test.gif");
  scoped_refptr<SharedBuffer> data_buffer = SharedBuffer::Create(data);

  {
    auto decoder = CreateDecoder();
    decoder->SetData(data_buffer.get(), true);
    for (size_t i = 0; i <= 3; ++i) {
      ImageFrame* frame = decoder->DecodeFrameBufferAtIndex(i);
      ASSERT_NE(frame, nullptr);
      EXPECT_EQ(frame->GetStatus(), ImageFrame::kFrameComplete);
    }
  }

  // Modify data to have an error in frame 2.
  const size_t kErrorOffset = 15302u;
  scoped_refptr<SharedBuffer> modified_data =
      SharedBuffer::Create(base::span(data).first(kErrorOffset));
  modified_data->Append(base::span_from_cstring("A"));
  modified_data->Append(base::span(data).subspan(kErrorOffset + 1));
  {
    auto decoder = CreateDecoder();
    decoder->SetData(modified_data.get(), true);
    decoder->DecodeFrameBufferAtIndex(2);
    EXPECT_FALSE(decoder->Failed());
  }

  {
    // Decode frame 3, recursively decoding frame 2, which 3 depends on.
    auto decoder = CreateDecoder();
    decoder->SetData(modified_data.get(), true);
    ImageFrame* frame = decoder->DecodeFrameBufferAtIndex(3);
    EXPECT_FALSE(decoder->Failed());
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->RequiredPreviousFrameIndex(), 2u);
  }
}

TEST(GIFImageDecoderTest, errorFrame) {
  scoped_refptr<SharedBuffer> test_data =
      ReadFileToSharedBuffer(kDecodersTestingDir, "error_frame.gif");
  ASSERT_TRUE(test_data.get());

  std::unique_ptr<ImageDecoder> decoder = CreateDecoder();
  decoder->SetData(test_data.get(), true);
  wtf_size_t frame_count = decoder->FrameCount();
  EXPECT_EQ(65u, frame_count);
  for (wtf_size_t i = 0; i < frame_count; ++i) {
    decoder->DecodeFrameBufferAtIndex(i);
  }
  EXPECT_FALSE(decoder->Failed());
}

// This is a regression test for https://crbug.com/404288140 where
// the following DCHECK in `blink::ImageFrame::TakeBitmapDataIfWritable` would
// fail when called from `blink::SkiaImageDecoderBase::Decode`:
//
//     ```
//     bool ImageFrame::TakeBitmapDataIfWritable(ImageFrame* other) {
//       DCHECK(other);
//       DCHECK_EQ(kFrameComplete, other->status_);  // <- this one
//     ```
TEST(GIFImageDecoderTest, regressionAgainstReusingIncompletePreviousFrame) {
  scoped_refptr<SharedBuffer> test_data = ReadFileToSharedBuffer(
      kDecodersTestingDir,
      "incomplete-prev-frame-reusing-clusterfuzz-repro.gif");
  ASSERT_TRUE(test_data.get());

  std::unique_ptr<ImageDecoder> decoder = CreateDecoder();
  decoder->SetData(test_data.get(), true);
  ImageFrame* frame = decoder->DecodeFrameBufferAtIndex(2);
  // Lack of a `DCHECK`-triggered crash is the main verification in this test.
  // But for completeness, some supplementary verification follows below...
  EXPECT_TRUE(decoder->Failed());
  EXPECT_TRUE(frame);
  EXPECT_EQ(frame->GetStatus(), ImageFrame::kFrameEmpty);
}

}  // namespace blink
