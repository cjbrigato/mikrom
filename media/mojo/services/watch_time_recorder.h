// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_MOJO_SERVICES_WATCH_TIME_RECORDER_H_
#define MEDIA_MOJO_SERVICES_WATCH_TIME_RECORDER_H_

#include <stdint.h>

#include <string_view>

#include "base/compiler_specific.h"
#include "base/containers/flat_map.h"
#include "base/time/time.h"
#include "media/base/audio_codecs.h"
#include "media/base/picture_in_picture_events_info.h"
#include "media/base/pipeline_status.h"
#include "media/base/video_codecs.h"
#include "media/mojo/mojom/watch_time_recorder.mojom.h"
#include "media/mojo/services/media_mojo_export.h"
#include "services/metrics/public/cpp/ukm_source_id.h"

namespace media {

// See mojom::WatchTimeRecorder for documentation.
class MEDIA_MOJO_EXPORT WatchTimeRecorder : public mojom::WatchTimeRecorder {
 public:
  WatchTimeRecorder(
      PictureInPictureEventsInfo::AutoPipReasonCallback auto_pip_reason_cb,
      mojom::PlaybackPropertiesPtr properties,
      ukm::SourceId source_id,
      bool is_top_frame,
      uint64_t player_id);

  WatchTimeRecorder(const WatchTimeRecorder&) = delete;
  WatchTimeRecorder& operator=(const WatchTimeRecorder&) = delete;

  ~WatchTimeRecorder() override;

  // mojom::WatchTimeRecorder implementation:
  void RecordWatchTime(WatchTimeKey key, base::TimeDelta watch_time) override;
  void FinalizeWatchTime(
      const std::vector<WatchTimeKey>& watch_time_keys) override;
  void OnError(const PipelineStatus& status) override;
  void UpdateSecondaryProperties(
      mojom::SecondaryPlaybackPropertiesPtr secondary_properties) override;
  void SetAutoplayInitiated(bool value) override;
  void OnDurationChanged(base::TimeDelta duration) override;
  void UpdateVideoDecodeStats(uint32_t video_frames_decoded,
                              uint32_t video_frames_dropped) override;
  void UpdateUnderflowCount(int32_t total_count) override;
  void UpdateUnderflowDuration(int32_t total_completed_count,
                               base::TimeDelta total_duration) override;

 private:
  // Records a UKM event based on |aggregate_watch_time_info_|; only recorded
  // with a complete finalize (destruction or empty FinalizeWatchTime call).
  // Clears |aggregate_watch_time_info_| upon completion.
  void RecordUkmPlaybackData();
  bool ShouldRecordUma() const;

  // Records Auto Picture in Picture whatch time when appropriate. This watch
  // time has a subtle caveat: the Auto Picture in Picture reason may change
  // from the time the `WatchTimeReporter` tells `this` to record the watch time
  // and when it gets to actually record the watch time.
  //
  // This can happen when entering/exiting Picture in Picture before retrieving
  // the Auto Picture in Picture reason. In other words, although the
  // `WatchTimeReporter` has assigned the watch time to a Picture in Picture
  // display type, the Picture in Picture window can be opened/closed (and
  // therefore change the Auto Picture in Picture reason) by the time the `this`
  // receives the message.
  void MaybeRecordWatchTimeForAutoPipReason(WatchTimeKey key,
                                            base::TimeDelta watch_time);

  PictureInPictureEventsInfo::AutoPipReasonCallback auto_pip_reason_cb_;

  std::optional<PictureInPictureEventsInfo::AutoPipReason>
      current_auto_pip_reason_ = std::nullopt;

  const mojom::PlaybackPropertiesPtr properties_;

  const ukm::SourceId source_id_;

  // Are UKM reports for the main frame or for a subframe?
  const bool is_top_frame_;

  // The provider ID which constructed this recorder. Used to record a UKM entry
  // at destruction that can be correlated with the final status for the
  // associated WebMediaPlayerImpl instance.
  const uint64_t player_id_;

  // Mapping of WatchTime metric keys to MeanTimeBetweenRebuffers (MTBR), smooth
  // rate (had zero rebuffers), and discard (<7s watch time) keys.
  struct ExtendedMetricsKeyMap {
    ExtendedMetricsKeyMap(const ExtendedMetricsKeyMap& copy);
    ExtendedMetricsKeyMap(WatchTimeKey watch_time_key,
                          std::string_view mtbr_key,
                          std::string_view smooth_rate_key,
                          std::string_view discard_key);
    const WatchTimeKey watch_time_key;
    const std::string_view mtbr_key;
    const std::string_view smooth_rate_key;
    const std::string_view discard_key;
  };
  const std::vector<ExtendedMetricsKeyMap> extended_metrics_keys_;

  using WatchTimeInfo = base::flat_map<WatchTimeKey, base::TimeDelta>;
  WatchTimeInfo watch_time_info_;

  // Aggregate record of all watch time for a given set of secondary properties.
  struct WatchTimeUkmRecord {
    explicit WatchTimeUkmRecord(
        mojom::SecondaryPlaybackPropertiesPtr properties);
    WatchTimeUkmRecord(WatchTimeUkmRecord&& record);
    ~WatchTimeUkmRecord();

    // Properties for this segment of UKM watch time.
    mojom::SecondaryPlaybackPropertiesPtr secondary_properties;

    // Sum of all watch time data since the last complete finalize.
    WatchTimeInfo aggregate_watch_time_info;

    // Total underflow count and duration for this segment of UKM watch time.
    int total_underflow_count = 0;
    int total_completed_underflow_count = 0;
    base::TimeDelta total_underflow_duration;

    uint32_t total_video_frames_decoded = 0;
    uint32_t total_video_frames_dropped = 0;
  };

  // List of all watch time segments. A new entry is added for every secondary
  // property update.
  std::vector<WatchTimeUkmRecord> ukm_records_;

  uint32_t video_frames_decoded_ = 0;
  uint32_t video_frames_dropped_ = 0;

  int underflow_count_ = 0;
  int completed_underflow_count_ = 0;
  base::TimeDelta underflow_duration_;

  PipelineStatusCodes pipeline_status_ = PIPELINE_OK;
  base::TimeDelta duration_ = kNoTimestamp;
  std::optional<bool> autoplay_initiated_;
};

}  // namespace media

#endif  // MEDIA_MOJO_SERVICES_WATCH_TIME_RECORDER_H_
