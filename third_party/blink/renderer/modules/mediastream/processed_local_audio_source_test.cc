// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/mediastream/processed_local_audio_source.h"

#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/test/scoped_feature_list.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "media/base/audio_bus.h"
#include "media/base/audio_glitch_info.h"
#include "media/base/audio_parameters.h"
#include "media/base/audio_timestamp_helper.h"
#include "media/base/media_switches.h"
#include "media/media_buildflags.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/platform/modules/mediastream/web_media_stream_audio_sink.h"
#include "third_party/blink/public/platform/scheduler/test/renderer_scheduler_test_support.h"
#include "third_party/blink/public/web/web_heap.h"
#include "third_party/blink/renderer/core/testing/sim/sim_test.h"
#include "third_party/blink/renderer/modules/mediastream/media_constraints.h"
#include "third_party/blink/renderer/modules/mediastream/testing_platform_support_with_mock_audio_capture_source.h"
#include "third_party/blink/renderer/modules/webrtc/webrtc_audio_device_impl.h"
#include "third_party/blink/renderer/platform/mediastream/media_stream_audio_processor_options.h"
#include "third_party/blink/renderer/platform/mediastream/media_stream_audio_track.h"
#include "third_party/blink/renderer/platform/mediastream/media_stream_component_impl.h"
#include "third_party/blink/renderer/platform/mediastream/media_stream_source.h"
#include "third_party/blink/renderer/platform/mediastream/media_stream_track_platform.h"

using ::testing::_;
using ::testing::AtLeast;
using ::testing::Invoke;
using ::testing::WithArg;

namespace blink {

namespace {

// Audio parameters for the VerifyAudioFlowWithoutAudioProcessing test.
constexpr int kSampleRate = 48000;
constexpr media::ChannelLayout kChannelLayout = media::CHANNEL_LAYOUT_STEREO;
constexpr int kDeviceBufferSize = 512;

enum class ProcessingLocation { kProcessedLocalAudioSource, kAudioService };

std::tuple<int, int> ComputeExpectedSourceAndOutputBufferSizes(
    ProcessingLocation processing_location) {
  // On Android, ProcessedLocalAudioSource forces a 20ms buffer size from the
  // input device.
#if BUILDFLAG(IS_ANDROID)
  constexpr int kExpectedUnprocessedBufferSize = kSampleRate / 50;
#else
  constexpr int kExpectedUnprocessedBufferSize = kDeviceBufferSize;
#endif

  // On both platforms, even though audio processing is turned off, the audio
  // processing code may force the use of 10ms output buffer sizes.
  constexpr int kExpectedOutputBufferSize = kSampleRate / 100;

  switch (processing_location) {
    case ProcessingLocation::kProcessedLocalAudioSource:
      // The ProcessedLocalAudioSource changes format when it hosts the audio
      // processor.
      return {kExpectedUnprocessedBufferSize, kExpectedOutputBufferSize};
    case ProcessingLocation::kAudioService:
      // To minimize resampling after processing in the audio service,
      // ProcessedLocalAudioSource requests audio in the post-processing format.
      return {kExpectedOutputBufferSize, kExpectedOutputBufferSize};
    default:
      NOTREACHED();
  }
}

class FormatCheckingMockAudioSink : public WebMediaStreamAudioSink {
 public:
  FormatCheckingMockAudioSink() = default;
  ~FormatCheckingMockAudioSink() override = default;

  void OnData(const media::AudioBus& audio_bus,
              base::TimeTicks estimated_capture_time) override {
    EXPECT_EQ(audio_bus.channels(), params_.channels());
    EXPECT_EQ(audio_bus.frames(), params_.frames_per_buffer());
    EXPECT_FALSE(estimated_capture_time.is_null());
    OnDataCallback();
  }
  MOCK_METHOD0(OnDataCallback, void());

  void OnSetFormat(const media::AudioParameters& params) override {
    params_ = params;
    FormatIsSet(params_);
  }
  MOCK_METHOD1(FormatIsSet, void(const media::AudioParameters& params));

 private:
  media::AudioParameters params_;
};

}  // namespace

class ProcessedLocalAudioSourceBase : public SimTest {
 protected:
  ProcessedLocalAudioSourceBase() = default;
  ~ProcessedLocalAudioSourceBase() override = default;

  void SetUp() override { SimTest::SetUp(); }

  void TearDown() override {
    SimTest::TearDown();
    audio_source_ = nullptr;
    audio_component_ = nullptr;
    WebHeap::CollectAllGarbageForTesting();
  }

  void CreateProcessedLocalAudioSource(
      const MediaStreamAudioProcessingLayout& processing_layout) {
    std::unique_ptr<blink::ProcessedLocalAudioSource> source =
        std::make_unique<blink::ProcessedLocalAudioSource>(
            *MainFrame().GetFrame(),
            MediaStreamDevice(
                mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE,
                "mock_audio_device_id", "Mock audio device", kSampleRate,
                media::ChannelLayoutConfig::FromLayout<kChannelLayout>(),
                kDeviceBufferSize),
            /*disable_local_echo=*/false, processing_layout, base::DoNothing(),
            scheduler::GetSingleThreadTaskRunnerForTesting());
    source->SetAllowInvalidRenderFrameIdForTesting(true);
    audio_source_ = MakeGarbageCollected<MediaStreamSource>(
        String::FromUTF8("audio_label"), MediaStreamSource::kTypeAudio,
        String::FromUTF8("audio_track"), /*remote=*/false, std::move(source));
    audio_component_ = MakeGarbageCollected<MediaStreamComponentImpl>(
        audio_source_->Id(), audio_source_,
        std::make_unique<MediaStreamAudioTrack>(/*is_local=*/true));
  }

  media::AudioCapturerSource::CaptureCallback* capture_source_callback() const {
    return static_cast<media::AudioCapturerSource::CaptureCallback*>(
        ProcessedLocalAudioSource::From(audio_source()));
  }

  MediaStreamAudioSource* audio_source() const {
    return MediaStreamAudioSource::From(audio_source_.Get());
  }

  MediaStreamComponent* audio_track() { return audio_component_; }

  MockAudioCapturerSource* mock_audio_capturer_source() {
    return webrtc_audio_device_platform_support_->mock_audio_capturer_source();
  }

 private:
  ScopedTestingPlatformSupport<AudioCapturerSourceTestingPlatformSupport>
      webrtc_audio_device_platform_support_;
  Persistent<MediaStreamSource> audio_source_;
  Persistent<MediaStreamComponent> audio_component_;
};

class ProcessedLocalAudioSourceTest
    : public ProcessedLocalAudioSourceBase,
      public testing::WithParamInterface<ProcessingLocation> {
 public:
  void SetUp() override {
    ProcessedLocalAudioSourceBase::SetUp();
    std::tie(expected_source_buffer_size_, expected_output_buffer_size_) =
        ComputeExpectedSourceAndOutputBufferSizes(GetParam());
  }

  void CheckSourceFormatMatches(const media::AudioParameters& params) {
    EXPECT_EQ(kSampleRate, params.sample_rate());
    EXPECT_EQ(kChannelLayout, params.channel_layout());
    EXPECT_EQ(expected_source_buffer_size_, params.frames_per_buffer());
  }

  void CheckOutputFormatMatches(const media::AudioParameters& params) {
    EXPECT_EQ(kSampleRate, params.sample_rate());
    EXPECT_EQ(kChannelLayout, params.channel_layout());
    EXPECT_EQ(expected_output_buffer_size_, params.frames_per_buffer());
  }

  int expected_source_buffer_size_;
  int expected_output_buffer_size_;
};

// Tests a basic end-to-end start-up, track+sink connections, audio flow, and
// shut-down. The tests in media_stream_audio_test.cc provide more comprehensive
// testing of the object graph connections and multi-threading concerns.
TEST_P(ProcessedLocalAudioSourceTest, VerifyAudioFlowWithoutAudioProcessing) {
  base::test::ScopedFeatureList scoped_feature_list;
#if BUILDFLAG(CHROME_WIDE_ECHO_CANCELLATION)
  if (GetParam() == ProcessingLocation::kAudioService) {
    scoped_feature_list.InitAndEnableFeature(
        media::kChromeWideEchoCancellation);
  } else {
    scoped_feature_list.InitAndDisableFeature(
        media::kChromeWideEchoCancellation);
  }
#endif

  using ThisTest =
      ProcessedLocalAudioSourceTest_VerifyAudioFlowWithoutAudioProcessing_Test;

  // Turn off the default constraints so the sink will get audio in chunks of
  // the native buffer size.
  AudioProcessingProperties properties;
  properties.DisableDefaultProperties();
  CreateProcessedLocalAudioSource(MediaStreamAudioProcessingLayout(
      properties,
      /*available_platform_effects=*/0, /*channels=*/1));

  // Connect the track, and expect the MockAudioCapturerSource to be initialized
  // and started by ProcessedLocalAudioSource.
  EXPECT_CALL(*mock_audio_capturer_source(),
              Initialize(_, capture_source_callback()))
      .WillOnce(WithArg<0>(Invoke(this, &ThisTest::CheckSourceFormatMatches)));
#if BUILDFLAG(IS_CHROMEOS)
  EXPECT_CALL(*mock_audio_capturer_source(), SetAutomaticGainControl(true));
#else
  EXPECT_CALL(*mock_audio_capturer_source(),
              SetAutomaticGainControl(properties.auto_gain_control));
#endif
  EXPECT_CALL(*mock_audio_capturer_source(), Start())
      .WillOnce(Invoke(
          capture_source_callback(),
          &media::AudioCapturerSource::CaptureCallback::OnCaptureStarted));
  ASSERT_TRUE(audio_source()->ConnectToInitializedTrack(audio_track()));
  CheckOutputFormatMatches(audio_source()->GetAudioParameters());

  // Connect a sink to the track.
  auto sink = std::make_unique<FormatCheckingMockAudioSink>();
  EXPECT_CALL(*sink, FormatIsSet(_))
      .WillOnce(Invoke(this, &ThisTest::CheckOutputFormatMatches));
  MediaStreamAudioTrack::From(audio_track())->AddSink(sink.get());

  // Feed audio data into the ProcessedLocalAudioSource and expect it to reach
  // the sink.
  int delay_ms = 65;
  double volume = 0.9;
  const base::TimeTicks capture_time =
      base::TimeTicks::Now() + base::Milliseconds(delay_ms);
  const media::AudioGlitchInfo glitch_info{.duration = base::Milliseconds(123),
                                           .count = 1};
  std::unique_ptr<media::AudioBus> audio_bus =
      media::AudioBus::Create(2, expected_source_buffer_size_);
  audio_bus->Zero();
  EXPECT_CALL(*sink, OnDataCallback()).Times(AtLeast(1));
  capture_source_callback()->Capture(audio_bus.get(), capture_time, glitch_info,
                                     volume);

  // Expect glitches to have been propagated.
  MediaStreamTrackPlatform::AudioFrameStats audio_stats;
  audio_track()->GetPlatformTrack()->TransferAudioFrameStatsTo(audio_stats);
  EXPECT_EQ(audio_stats.TotalFrames() - audio_stats.DeliveredFrames(),
            static_cast<unsigned int>(media::AudioTimestampHelper::TimeToFrames(
                glitch_info.duration, kSampleRate)));
  EXPECT_EQ(
      audio_stats.TotalFramesDuration() - audio_stats.DeliveredFramesDuration(),
      glitch_info.duration);

  // Expect the ProcessedLocalAudioSource to auto-stop the MockCapturerSource
  // when the track is stopped.
  EXPECT_CALL(*mock_audio_capturer_source(), Stop());
  MediaStreamAudioTrack::From(audio_track())->Stop();
}

#if BUILDFLAG(CHROME_WIDE_ECHO_CANCELLATION)
INSTANTIATE_TEST_SUITE_P(
    All,
    ProcessedLocalAudioSourceTest,
    testing::Values(ProcessingLocation::kProcessedLocalAudioSource,
                    ProcessingLocation::kAudioService));
#else
INSTANTIATE_TEST_SUITE_P(
    All,
    ProcessedLocalAudioSourceTest,
    testing::Values(ProcessingLocation::kProcessedLocalAudioSource));
#endif

#if BUILDFLAG(IS_CHROMEOS)
enum AgcState {
  AGC_DISABLED,
  BROWSER_AGC,
  SYSTEM_AGC,
};

class ProcessedLocalAudioSourceIgnoreUiGainsTest
    : public ProcessedLocalAudioSourceBase,
      public testing::WithParamInterface<testing::tuple<bool, AgcState>> {
 public:
  bool IsIgnoreUiGainsEnabled() { return std::get<0>(GetParam()); }

  void SetUp() override {
    if (IsIgnoreUiGainsEnabled()) {
      feature_list_.InitAndEnableFeature(media::kIgnoreUiGains);
    } else {
      feature_list_.InitAndDisableFeature(media::kIgnoreUiGains);
    }

    ProcessedLocalAudioSourceBase::SetUp();
  }

  MediaStreamAudioProcessingLayout SetUpAudioProcessingLayout() {
    AudioProcessingProperties properties;
    int platform_effects = 0;
    switch (std::get<1>(GetParam())) {
      case AGC_DISABLED:
        properties.auto_gain_control = false;
        break;
      case BROWSER_AGC:
        properties.auto_gain_control = true;
        break;
      case SYSTEM_AGC:
        properties.auto_gain_control = true;
        platform_effects |= media::AudioParameters::AUTOMATIC_GAIN_CONTROL;
        break;
    }
    return MediaStreamAudioProcessingLayout(properties, platform_effects,
                                            /*channels=*/1);
  }

 protected:
  base::test::ScopedFeatureList feature_list_;
};

MATCHER_P2(AudioEffectsAsExpected, flag, agc_state, "") {
  if (flag) {
    switch (agc_state) {
      case AGC_DISABLED:
        return (arg.effects() & media::AudioParameters::IGNORE_UI_GAINS) == 0;
        break;
      case BROWSER_AGC:
      case SYSTEM_AGC:
        return (arg.effects() & media::AudioParameters::IGNORE_UI_GAINS) != 0;
        break;
    }
  } else {
    return (arg.effects() & media::AudioParameters::IGNORE_UI_GAINS) == 0;
  }
}

TEST_P(ProcessedLocalAudioSourceIgnoreUiGainsTest,
       VerifyIgnoreUiGainsStateAsExpected) {
  CreateProcessedLocalAudioSource(SetUpAudioProcessingLayout());

  // Connect the track, and expect the MockAudioCapturerSource to be initialized
  // and started by ProcessedLocalAudioSource.
  EXPECT_CALL(*mock_audio_capturer_source(),
              Initialize(AudioEffectsAsExpected(std::get<0>(GetParam()),
                                                std::get<1>(GetParam())),
                         capture_source_callback()));
  EXPECT_CALL(*mock_audio_capturer_source(), SetAutomaticGainControl(true));
  EXPECT_CALL(*mock_audio_capturer_source(), Start())
      .WillOnce(Invoke(
          capture_source_callback(),
          &media::AudioCapturerSource::CaptureCallback::OnCaptureStarted));
  ASSERT_TRUE(audio_source()->ConnectToInitializedTrack(audio_track()));
}

INSTANTIATE_TEST_SUITE_P(
    IgnoreUiGainsTest,
    ProcessedLocalAudioSourceIgnoreUiGainsTest,
    ::testing::Combine(::testing::Bool(),
                       ::testing::ValuesIn({AgcState::AGC_DISABLED,
                                            AgcState::BROWSER_AGC,
                                            AgcState::SYSTEM_AGC})));

enum AecState {
  AEC_DISABLED,
  BROWSER_AEC,
  SYSTEM_AEC,
};

enum VoiceIsolationState {
  kEnabled,
  kDisabled,
  kDefault,
};

class ProcessedLocalAudioSourceVoiceIsolationTest
    : public ProcessedLocalAudioSourceBase,
      public testing::WithParamInterface<
          testing::tuple<bool, bool, VoiceIsolationState, AecState, bool>> {
 public:
  bool IsVoiceIsolationOptionEnabled() { return std::get<0>(GetParam()); }
  bool IsVoiceIsolationSupported() { return std::get<1>(GetParam()); }
  VoiceIsolationState GetVoiceIsolationState() {
    return std::get<2>(GetParam());
  }
  AecState GetAecState() { return std::get<3>(GetParam()); }
  bool IsSystemAecDefaultEnabled() { return std::get<4>(GetParam()); }

  void SetUp() override {
    if (IsVoiceIsolationOptionEnabled()) {
      feature_list_.InitAndEnableFeature(
          media::kCrOSSystemVoiceIsolationOption);
    } else {
      feature_list_.InitAndDisableFeature(
          media::kCrOSSystemVoiceIsolationOption);
    }

    ProcessedLocalAudioSourceBase::SetUp();
  }

  MediaStreamAudioProcessingLayout SetUpAudioProcessingLayout(
      int platform_effects) {
    AudioProcessingProperties properties;
    switch (GetAecState()) {
      case AEC_DISABLED:
        properties.echo_cancellation_type = AudioProcessingProperties::
            EchoCancellationType::kEchoCancellationDisabled;
        break;
      case BROWSER_AEC:
        properties.echo_cancellation_type = AudioProcessingProperties::
            EchoCancellationType::kEchoCancellationAec3;
        break;
      case SYSTEM_AEC:
        properties.echo_cancellation_type = AudioProcessingProperties::
            EchoCancellationType::kEchoCancellationSystem;
        break;
    }

    switch (GetVoiceIsolationState()) {
      case VoiceIsolationState::kEnabled:
        properties.voice_isolation = AudioProcessingProperties::
            VoiceIsolationType::kVoiceIsolationEnabled;
        break;
      case VoiceIsolationState::kDisabled:
        properties.voice_isolation = AudioProcessingProperties::
            VoiceIsolationType::kVoiceIsolationDisabled;
        break;
      case VoiceIsolationState::kDefault:
        properties.voice_isolation = AudioProcessingProperties::
            VoiceIsolationType::kVoiceIsolationDefault;
        break;
    }
    return MediaStreamAudioProcessingLayout(properties, platform_effects,
                                            /*channels=*/1);
  }

 protected:
  base::test::ScopedFeatureList feature_list_;
};

MATCHER_P4(VoiceIsolationAsExpected,
           voice_isolation_option_enabled,
           voice_isolation_supported,
           voice_isolation_state,
           aec_state,
           "") {
  // Only if voice isolation is supported and browser AEC is enabled while voice
  // isolation option feature flag is set, The voice isolation is force to being
  // off. In this case, `CLIENT_CONTROLLED_VOICE_ISOLATION` should be set and
  // `VOICE_ISOLATION` should be off.
  // Otherwise, `CLIENT_CONTROLLED_VOICE_ISOLATION` should be off and
  // `VOICE_ISOLATION` bit is don't-care.
  const bool client_controlled_voice_isolation =
      arg.effects() & media::AudioParameters::CLIENT_CONTROLLED_VOICE_ISOLATION;
  const bool voice_isolation_activated =
      arg.effects() & media::AudioParameters::VOICE_ISOLATION;

  if (voice_isolation_supported && voice_isolation_option_enabled) {
    if (aec_state == BROWSER_AEC) {
      return client_controlled_voice_isolation && !voice_isolation_activated;
    }
    if (voice_isolation_state == VoiceIsolationState::kEnabled) {
      return client_controlled_voice_isolation && voice_isolation_activated;
    }
    if (voice_isolation_state == VoiceIsolationState::kDisabled) {
      return client_controlled_voice_isolation && !voice_isolation_activated;
    }
  }
  return !client_controlled_voice_isolation;
}

TEST_P(ProcessedLocalAudioSourceVoiceIsolationTest,
       VerifyVoiceIsolationStateAsExpected) {
  int platform_effects = 0;
  if (IsVoiceIsolationSupported()) {
    platform_effects |= media::AudioParameters::VOICE_ISOLATION_SUPPORTED;
  }
  if (IsSystemAecDefaultEnabled()) {
    platform_effects |= media::AudioParameters::ECHO_CANCELLER;
  }

  MediaStreamAudioProcessingLayout processing_layout =
      SetUpAudioProcessingLayout(platform_effects);
  CreateProcessedLocalAudioSource(processing_layout);

  blink::MediaStreamDevice modified_device(audio_source()->device());
  modified_device.input.set_effects(platform_effects);
  audio_source()->SetDevice(modified_device);

  // Connect the track, and expect the MockAudioCapturerSource to be initialized
  // and started by ProcessedLocalAudioSource.
  EXPECT_CALL(*mock_audio_capturer_source(),
              Initialize(VoiceIsolationAsExpected(
                             IsVoiceIsolationOptionEnabled(),
                             IsVoiceIsolationSupported(),
                             GetVoiceIsolationState(), GetAecState()),
                         capture_source_callback()));
  EXPECT_CALL(*mock_audio_capturer_source(), Start())
      .WillOnce(Invoke(
          capture_source_callback(),
          &media::AudioCapturerSource::CaptureCallback::OnCaptureStarted));
  ASSERT_TRUE(audio_source()->ConnectToInitializedTrack(audio_track()));
}

INSTANTIATE_TEST_SUITE_P(
    VoiceIsolationTest,
    ProcessedLocalAudioSourceVoiceIsolationTest,
    ::testing::Combine(::testing::Bool(),
                       ::testing::Bool(),
                       ::testing::ValuesIn({VoiceIsolationState::kEnabled,
                                            VoiceIsolationState::kDisabled,
                                            VoiceIsolationState::kDefault}),
                       ::testing::ValuesIn({AecState::AEC_DISABLED,
                                            AecState::BROWSER_AEC,
                                            AecState::SYSTEM_AEC}),
                       ::testing::Bool()));

#endif

// Matches media::AudioParameters effects.
MATCHER_P(ExactParamProcessingEffects, expected, "") {
  if (expected.effects() ==
      (arg.effects() & (media::AudioParameters::ECHO_CANCELLER |
                        media::AudioParameters::NOISE_SUPPRESSION |
                        media::AudioParameters::AUTOMATIC_GAIN_CONTROL))) {
    return true;
  }
  LOG(ERROR) << "\n expected: " << expected.AsHumanReadableString()
             << "\n      arg: " << arg.AsHumanReadableString();
  return false;
}

class ProcessedLocalAudioSourcePlatformEffectsTest
    : public ProcessedLocalAudioSourceBase,
      public testing::WithParamInterface<
          testing::tuple<AudioProcessingProperties::EchoCancellationType,
                         bool,
                         bool>> {};

TEST_P(ProcessedLocalAudioSourcePlatformEffectsTest,
       PlatformAecNsAgcCorrectIfAvailale) {
  AudioProcessingProperties properties;
  properties.echo_cancellation_type = std::get<0>(GetParam());
  properties.noise_suppression = std::get<1>(GetParam());
  properties.auto_gain_control = std::get<2>(GetParam());

  int platform_effects = media::AudioParameters::ECHO_CANCELLER |
                         media::AudioParameters::NOISE_SUPPRESSION |
                         media::AudioParameters::AUTOMATIC_GAIN_CONTROL;

  CreateProcessedLocalAudioSource(MediaStreamAudioProcessingLayout(
      properties, platform_effects, /*channels=*/1));

  // Enable platform AEC, HS and AGC effects for the device.
  blink::MediaStreamDevice modified_device(audio_source()->device());
  modified_device.input.set_effects(platform_effects);
  audio_source()->SetDevice(modified_device);

  media::AudioParameters expected_params = modified_device.input;
  int expected_effects = expected_params.effects();

  if (media::IsChromeWideEchoCancellationEnabled()) {
    // As of now, when Chrome-wide echo cancellation is enabled, all effects are
    // cleared out. While it's inconsistent, it works, because as of now in
    // production we never have Chrome-wide echo cancellation and platform AEC
    // available at the same time.
    expected_effects = 0;
  } else if (properties.echo_cancellation_type !=
             AudioProcessingProperties::EchoCancellationType::
                 kEchoCancellationSystem) {
    // No platform processing if platform AEC is not requested.
    expected_effects &= ~media::AudioParameters::ECHO_CANCELLER;
    expected_effects &= ~media::AudioParameters::AUTOMATIC_GAIN_CONTROL;
    if (!MediaStreamAudioProcessingLayout::
            IsIndependentSystemNsAllowedForTests()) {
      // Special case for NS.
      expected_effects &= ~media::AudioParameters::NOISE_SUPPRESSION;
    }
  } else {  // kEchoCancellationSystem
    // Disable AGC and NS if not requested.
    if (!properties.auto_gain_control) {
      expected_effects &= ~media::AudioParameters::AUTOMATIC_GAIN_CONTROL;
    }
    if (!properties.noise_suppression &&
        !MediaStreamAudioProcessingLayout::
            IsIndependentSystemNsAllowedForTests()) {
      // TODO(crbug.com/417413190): It's weird that we keep NS enabled in this
      // case if IsIndependentSystemNsAllowed() returns true, but this is how
      // the code works now.
      expected_effects &= ~media::AudioParameters::NOISE_SUPPRESSION;
    }
  }

  expected_params.set_effects(expected_effects);

  // Connect the track, and expect the MockAudioCapturerSource to be
  // initialized and started by ProcessedLocalAudioSource.
  EXPECT_CALL(*mock_audio_capturer_source(),
              Initialize(ExactParamProcessingEffects(expected_params),
                         capture_source_callback()));
  EXPECT_CALL(*mock_audio_capturer_source(), Start())
      .WillOnce(Invoke(
          capture_source_callback(),
          &media::AudioCapturerSource::CaptureCallback::OnCaptureStarted));

  ASSERT_TRUE(audio_source()->ConnectToInitializedTrack(audio_track()));
}

INSTANTIATE_TEST_SUITE_P(
    All,
    ProcessedLocalAudioSourcePlatformEffectsTest,
    ::testing::Combine(
        ::testing::ValuesIn({AudioProcessingProperties::EchoCancellationType::
                                 kEchoCancellationDisabled,
                             AudioProcessingProperties::EchoCancellationType::
                                 kEchoCancellationSystem,
                             AudioProcessingProperties::EchoCancellationType::
                                 kEchoCancellationAec3}),
        // ACG and NS on/off.
        ::testing::Bool(),
        ::testing::Bool()));

}  // namespace blink
