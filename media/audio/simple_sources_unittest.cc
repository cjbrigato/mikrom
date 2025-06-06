// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40285824): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include "media/audio/simple_sources.h"

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <limits>
#include <memory>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/time/time.h"
#include "media/audio/audio_io.h"
#include "media/audio/test_data.h"
#include "media/base/audio_bus.h"
#include "media/base/audio_parameters.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {

// Validate that the SineWaveAudioSource writes the expected values.
TEST(SimpleSources, SineWaveAudioSource) {
  static const uint32_t samples = 1024;
  static const int freq = 200;

  AudioParameters params(AudioParameters::AUDIO_PCM_LINEAR,
                         ChannelLayoutConfig::Mono(),
                         AudioParameters::kTelephoneSampleRate, samples);

  SineWaveAudioSource source(1, freq, params.sample_rate());
  std::unique_ptr<AudioBus> audio_bus = AudioBus::Create(params);
  source.OnMoreData(base::TimeDelta(), base::TimeTicks::Now(), {},
                    audio_bus.get());
  EXPECT_EQ(1, source.callbacks());
  EXPECT_EQ(0, source.errors());

  uint32_t half_period = AudioParameters::kTelephoneSampleRate / (freq * 2);

  auto first_channel = audio_bus->channel_span(0);

  // Spot test positive incursion of sine wave.
  EXPECT_NEAR(0, first_channel[0], std::numeric_limits<float>::epsilon());
  EXPECT_FLOAT_EQ(0.15643446f, first_channel[1]);
  EXPECT_LT(first_channel[1], first_channel[2]);
  EXPECT_LT(first_channel[2], first_channel[3]);
  // Spot test negative incursion of sine wave.
  EXPECT_NEAR(0, first_channel[half_period],
              std::numeric_limits<float>::epsilon());
  EXPECT_FLOAT_EQ(-0.15643446f, first_channel[half_period + 1]);
  EXPECT_GT(first_channel[half_period + 1], first_channel[half_period + 2]);
  EXPECT_GT(first_channel[half_period + 2], first_channel[half_period + 3]);
}

TEST(SimpleSources, SineWaveAudioCapped) {
  SineWaveAudioSource source(1, 200, AudioParameters::kTelephoneSampleRate);

  static const int kSampleCap = 100;
  source.CapSamples(kSampleCap);

  std::unique_ptr<AudioBus> audio_bus = AudioBus::Create(1, 2 * kSampleCap);
  EXPECT_EQ(source.OnMoreData(base::TimeDelta(), base::TimeTicks::Now(), {},
                              audio_bus.get()),
            kSampleCap);
  EXPECT_EQ(1, source.callbacks());
  EXPECT_EQ(source.OnMoreData(base::TimeDelta(), base::TimeTicks::Now(), {},
                              audio_bus.get()),
            0);
  EXPECT_EQ(2, source.callbacks());
  source.Reset();
  EXPECT_EQ(source.OnMoreData(base::TimeDelta(), base::TimeTicks::Now(), {},
                              audio_bus.get()),
            kSampleCap);
  EXPECT_EQ(3, source.callbacks());
  EXPECT_EQ(0, source.errors());
}

TEST(SimpleSources, OnError) {
  SineWaveAudioSource source(1, 200, AudioParameters::kTelephoneSampleRate);
  source.OnError(AudioOutputStream::AudioSourceCallback::ErrorType::kUnknown);
  EXPECT_EQ(1, source.errors());
  source.OnError(AudioOutputStream::AudioSourceCallback::ErrorType::kUnknown);
  EXPECT_EQ(2, source.errors());
}

void VerifyContainsTestFile(const AudioBus* audio_bus) {
  // Convert the test data (little-endian) into floats and compare. We need to
  // index past the first bytes in the data, which contain the wav header.
  const int kFirstSampleIndex = 12 + 8 + 16 + 8;
  std::array<int16_t, 2> data;
  data[0] = kTestAudioData[kFirstSampleIndex];
  data[0] |= (kTestAudioData[kFirstSampleIndex + 1] << 8);
  data[1] = kTestAudioData[kFirstSampleIndex + 2];
  data[1] |= (kTestAudioData[kFirstSampleIndex + 3] << 8);

  // The first frame should hold the WAV data.
  EXPECT_FLOAT_EQ(static_cast<float>(data[0]) / ((1 << 15) - 1),
                  audio_bus->channel_span(0)[0]);
  EXPECT_FLOAT_EQ(static_cast<float>(data[1]) / ((1 << 15) - 1),
                  audio_bus->channel_span(1)[0]);

  // All other frames should be zero-padded. This applies even when looping, as
  // the looping will restart on the next call to OnMoreData.
  for (auto channel : audio_bus->AllChannels()) {
    for (int frame = 1; frame < audio_bus->frames(); ++frame) {
      EXPECT_FLOAT_EQ(0.0, channel[frame]);
    }
  }
}

TEST(SimpleSources, FileSourceTestDataWithoutLooping) {
  const int kNumFrames = 10;

  // Create a temporary file filled with WAV data.
  base::FilePath temp_path;
  ASSERT_TRUE(base::CreateTemporaryFile(&temp_path));
  base::File temp(temp_path,
                  base::File::FLAG_WRITE | base::File::FLAG_OPEN_ALWAYS);
  temp.WriteAtCurrentPos(base::byte_span_from_cstring(kTestAudioData));
  ASSERT_EQ(kTestAudioDataSize, static_cast<size_t>(temp.GetLength()));
  temp.Close();

  // Create AudioParameters which match those in the WAV data.
  AudioParameters params(AudioParameters::AUDIO_PCM_LINEAR,
                         ChannelLayoutConfig::Stereo(), 48000, kNumFrames);
  std::unique_ptr<AudioBus> audio_bus = AudioBus::Create(2, kNumFrames);
  audio_bus->Zero();

  // Create a FileSource that reads this file.
  bool loop = false;
  FileSource source(params, temp_path, loop);
  EXPECT_EQ(kNumFrames,
            source.OnMoreData(base::TimeDelta(), base::TimeTicks::Now(), {},
                              audio_bus.get()));

  VerifyContainsTestFile(audio_bus.get());

  // We should not play any more audio after the file reaches its end.
  audio_bus->Zero();
  source.OnMoreData(base::TimeDelta(), base::TimeTicks::Now(), {},
                    audio_bus.get());

  EXPECT_TRUE(audio_bus->AreFramesZero());
}

TEST(SimpleSources, FileSourceTestDataWithLooping) {
  const int kNumFrames = 10;

  // Create a temporary file filled with WAV data.
  base::FilePath temp_path;
  ASSERT_TRUE(base::CreateTemporaryFile(&temp_path));
  base::File temp(temp_path,
                  base::File::FLAG_WRITE | base::File::FLAG_OPEN_ALWAYS);
  temp.WriteAtCurrentPos(base::byte_span_from_cstring(kTestAudioData));
  ASSERT_EQ(kTestAudioDataSize, static_cast<size_t>(temp.GetLength()));
  temp.Close();

  // Create AudioParameters which match those in the WAV data.
  AudioParameters params(AudioParameters::AUDIO_PCM_LINEAR,
                         ChannelLayoutConfig::Stereo(), 48000, kNumFrames);
  std::unique_ptr<AudioBus> audio_bus = AudioBus::Create(2, kNumFrames);
  audio_bus->Zero();

  bool loop = true;
  FileSource source(params, temp_path, loop);

  // Verify that we keep reading in the file when looping.
  source.OnMoreData(base::TimeDelta(), base::TimeTicks::Now(), {},
                    audio_bus.get());
  audio_bus->Zero();
  source.OnMoreData(base::TimeDelta(), base::TimeTicks::Now(), {},
                    audio_bus.get());

  VerifyContainsTestFile(audio_bus.get());
}

TEST(SimpleSources, BadFilePathFails) {
  AudioParameters params(AudioParameters::AUDIO_PCM_LINEAR,
                         ChannelLayoutConfig::Stereo(), 48000, 10);
  std::unique_ptr<AudioBus> audio_bus = AudioBus::Create(2, 10);
  audio_bus->Zero();

  // Create a FileSource that reads this file.
  base::FilePath path;
  path = path.Append(FILE_PATH_LITERAL("does"))
             .Append(FILE_PATH_LITERAL("not"))
             .Append(FILE_PATH_LITERAL("exist"));
  bool loop = false;
  FileSource source(params, path, loop);
  EXPECT_EQ(0, source.OnMoreData(base::TimeDelta(), base::TimeTicks::Now(), {},
                                 audio_bus.get()));

  // Confirm all frames are zero-padded.
  EXPECT_TRUE(audio_bus->AreFramesZero());
}

TEST(SimpleSources, FileSourceCorruptTestDataFails) {
  const int kNumFrames = 10;

  // Create a temporary file filled with WAV data.
  base::FilePath temp_path;
  ASSERT_TRUE(base::CreateTemporaryFile(&temp_path));
  base::File temp(temp_path,
                  base::File::FLAG_WRITE | base::File::FLAG_OPEN_ALWAYS);
  temp.WriteAtCurrentPos(base::byte_span_from_cstring(kTestAudioData));

  // Corrupt the header.
  temp.Write(3, "0x00", 1);

  ASSERT_EQ(kTestAudioDataSize, static_cast<size_t>(temp.GetLength()));
  temp.Close();

  // Create AudioParameters which match those in the WAV data.
  AudioParameters params(AudioParameters::AUDIO_PCM_LINEAR,
                         ChannelLayoutConfig::Stereo(), 48000, kNumFrames);
  std::unique_ptr<AudioBus> audio_bus = AudioBus::Create(2, kNumFrames);
  audio_bus->Zero();

  // Create a FileSource that reads this file.
  bool loop = false;
  FileSource source(params, temp_path, loop);
  EXPECT_EQ(0, source.OnMoreData(base::TimeDelta(), base::TimeTicks::Now(), {},
                                 audio_bus.get()));

  // Confirm all frames are zero-padded.
  EXPECT_TRUE(audio_bus->AreFramesZero());
}

}  // namespace media
