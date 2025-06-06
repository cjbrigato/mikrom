// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromecast/base/metrics/grouped_histogram.h"

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <string_view>

#include "base/check_op.h"
#include "base/metrics/histogram.h"
#include "base/metrics/statistics_recorder.h"
#include "base/no_destructor.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/lock.h"
#include "base/time/time.h"

namespace chromecast {
namespace metrics {

namespace {

const char kAppNameErrorNoApp[] = "ERROR_NO_APP_REGISTERED";

// Current app name guarded by lock.
struct CurrentAppNameWithLock {
  base::Lock lock;
  std::string app_name;
};

CurrentAppNameWithLock& GetCurrentApp() {
  static base::NoDestructor<CurrentAppNameWithLock> current_app;
  return *current_app;
}

std::string GetAppName() {
  base::AutoLock lock(GetCurrentApp().lock);
  const std::string& app_name = GetCurrentApp().app_name;
  return app_name.empty() ? kAppNameErrorNoApp : app_name;
}

struct HistogramArgs {
  base::DurableStringView durable_name;
  int minimum;
  int maximum;
  size_t bucket_count;

  template <size_t N>
  HistogramArgs(const char (&literal)[N], int min, int max, size_t count)
      : durable_name(std::string_view(literal, N - 1)),
        minimum(min),
        maximum(max),
        bucket_count(count) {}
};

// List of metrics to collect using a GroupedHistogram.
//
// When adding more Histograms to this list, find the source of the
// Histogram and look for the construction arguments it uses to add it in.
const std::array kHistogramsToGroup = {
    HistogramArgs("DNS.TotalTime", 1, 1000 * 60 * 60, 100),
    HistogramArgs("Net.DNS_Resolution_And_TCP_Connection_Latency2",
                  1,
                  1000 * 60 * 10,
                  100),
    HistogramArgs("Net.SSL_Connection_Latency2", 1, 1000 * 60, 100),
    HistogramArgs("Net.TCP_Connection_Latency", 1, 1000 * 60 * 10, 100),
    HistogramArgs("Net.HttpJob.TotalTime", 1, 1000 * 10, 50),
};

// This class is used to override a Histogram to generate per-app metrics.
// It intercepts calls to Add() for a given metric and generates new metrics
// of the form "<metric-name>.<app-name>".
class GroupedHistogram : public base::Histogram {
 public:
  // TODO(crbug.com/40824087): min/max parameters are redundant with "ranges"
  // and can probably be removed.
  GroupedHistogram(base::DurableStringView metric_to_override,
                   Sample32 minimum,
                   Sample32 maximum,
                   const base::BucketRanges* ranges)
      : Histogram(metric_to_override, ranges),
        minimum_(minimum),
        maximum_(maximum),
        bucket_count_(ranges->bucket_count()) {}

  GroupedHistogram(const GroupedHistogram&) = delete;
  GroupedHistogram& operator=(const GroupedHistogram&) = delete;

  ~GroupedHistogram() override {
  }

  // base::Histogram implementation:
  void Add(Sample32 value) override {
    Histogram::Add(value);

    // Note: This is very inefficient. Fetching the app name (which has a lock)
    // plus doing a search by name with FactoryGet (which also has a lock) makes
    // incrementing a metric relatively slow.
    std::string name = base::StrCat({histogram_name(), ".", GetAppName()});
    HistogramBase* grouped_histogram =
        base::Histogram::FactoryGet(name,
                                    minimum_,
                                    maximum_,
                                    bucket_count_,
                                    flags());
    DCHECK(grouped_histogram);
    grouped_histogram->Add(value);
  }

 private:
  // Saved construction arguments for reconstructing the Histogram later (with
  // a suffixed app name).
  Sample32 minimum_;
  Sample32 maximum_;
  uint32_t bucket_count_;
};

// Registers a GroupedHistogram with StatisticsRecorder.  Must be called
// before any Histogram of the same name has been used.
// It acts similarly to Histogram::FactoryGet but checks that
// the histogram is being newly created and does not already exist.
void PreregisterHistogram(base::DurableStringView durable_name,
                          GroupedHistogram::Sample32 minimum,
                          GroupedHistogram::Sample32 maximum,
                          size_t bucket_count,
                          int32_t flags) {
  DCHECK(base::Histogram::InspectConstructionArguments(
      *durable_name, &minimum, &maximum, &bucket_count));
  DCHECK(!base::StatisticsRecorder::FindHistogram(*durable_name))
      << "Failed to preregister " << *durable_name
      << ", Histogram already exists.";

  // To avoid racy destruction at shutdown, the following will be leaked.
  base::BucketRanges* ranges = new base::BucketRanges(bucket_count + 1);
  base::Histogram::InitializeBucketRanges(minimum, maximum, ranges);
  const base::BucketRanges* registered_ranges =
      base::StatisticsRecorder::RegisterOrDeleteDuplicateRanges(ranges);

  GroupedHistogram* tentative_histogram =
      new GroupedHistogram(durable_name, minimum, maximum, registered_ranges);

  tentative_histogram->SetFlags(flags);
  base::HistogramBase* histogram =
      base::StatisticsRecorder::RegisterOrDeleteDuplicate(tentative_histogram);

  DCHECK_EQ(histogram, tentative_histogram);
  DCHECK_EQ(base::HISTOGRAM, histogram->GetHistogramType());
  DCHECK(histogram->HasConstructionArguments(minimum, maximum, bucket_count));
}

} // namespace

void PreregisterAllGroupedHistograms() {
  for (const auto& histogram : kHistogramsToGroup) {
    PreregisterHistogram(histogram.durable_name, histogram.minimum,
                         histogram.maximum, histogram.bucket_count,
                         base::HistogramBase::kUmaTargetedHistogramFlag);
  }
}

void TagAppStartForGroupedHistograms(const std::string& app_name) {
  base::AutoLock lock(GetCurrentApp().lock);
  GetCurrentApp().app_name = app_name;
}

} // namespace metrics
} // namespace chromecast
