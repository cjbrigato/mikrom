// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/metrics/power/battery_discharge_reporter.h"

#include <utility>

#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/metrics/histogram_functions.h"
#include "chrome/browser/metrics/power/power_metrics.h"
#include "chrome/browser/metrics/power/process_metrics_recorder_util.h"
#include "chrome/browser/metrics/usage_scenario/usage_scenario.h"

#if BUILDFLAG(IS_MAC)
#include "base/metrics/histogram_functions.h"
#endif  // BUILDFLAG(IS_MAC)

namespace {

constexpr const char* kBatteryDischargeIsValidSampleInterval =
    "Power.BatteryDischargeReporter.IsValidSampleInterval";

bool IsWithinTolerance(base::TimeDelta value,
                       base::TimeDelta expected,
                       base::TimeDelta tolerance) {
  return (value - expected).magnitude() < tolerance;
}

}  // namespace

BatteryDischargeReporter::BatteryDischargeReporter(
    base::BatteryStateSampler* battery_state_sampler,
    UsageScenarioDataStore* battery_usage_scenario_data_store)
    : sample_interval_(battery_state_sampler->GetSampleInterval()),
      battery_usage_scenario_data_store_(battery_usage_scenario_data_store) {
  // Check that the sample interval evenly divides 1 minute.
  CHECK_EQ(base::Minutes(1) % GetSampleInterval(), base::TimeDelta());

  if (!battery_usage_scenario_data_store_) {
    battery_usage_scenario_tracker_ = std::make_unique<UsageScenarioTracker>();
    battery_usage_scenario_data_store_ =
        battery_usage_scenario_tracker_->data_store();
  }

  scoped_battery_state_sampler_observation_.Observe(battery_state_sampler);
}

BatteryDischargeReporter::~BatteryDischargeReporter() = default;

void BatteryDischargeReporter::OnBatteryStateSampled(
    const std::optional<base::BatteryLevelProvider::BatteryState>&
        battery_state) {
  base::TimeTicks now_ticks = base::TimeTicks::Now();

  // First sampling event. Remember the time and skip.
  if (!last_sample_time_) {
    last_sample_time_ = now_ticks;
    StartNewOneMinuteInterval(battery_state);
#if BUILDFLAG(IS_WIN)
    StartNewTenMinutesInterval(battery_state);
#endif  // BUILDFLAG(IS_WIN)
    return;
  }

  base::TimeDelta current_sample_interval = now_ticks - *last_sample_time_;
  last_sample_time_ = now_ticks;

  if (!IsValidSampleInterval(current_sample_interval)) {
    // The sample interval is invalid. Maybe the computer when to sleep. Don't
    // trust the data.
    StartNewOneMinuteInterval(battery_state);
#if BUILDFLAG(IS_WIN)
    StartNewTenMinutesInterval(battery_state);
#endif  // BUILDFLAG(IS_WIN)
    return;
  }

  // One minute interval.
  ++one_minute_sample_count_;
  one_minute_interval_duration_ += current_sample_interval;

  if (one_minute_sample_count_ == (base::Minutes(1) / GetSampleInterval())) {
#if BUILDFLAG(IS_MAC)
    RecordIOPMPowerSourceSampleEventDelta(one_minute_interval_duration_);
#endif
    ReportOneMinuteInterval(one_minute_interval_duration_, battery_state);
    StartNewOneMinuteInterval(battery_state);
    is_initial_interval_ = false;
  }

#if BUILDFLAG(IS_WIN)
  // Ten minutes interval.
  ++ten_minutes_sample_count_;
  ten_minutes_interval_duration_ += current_sample_interval;

  if (ten_minutes_sample_count_ == (base::Minutes(10) / GetSampleInterval())) {
    ReportTenMinutesInterval(ten_minutes_interval_duration_, battery_state);
    StartNewTenMinutesInterval(battery_state);
  }
#endif  // BUILDFLAG(IS_WIN)
}

base::TimeDelta BatteryDischargeReporter::GetSampleInterval() const {
  return sample_interval_;
}

bool BatteryDischargeReporter::IsValidSampleInterval(
    base::TimeDelta interval_duration) {
  bool is_valid_sample_interval = IsWithinTolerance(
      interval_duration, GetSampleInterval(), base::Seconds(1));
  base::UmaHistogramBoolean(kBatteryDischargeIsValidSampleInterval,
                            is_valid_sample_interval);
  return is_valid_sample_interval;
}

void BatteryDischargeReporter::StartNewOneMinuteInterval(
    const std::optional<base::BatteryLevelProvider::BatteryState>&
        battery_state) {
  one_minute_sample_count_ = 0;
  one_minute_interval_duration_ = base::TimeDelta();
  one_minute_interval_start_battery_state_ = battery_state;
}

#if BUILDFLAG(IS_WIN)
void BatteryDischargeReporter::StartNewTenMinutesInterval(
    const std::optional<base::BatteryLevelProvider::BatteryState>&
        battery_state) {
  ten_minutes_sample_count_ = 0;
  ten_minutes_interval_duration_ = base::TimeDelta();
  ten_minutes_interval_start_battery_state_ = battery_state;
}
#endif  // BUILDFLAG(IS_WIN)

void BatteryDischargeReporter::ReportOneMinuteInterval(
    base::TimeDelta interval_duration,
    const std::optional<base::BatteryLevelProvider::BatteryState>&
        battery_state) {
  // Evaluate battery discharge mode and rate.
  auto battery_discharge = GetBatteryDischargeDuringInterval(
      one_minute_interval_start_battery_state_, battery_state,
      interval_duration);

#if BUILDFLAG(IS_WIN)
  if (battery_discharge.mode == BatteryDischargeMode::kDischarging) {
    base::UmaHistogramBoolean(
        "Power.BatteryDischargeGranularityAvailable",
        battery_state->battery_discharge_granularity.has_value());

    if (battery_state->battery_discharge_granularity.has_value()) {
      base::UmaHistogramCustomCounts(
          "Power.BatteryDischargeGranularityMilliwattHours2",
          battery_state->battery_discharge_granularity.value(),
          /*min=*/0, /*exclusive_max=*/20000,
          /*buckets=*/50);

      uint32_t granularity_relative =
          battery_state->battery_discharge_granularity.value() * 10000 /
          battery_state->full_charged_capacity.value();
      base::UmaHistogramCustomCounts(
          "Power.BatteryDischargeGranularityRelative2", granularity_relative,
          /*min=*/0, /*exclusive_max=*/20000,
          /*buckets=*/50);
    }
  }
#endif

  auto interval_data = battery_usage_scenario_data_store_->ResetIntervalData();

  // Get scenario data.
  const auto long_interval_scenario_params =
      GetLongIntervalScenario(interval_data);
  // Histograms are recorded without suffix and with a scenario-specific
  // suffix.
  const std::vector<const char*> long_interval_suffixes{
      "", long_interval_scenario_params.histogram_suffix};
  ReportBatteryHistograms(interval_duration, battery_discharge,
                          is_initial_interval_, long_interval_suffixes);
}

#if BUILDFLAG(IS_WIN)
void BatteryDischargeReporter::ReportTenMinutesInterval(
    base::TimeDelta interval_duration,
    const std::optional<base::BatteryLevelProvider::BatteryState>&
        battery_state) {
  auto battery_discharge = GetBatteryDischargeDuringInterval(
      ten_minutes_interval_start_battery_state_, battery_state,
      interval_duration);

  ReportBatteryHistogramsTenMinutesInterval(interval_duration,
                                            battery_discharge);
}
#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_MAC)
void BatteryDischargeReporter::RecordIOPMPowerSourceSampleEventDelta(
    base::TimeDelta sampling_event_delta) {
  // The delta is expected to be almost always 60 seconds. Split the buckets for
  // 0.2s granularity (10s interval with 50 buckets + 1 underflow bucket + 1
  // overflow bucket) around that value.
  base::HistogramBase* histogram = base::LinearHistogram::FactoryTimeGet(
      "Power.IOPMPowerSource.SamplingEventDelta",
      /*min=*/base::Seconds(55), /*max=*/base::Seconds(65), /*buckets=*/52,
      base::HistogramBase::kUmaTargetedHistogramFlag);
  histogram->AddTime(sampling_event_delta);

  // Same as the above but using a range that starts from zero and significantly
  // goes beyond the expected mean time of |sampling_event_delta| (which is 60
  // seconds.).
  base::UmaHistogramMediumTimes(
      "Power.IOPMPowerSource.SamplingEventDelta.MediumTimes",
      sampling_event_delta);
}
#endif  // BUILDFLAG(IS_MAC)
