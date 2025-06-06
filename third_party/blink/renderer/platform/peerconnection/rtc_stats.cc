// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/peerconnection/rtc_stats.h"

#include <cstddef>
#include <memory>
#include <set>
#include <string>

#include "base/check_op.h"
#include "base/containers/contains.h"
#include "base/numerics/safe_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/platform/peerconnection/rtc_scoped_refptr_cross_thread_copier.h"
#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/blink/renderer/platform/wtf/text/string_hash.h"
#include "third_party/webrtc/api/stats/rtc_stats.h"
#include "third_party/webrtc/api/stats/rtcstats_objects.h"
#include "third_party/webrtc/rtc_base/ref_counted_object.h"

namespace blink {

RTCStatsReportPlatform::RTCStatsReportPlatform(
    const scoped_refptr<const webrtc::RTCStatsReport>& stats_report)
    : stats_report_(stats_report),
      it_(stats_report_->begin()),
      end_(stats_report_->end()) {
  DCHECK(stats_report_);
}

RTCStatsReportPlatform::~RTCStatsReportPlatform() {}

std::unique_ptr<RTCStatsReportPlatform> RTCStatsReportPlatform::CopyHandle()
    const {
  return std::make_unique<RTCStatsReportPlatform>(stats_report_);
}

const webrtc::RTCStats* RTCStatsReportPlatform::NextStats() {
  while (it_ != end_) {
    const webrtc::RTCStats& stat = *it_;
    ++it_;
    return &stat;
  }
  return nullptr;
}

size_t RTCStatsReportPlatform::Size() const {
  return stats_report_->size();
}

webrtc::scoped_refptr<webrtc::RTCStatsCollectorCallback>
CreateRTCStatsCollectorCallback(
    scoped_refptr<base::SingleThreadTaskRunner> main_thread,
    RTCStatsReportCallback callback) {
  return webrtc::scoped_refptr<RTCStatsCollectorCallbackImpl>(
      new webrtc::RefCountedObject<RTCStatsCollectorCallbackImpl>(
          std::move(main_thread), std::move(callback)));
}

RTCStatsCollectorCallbackImpl::RTCStatsCollectorCallbackImpl(
    scoped_refptr<base::SingleThreadTaskRunner> main_thread,
    RTCStatsReportCallback callback)
    : main_thread_(std::move(main_thread)), callback_(std::move(callback)) {}

RTCStatsCollectorCallbackImpl::~RTCStatsCollectorCallbackImpl() {
  DCHECK(!callback_);
}

void RTCStatsCollectorCallbackImpl::OnStatsDelivered(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
  PostCrossThreadTask(
      *main_thread_.get(), FROM_HERE,
      CrossThreadBindOnce(
          &RTCStatsCollectorCallbackImpl::OnStatsDeliveredOnMainThread,
          webrtc::scoped_refptr<RTCStatsCollectorCallbackImpl>(this), report));
}

void RTCStatsCollectorCallbackImpl::OnStatsDeliveredOnMainThread(
    webrtc::scoped_refptr<const webrtc::RTCStatsReport> report) {
  DCHECK(main_thread_->BelongsToCurrentThread());
  DCHECK(report);
  DCHECK(callback_);
  // Make sure the callback is destroyed in the main thread as well.
  std::move(callback_).Run(std::make_unique<RTCStatsReportPlatform>(
      base::WrapRefCounted(report.get())));
}

}  // namespace blink
