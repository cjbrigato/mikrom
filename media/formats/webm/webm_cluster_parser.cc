// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40285824): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include "media/formats/webm/webm_cluster_parser.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/numerics/byte_conversions.h"
#include "base/numerics/checked_math.h"
#include "base/numerics/safe_conversions.h"
#include "base/types/optional_util.h"
#include "media/base/decrypt_config.h"
#include "media/base/stream_parser_buffer.h"
#include "media/base/timestamp_constants.h"
#include "media/base/webvtt_util.h"
#include "media/formats/webm/webm_constants.h"
#include "media/formats/webm/webm_crypto_helpers.h"
#include "media/formats/webm/webm_webvtt_parser.h"

namespace media {

enum {
  // Limits the number of MEDIA_LOG() calls in the path of reading encoded
  // duration to avoid spamming for corrupted data.
  kMaxDurationErrorLogs = 10,
  // Limits the number of MEDIA_LOG() calls warning the user that buffer
  // durations have been estimated.
  kMaxDurationEstimateLogs = 10,
};

WebMClusterParser::WebMClusterParser(
    int64_t timecode_scale_ns,
    int audio_track_num,
    base::TimeDelta audio_default_duration,
    int video_track_num,
    base::TimeDelta video_default_duration,
    const std::set<int64_t>& ignored_tracks,
    const std::string& audio_encryption_key_id,
    const std::string& video_encryption_key_id,
    const AudioCodec audio_codec,
    MediaLog* media_log)
    : timecode_multiplier_(timecode_scale_ns / 1000.0),
      ignored_tracks_(ignored_tracks),
      audio_encryption_key_id_(audio_encryption_key_id),
      video_encryption_key_id_(video_encryption_key_id),
      audio_codec_(audio_codec),
      parser_(kWebMIdCluster, this),
      cluster_start_time_(kNoTimestamp),
      audio_(audio_track_num,
             TrackType::AUDIO,
             audio_default_duration,
             media_log),
      video_(video_track_num,
             TrackType::VIDEO,
             video_default_duration,
             media_log),
      ready_buffer_upper_bound_(kNoDecodeTimestamp),
      media_log_(media_log) {
}

WebMClusterParser::~WebMClusterParser() = default;

void WebMClusterParser::Reset() {
  last_block_timecode_.reset();
  cluster_timecode_ = -1;
  cluster_start_time_ = kNoTimestamp;
  cluster_ended_ = false;
  parser_.Reset();
  audio_.Reset();
  video_.Reset();
  ready_buffer_upper_bound_ = kNoDecodeTimestamp;
}

int WebMClusterParser::Parse(const uint8_t* buf, int size) {
  audio_.ClearReadyBuffers();
  video_.ClearReadyBuffers();
  ready_buffer_upper_bound_ = kNoDecodeTimestamp;

  int result = parser_.Parse(buf, size);

  if (result < 0) {
    cluster_ended_ = false;
    return result;
  }

  cluster_ended_ = parser_.IsParsingComplete();
  if (cluster_ended_) {
    // If there were no buffers in this cluster, set the cluster start time to
    // be the |cluster_timecode_|.
    if (cluster_start_time_ == kNoTimestamp) {
      // If the cluster did not even have a |cluster_timecode_|, signal parse
      // error.
      if (cluster_timecode_ < 0)
        return -1;

      cluster_start_time_ =
          base::Microseconds(cluster_timecode_ * timecode_multiplier_);
    }

    // Reset the parser if we're done parsing so that
    // it is ready to accept another cluster on the next
    // call.
    parser_.Reset();

    last_block_timecode_.reset();
    cluster_timecode_ = -1;
  }

  return result;
}

void WebMClusterParser::GetBuffers(StreamParser::BufferQueueMap* buffers) {
  DCHECK(buffers->empty());
  if (ready_buffer_upper_bound_ == kNoDecodeTimestamp)
    UpdateReadyBuffers();
  const BufferQueue& audio_buffers = audio_.ready_buffers();
  if (!audio_buffers.empty()) {
    buffers->insert(std::make_pair(audio_.track_num(), audio_buffers));
  }
  const BufferQueue& video_buffers = video_.ready_buffers();
  if (!video_buffers.empty()) {
    buffers->insert(std::make_pair(video_.track_num(), video_buffers));
  }
}

base::TimeDelta WebMClusterParser::TryGetEncodedAudioDuration(
    const uint8_t* data,
    int size) {

  // Duration is currently read assuming the *entire* stream is unencrypted.
  // The special "Signal Byte" prepended to Blocks in encrypted streams is
  // assumed to not be present.
  // TODO(chcunningham): Consider parsing "Signal Byte" for encrypted streams
  // to return duration for any unencrypted blocks.

  if (audio_codec_ == AudioCodec::kOpus) {
    return ReadOpusDuration(data, size);
  }

  // TODO(wolenetz/chcunningham): Implement duration reading for Vorbis. See
  // motivations in http://crbug.com/396634.

  return kNoTimestamp;
}

base::TimeDelta WebMClusterParser::ReadOpusDuration(const uint8_t* data,
                                                    int size) {
  // Masks and constants for Opus packets. See
  // https://tools.ietf.org/html/rfc6716#page-14
  static const uint8_t kTocConfigMask = 0xf8;
  static const uint8_t kTocFrameCountCodeMask = 0x03;
  static const uint8_t kFrameCountMask = 0x3f;
  static const base::TimeDelta kPacketDurationMax = base::Milliseconds(120);

  if (size < 1) {
    LIMITED_MEDIA_LOG(DEBUG, media_log_, num_duration_errors_,
                      kMaxDurationErrorLogs)
        << "Invalid zero-byte Opus packet; demuxed block duration may be "
           "imprecise.";
    return kNoTimestamp;
  }

  // Frame count type described by last 2 bits of Opus TOC byte.
  int frame_count_type = data[0] & kTocFrameCountCodeMask;

  int frame_count = 0;
  switch (frame_count_type) {
    case 0:
      frame_count = 1;
      break;
    case 1:
    case 2:
      frame_count = 2;
      break;
    case 3:
      // Type 3 indicates an arbitrary frame count described in the next byte.
      if (size < 2) {
        LIMITED_MEDIA_LOG(DEBUG, media_log_, num_duration_errors_,
                          kMaxDurationErrorLogs)
            << "Second byte missing from 'Code 3' Opus packet; demuxed block "
               "duration may be imprecise.";
        return kNoTimestamp;
      }

      frame_count = data[1] & kFrameCountMask;

      if (frame_count == 0) {
        LIMITED_MEDIA_LOG(DEBUG, media_log_, num_duration_errors_,
                          kMaxDurationErrorLogs)
            << "Illegal 'Code 3' Opus packet with frame count zero; demuxed "
               "block duration may be imprecise.";
        return kNoTimestamp;
      }

      break;
    default:
      LIMITED_MEDIA_LOG(DEBUG, media_log_, num_duration_errors_,
                        kMaxDurationErrorLogs)
          << "Unexpected Opus frame count type: " << frame_count_type << "; "
          << "demuxed block duration may be imprecise.";
      return kNoTimestamp;
  }

  int opusConfig = (data[0] & kTocConfigMask) >> 3;
  CHECK_GE(opusConfig, 0);
  CHECK_LT(opusConfig, static_cast<int>(std::size(kOpusFrameDurationsMu)));

  DCHECK_GT(frame_count, 0);
  base::TimeDelta duration =
      base::Microseconds(kOpusFrameDurationsMu[opusConfig] * frame_count);

  if (duration > kPacketDurationMax) {
    // Intentionally allowing packet to pass through for now. Decoder should
    // either handle or fail gracefully. MEDIA_LOG as breadcrumbs in case
    // things go sideways.
    LIMITED_MEDIA_LOG(DEBUG, media_log_, num_duration_errors_,
                      kMaxDurationErrorLogs)
        << "Warning, demuxed Opus packet with encoded duration: "
        << duration.InMilliseconds() << "ms. Should be no greater than "
        << kPacketDurationMax.InMilliseconds() << "ms.";
  }

  return duration;
}

WebMParserClient* WebMClusterParser::OnListStart(int id) {
  if (id == kWebMIdCluster) {
    cluster_timecode_ = -1;
    cluster_start_time_ = kNoTimestamp;
  } else if (id == kWebMIdBlockGroup) {
    block_data_.reset();
    block_duration_ = -1;
    discard_padding_ = -1;
    discard_padding_set_ = false;
    reference_block_set_ = false;
  } else if (id == kWebMIdBlockAdditions) {
    block_add_id_ = -1;
    block_additional_data_.reset();
  }

  return this;
}

bool WebMClusterParser::OnListEnd(int id) {
  if (id != kWebMIdBlockGroup)
    return true;

  // Make sure the BlockGroup actually had a Block.
  if (!block_data_) {
    MEDIA_LOG(ERROR, media_log_) << "Block missing from BlockGroup.";
    return false;
  }

  base::span<uint8_t> data;
  if (block_data_.has_value()) {
    data = base::span(block_data_.value());
  }
  base::span<uint8_t> additional;
  if (block_additional_data_.has_value()) {
    additional = base::span(block_additional_data_.value());
  }

  bool result = ParseBlock(false, data.data(), data.size(), additional.data(),
                           additional.size(), block_duration_,
                           discard_padding_set_ ? discard_padding_ : 0,
                           reference_block_set_);
  block_data_.reset();
  block_duration_ = -1;
  block_add_id_ = -1;
  block_additional_data_.reset();
  discard_padding_ = -1;
  discard_padding_set_ = false;
  reference_block_set_ = false;
  return result;
}

bool WebMClusterParser::OnUInt(int id, int64_t val) {
  int64_t* dst;
  switch (id) {
    case kWebMIdTimecode:
      dst = &cluster_timecode_;
      break;
    case kWebMIdBlockDuration:
      dst = &block_duration_;
      break;
    case kWebMIdBlockAddID:
      dst = &block_add_id_;
      break;
    default:
      return true;
  }
  if (*dst != -1)
    return false;
  *dst = val;
  return true;
}

bool WebMClusterParser::ParseBlock(bool is_simple_block,
                                   const uint8_t* buf,
                                   size_t size,
                                   const uint8_t* additional,
                                   int additional_size,
                                   int duration,
                                   int64_t discard_padding,
                                   bool reference_block_set) {
  const size_t kBlockHeaderSize = 4;
  if (size < kBlockHeaderSize) {
    return false;
  }

  // Return an error if the trackNum > 127. We just aren't
  // going to support large track numbers right now.
  if (!(buf[0] & 0x80)) {
    MEDIA_LOG(ERROR, media_log_) << "TrackNumber over 127 not supported";
    return false;
  }

  int track_num = buf[0] & 0x7f;
  int timecode = buf[1] << 8 | buf[2];
  int flags = buf[3] & 0xff;
  int lacing = (flags >> 1) & 0x3;

  if (lacing) {
    MEDIA_LOG(ERROR, media_log_) << "Lacing " << lacing
                                 << " is not supported yet.";
    return false;
  }

  // Sign extend negative timecode offsets.
  if (timecode & 0x8000)
    timecode |= ~0xffff;

  // The first bit of the flags is set when a SimpleBlock contains only
  // keyframes. If this is a Block, then keyframe is inferred by the absence of
  // the ReferenceBlock Element.
  // http://www.matroska.org/technical/specs/index.html
  bool is_keyframe =
      is_simple_block ? (flags & 0x80) != 0 : !reference_block_set;

  const uint8_t* frame_data = buf + kBlockHeaderSize;
  size_t frame_size = size - kBlockHeaderSize;
  return OnBlock(is_simple_block, track_num, timecode, duration, frame_data,
                 frame_size, additional, additional_size, discard_padding,
                 is_keyframe);
}

bool WebMClusterParser::OnBinary(int id, const uint8_t* data_ptr, int size) {
  auto data =
      // TODO(crbug.com/40284755): This function should receive a span, not a
      // pointer/size pair.
      UNSAFE_TODO(base::span(data_ptr, base::checked_cast<size_t>(size)));
  switch (id) {
    case kWebMIdSimpleBlock:
      return ParseBlock(true, data.data(), data.size(), nullptr, 0, -1, 0,
                        false);

    case kWebMIdBlock:
      if (block_data_) {
        MEDIA_LOG(ERROR, media_log_)
            << "More than 1 Block in a BlockGroup is not "
               "supported.";
        return false;
      }
      block_data_ = base::HeapArray<uint8_t>::Uninit(data.size());
      base::span(*block_data_).copy_from(data);
      return true;

    case kWebMIdBlockAdditional: {
      uint64_t block_add_id = base::ByteSwap(block_add_id_);
      if (block_additional_data_) {
        // TODO(vigneshv): Technically, more than 1 BlockAdditional is allowed
        // as per matroska spec. But for now we don't have a use case to
        // support parsing of such files. Take a look at this again when such a
        // case arises.
        MEDIA_LOG(ERROR, media_log_) << "More than 1 BlockAdditional in a "
                                        "BlockGroup is not supported.";
        return false;
      }
      // First 8 bytes of side_data in DecoderBuffer is the BlockAddID
      // element's value in Big Endian format. This is done to mimic ffmpeg
      // demuxer's behavior.
      block_additional_data_ =
          base::HeapArray<uint8_t>::Uninit(sizeof(block_add_id) + data.size());
      auto [additional_id, additional_data] =
          base::span(*block_additional_data_).split_at<sizeof(block_add_id)>();
      additional_id.copy_from(base::byte_span_from_ref(block_add_id));
      additional_data.copy_from(data);
      return true;
    }
    case kWebMIdDiscardPadding: {
      if (discard_padding_set_ || data.empty() || data.size() > 8u) {
        return false;
      }
      discard_padding_set_ = true;

      // Read in the big-endian integer. There may be less than 8 bytes, so we
      // place them at the back of the array, in the LSB positions.
      uint8_t bytes[8u] = {};
      base::span(bytes).last(data.size()).copy_from(data);
      discard_padding_ = base::I64FromBigEndian(bytes);
      return true;
    }
    case kWebMIdReferenceBlock: {
      // We use ReferenceBlock to determine whether the current Block contains a
      // keyframe or not. Other than that, we don't care about the value of the
      // ReferenceBlock element itself.
      reference_block_set_ = true;
      return true;
    }
    default:
      return true;
  }
}

bool WebMClusterParser::OnBlock(bool is_simple_block,
                                int track_num,
                                int timecode,
                                int block_duration,
                                const uint8_t* data,
                                size_t size,
                                const uint8_t* additional,
                                size_t additional_size,
                                int64_t discard_padding,
                                bool is_keyframe) {
  if (cluster_timecode_ == -1) {
    MEDIA_LOG(ERROR, media_log_) << "Got a block before cluster timecode.";
    return false;
  }

  if (last_block_timecode_.has_value() && timecode < *last_block_timecode_) {
    MEDIA_LOG(ERROR, media_log_)
        << "Got a block with a timecode before the previous block.";
    return false;
  }

  Track* track = nullptr;
  StreamParserBuffer::Type buffer_type = DemuxerStream::AUDIO;
  std::string encryption_key_id;
  base::TimeDelta encoded_duration = kNoTimestamp;
  if (track_num == audio_.track_num()) {
    track = &audio_;
    encryption_key_id = audio_encryption_key_id_;
    if (encryption_key_id.empty()) {
      encoded_duration = TryGetEncodedAudioDuration(data, size);
    }
  } else if (track_num == video_.track_num()) {
    track = &video_;
    encryption_key_id = video_encryption_key_id_;
    buffer_type = DemuxerStream::VIDEO;
  } else if (ignored_tracks_.find(track_num) != ignored_tracks_.end()) {
    return true;
  } else {
    MEDIA_LOG(ERROR, media_log_) << "Unexpected track number " << track_num;
    return false;
  }

  last_block_timecode_ = timecode;

  int64_t microseconds;

  if (!base::CheckMul(base::CheckAdd(cluster_timecode_, timecode),
                      timecode_multiplier_)
           .AssignIfValid(&microseconds)) {
    MEDIA_LOG(ERROR, media_log_) << "Invalid cluster timecode.";
    return false;
  }

  base::TimeDelta timestamp = base::Microseconds(microseconds);

  if (timestamp == kNoTimestamp || timestamp == kInfiniteDuration) {
    MEDIA_LOG(ERROR, media_log_) << "Invalid block timestamp.";
    return false;
  }

  // Every encrypted Block has a signal byte and IV prepended to it.
  // See: http://www.webmproject.org/docs/webm-encryption/
  std::unique_ptr<DecryptConfig> decrypt_config;
  size_t data_offset = 0;
  if (!encryption_key_id.empty() &&
      !WebMCreateDecryptConfig(
          data, size,
          reinterpret_cast<const uint8_t*>(encryption_key_id.data()),
          encryption_key_id.size(), &decrypt_config, &data_offset)) {
    MEDIA_LOG(ERROR, media_log_) << "Failed to extract decrypt config.";
    return false;
  }

  // TODO(wolenetz/acolwell): Validate and use a common cross-parser TrackId
  // type with remapped bytestream track numbers and allow multiple tracks as
  // applicable. See https://crbug.com/341581.
  auto data_span = base::span(data, size).subspan(data_offset);
  auto buffer = StreamParserBuffer::CopyFrom(data_span, is_keyframe,
                                             buffer_type, track_num);
  if (additional_size) {
    buffer->WritableSideData().alpha_data =
        base::HeapArray<uint8_t>::CopiedFrom(
            base::span<const uint8_t>(additional, additional_size));
  }

  if (decrypt_config) {
    buffer->set_decrypt_config(std::move(decrypt_config));
  }

  buffer->set_timestamp(timestamp);
  if (cluster_start_time_ == kNoTimestamp)
    cluster_start_time_ = timestamp;

  base::TimeDelta block_duration_time_delta = kNoTimestamp;
  if (block_duration >= 0) {
    block_duration_time_delta =
        base::Microseconds(block_duration * timecode_multiplier_);
  }

  // Prefer encoded duration over BlockGroup->BlockDuration or
  // TrackEntry->DefaultDuration when available. This layering violation is a
  // workaround for http://crbug.com/396634, decreasing the likelihood of
  // fall-back to rough estimation techniques for Blocks that lack a
  // BlockDuration at the end of a cluster. Cross cluster durations are not
  // feasible given flexibility of cluster ordering and MSE APIs. Duration
  // estimation may still apply in cases of encryption and codecs for which
  // we do not extract encoded duration. Within a cluster, estimates are applied
  // as Block Timecode deltas, or once the whole cluster is parsed in the case
  // of the last Block in the cluster. See Track::AddBuffer and
  // ApplyDurationEstimateIfNeeded().
  if (encoded_duration != kNoTimestamp) {
    DCHECK(encoded_duration != kInfiniteDuration);
    DCHECK(encoded_duration.is_positive());
    buffer->set_duration(encoded_duration);

    DVLOG(3) << __func__ << " : "
             << "Using encoded duration " << encoded_duration.InSecondsF();

    if (block_duration_time_delta != kNoTimestamp) {
      base::TimeDelta duration_difference =
          block_duration_time_delta - encoded_duration;

      const auto kWarnDurationDiff =
          base::Microseconds(timecode_multiplier_ * 2);
      if (duration_difference.magnitude() > kWarnDurationDiff) {
        LIMITED_MEDIA_LOG(DEBUG, media_log_, num_duration_errors_,
                          kMaxDurationErrorLogs)
            << "BlockDuration (" << block_duration_time_delta.InMilliseconds()
            << "ms) differs significantly from encoded duration ("
            << encoded_duration.InMilliseconds() << "ms).";
      }
    }
  } else if (block_duration_time_delta != kNoTimestamp &&
             block_duration_time_delta != kInfiniteDuration) {
    buffer->set_duration(block_duration_time_delta);
  } else {
    buffer->set_duration(track->default_duration());
  }

  // TODO(wolenetz): Is this correct for negative |discard_padding|? See
  // https://crbug.com/969195.
  if (discard_padding != 0) {
    buffer->set_discard_padding(std::make_pair(
        base::TimeDelta(), base::Microseconds(discard_padding / 1000)));
  }

  return track->AddBuffer(std::move(buffer));
}

WebMClusterParser::Track::Track(int track_num,
                                TrackType track_type,
                                base::TimeDelta default_duration,
                                MediaLog* media_log)
    : track_num_(track_num),
      track_type_(track_type),
      default_duration_(default_duration),
      max_frame_duration_(kNoTimestamp),
      media_log_(media_log) {
  DCHECK(default_duration_ == kNoTimestamp || default_duration_.is_positive());
}

WebMClusterParser::Track::Track(const Track& other) = default;

WebMClusterParser::Track::~Track() = default;

DecodeTimestamp WebMClusterParser::Track::GetReadyUpperBound() {
  DCHECK(ready_buffers_.empty());
  if (last_added_buffer_missing_duration_)
    return last_added_buffer_missing_duration_->GetDecodeTimestamp();

  return kMaxDecodeTimestamp;
}

void WebMClusterParser::Track::ExtractReadyBuffers(
    const DecodeTimestamp before_timestamp) {
  DCHECK(ready_buffers_.empty());
  DCHECK(kNoDecodeTimestamp < before_timestamp);

  if (buffers_.empty())
    return;

  if (buffers_.back()->GetDecodeTimestamp() < before_timestamp) {
    // All of |buffers_| are ready.
    ready_buffers_.swap(buffers_);
    DVLOG(3) << __func__ << " : " << track_num_ << " All "
             << ready_buffers_.size() << " are ready: before upper bound ts "
             << before_timestamp.InSecondsF();
    return;
  }

  // Not all of |buffers_| are ready yet. Move any that are ready to
  // |ready_buffers_|.
  while (true) {
    if (buffers_.front()->GetDecodeTimestamp() >= before_timestamp)
      break;
    ready_buffers_.emplace_back(std::move(buffers_.front()));
    buffers_.pop_front();
    DCHECK(!buffers_.empty());
  }

  DVLOG(3) << __func__ << " : " << track_num_ << " Only "
           << ready_buffers_.size() << " ready, " << buffers_.size()
           << " at or after upper bound ts " << before_timestamp.InSecondsF();
}

bool WebMClusterParser::Track::AddBuffer(
    scoped_refptr<StreamParserBuffer> buffer) {
  DVLOG(2) << "AddBuffer() : " << track_num_ << " ts "
           << buffer->timestamp().InSecondsF() << " dur "
           << buffer->duration().InSecondsF() << " kf "
           << buffer->is_key_frame() << " size " << buffer->size();

  if (last_added_buffer_missing_duration_) {
    base::TimeDelta derived_duration =
        buffer->timestamp() - last_added_buffer_missing_duration_->timestamp();
    if (derived_duration == kInfiniteDuration) {
      DVLOG(2) << "Duration of last buffer is too large.";
      return false;
    }

    last_added_buffer_missing_duration_->set_duration(derived_duration);

    DVLOG(2) << "AddBuffer() : applied derived duration to held-back buffer : "
             << " ts "
             << last_added_buffer_missing_duration_->timestamp().InSecondsF()
             << " dur "
             << last_added_buffer_missing_duration_->duration().InSecondsF()
             << " kf " << last_added_buffer_missing_duration_->is_key_frame()
             << " size " << last_added_buffer_missing_duration_->size();
    if (!QueueBuffer(std::move(last_added_buffer_missing_duration_)))
      return false;
  }

  if (buffer->duration() == kNoTimestamp) {
    last_added_buffer_missing_duration_ = std::move(buffer);
    DVLOG(2) << "AddBuffer() : holding back buffer that is missing duration";
    return true;
  }

  return QueueBuffer(std::move(buffer));
}

void WebMClusterParser::Track::ApplyDurationEstimateIfNeeded() {
  if (!last_added_buffer_missing_duration_)
    return;

  last_added_buffer_missing_duration_->set_duration(GetDurationEstimate());
  last_added_buffer_missing_duration_->set_is_duration_estimated(true);

  LIMITED_MEDIA_LOG(INFO, media_log_, num_duration_estimates_,
                    kMaxDurationEstimateLogs)
      << "Estimating WebM block duration="
      << last_added_buffer_missing_duration_->duration().InMilliseconds()
      << "ms for the last (Simple)Block in the Cluster for this Track (PTS="
      << last_added_buffer_missing_duration_->timestamp().InMilliseconds()
      << "ms). Use BlockGroups with BlockDurations at the end of each Cluster "
      << "to avoid estimation.";

  DVLOG(2) << __func__ << " new dur : ts "
           << last_added_buffer_missing_duration_->timestamp().InSecondsF()
           << " dur "
           << last_added_buffer_missing_duration_->duration().InSecondsF()
           << " kf " << last_added_buffer_missing_duration_->is_key_frame()
           << " size " << last_added_buffer_missing_duration_->size();

  // Don't use the applied duration as a future estimation (don't use
  // QueueBuffer() here.)
  buffers_.emplace_back(std::move(last_added_buffer_missing_duration_));
}

void WebMClusterParser::Track::ClearReadyBuffers() {
  // Note that |buffers_| are kept and |{min|max}_frame_duration_| is not
  // reset here.
  ready_buffers_.clear();
}

void WebMClusterParser::Track::Reset() {
  ClearReadyBuffers();
  buffers_.clear();
  last_added_buffer_missing_duration_.reset();
}

bool WebMClusterParser::Track::QueueBuffer(
    scoped_refptr<StreamParserBuffer> buffer) {
  DCHECK(!last_added_buffer_missing_duration_);

  // WebMClusterParser::OnBlock() gives MEDIA_LOG and parse error on decreasing
  // block timecode detection within a cluster. Therefore, we should not see
  // those here.
  CHECK(buffers_.empty() ||
        buffers_.back()->GetDecodeTimestamp() <= buffer->GetDecodeTimestamp());

  base::TimeDelta duration = buffer->duration();
  if (duration.is_negative() || duration == kNoTimestamp) {
    MEDIA_LOG(ERROR, media_log_)
        << "Invalid buffer duration: " << duration.InSecondsF();
    return false;
  }

  if (duration.is_positive()) {
    base::TimeDelta orig_max_duration = max_frame_duration_;

    if (max_frame_duration_ == kNoTimestamp) {
      max_frame_duration_ = duration;
    } else {
      max_frame_duration_ = std::max(max_frame_duration_, duration);
    }

    if (max_frame_duration_ != orig_max_duration) {
      DVLOG(3) << "Updated max duration estimate:" << orig_max_duration
               << " -> " << max_frame_duration_ << " at timestamp: "
               << buffer->GetDecodeTimestamp().InSecondsF();
    }
  }

  buffers_.push_back(std::move(buffer));
  return true;
}

base::TimeDelta WebMClusterParser::Track::GetDurationEstimate() {
  base::TimeDelta duration;

  if (max_frame_duration_ == kNoTimestamp) {
    DVLOG(3) << __func__ << " : using hardcoded default duration";
    if (track_type_ == TrackType::AUDIO) {
      duration = base::Milliseconds(kDefaultAudioBufferDurationInMs);
    } else {
      // Video tracks use the larger video default duration.
      duration = base::Milliseconds(kDefaultVideoBufferDurationInMs);
    }
  } else {
    // Use max duration to minimize the risk of introducing gaps in the buffered
    // range. For audio, this is still safe because overlap trimming is not
    // applied to buffers where is_duration_estimated() = true.
    duration = max_frame_duration_;
  }

  DCHECK(duration.is_positive());
  DCHECK(duration != kNoTimestamp);
  return duration;
}

void WebMClusterParser::UpdateReadyBuffers() {
  DCHECK(ready_buffer_upper_bound_ == kNoDecodeTimestamp);

  if (cluster_ended_) {
    audio_.ApplyDurationEstimateIfNeeded();
    video_.ApplyDurationEstimateIfNeeded();
    ready_buffer_upper_bound_ = kMaxDecodeTimestamp;
    DCHECK(ready_buffer_upper_bound_ == audio_.GetReadyUpperBound());
    DCHECK(ready_buffer_upper_bound_ == video_.GetReadyUpperBound());
  } else {
    ready_buffer_upper_bound_ = std::min(audio_.GetReadyUpperBound(),
                                         video_.GetReadyUpperBound());
    DCHECK(kNoDecodeTimestamp < ready_buffer_upper_bound_);
  }

  // Prepare each track's ready buffers for retrieval.
  audio_.ExtractReadyBuffers(ready_buffer_upper_bound_);
  video_.ExtractReadyBuffers(ready_buffer_upper_bound_);
}

}  // namespace media
