// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/metrics/power/process_metrics_recorder_util.h"

#include <cmath>
#include <optional>

#include "base/metrics/histogram_functions.h"
#include "base/strings/strcat.h"
#include "base/time/time.h"
#include "build/build_config.h"

namespace {

// CPU usage metrics are provided as a double in the [0.0, number of cores *
// 100.0] range. The CPU usage is usually below 1%, so the histograms are
// reported with a 1/10000 granularity to make analyzing the data easier
// (otherwise almost all samples end up in the same [0, 1[ bucket).
constexpr int kCPUUsageFactor = 100;
// Scale up to the equivalent of 16 CPU cores fully loaded. As of 2025-05-06,
// this was capped at 2 cores but the 99th percentile of the data fell in the
// overflow bucket. To analyze rare but heavy workloads, we'd like the 99.999th
// percentile of samples to fall outside the overflow.
constexpr int kCPUUsageHistogramMin = 1;
constexpr int kCPUUsageHistogramMax = 1600 * kCPUUsageFactor;
constexpr int kCPUUsageHistogramBucketCount = 100;

#if BUILDFLAG(IS_WIN)
bool HasConstantRateTSC() {
#if defined(ARCH_CPU_ARM64)
  // Constant rate TSC is never support on Arm CPUs.
  return false;
#else
  // Probe the CPU to detect if constant-rate TSC is supported.
  return base::time_internal::HasConstantRateTSC();
#endif
}
#endif  // BUILDFLAG(IS_WIN)

void RecordAverageCPUUsage(const char* histogram_suffix,
                           const std::optional<double>& cpu_usage) {
#if BUILDFLAG(IS_WIN)
  // Skip recording the average CPU usage if the CPU doesn't support constant
  // rate TSC, since Windows does not offer a way to get a precise measurement
  // without it.
  if (!HasConstantRateTSC())
    return;
#endif

  // The metric definition in
  // tools/metrics/histograms/metadata/power/histograms.xml says, "If no process
  // of type {ProcessName} existed during the interval, a sample of zero is
  // still emitted."
  base::UmaHistogramCustomCounts(
      base::StrCat({"PerformanceMonitor.AverageCPU9.", histogram_suffix}),
      cpu_usage.value_or(0.0) * kCPUUsageFactor, kCPUUsageHistogramMin,
      kCPUUsageHistogramMax, kCPUUsageHistogramBucketCount);
}

}  // namespace

void RecordProcessHistograms(const char* histogram_suffix,
                             const ProcessMonitor::Metrics& metrics) {
  RecordAverageCPUUsage(histogram_suffix, metrics.cpu_usage);
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || \
    BUILDFLAG(IS_AIX)
  base::UmaHistogramCounts10000(
      base::StrCat({"PerformanceMonitor.IdleWakeups2.", histogram_suffix}),
      metrics.idle_wakeups);
#endif
#if BUILDFLAG(IS_MAC)
  base::UmaHistogramCounts1000(
      base::StrCat(
          {"PerformanceMonitor.PackageExitIdleWakeups2.", histogram_suffix}),
      metrics.package_idle_wakeups);
#endif
}
