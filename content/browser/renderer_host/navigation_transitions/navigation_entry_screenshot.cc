// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/navigation_transitions/navigation_entry_screenshot.h"

#include "base/feature_list.h"
#include "base/functional/callback.h"
#include "base/metrics/histogram_macros.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "components/performance_manager/scenario_api/performance_scenario_observer.h"
#include "components/performance_manager/scenario_api/performance_scenarios.h"
#include "content/browser/renderer_host/navigation_transitions/navigation_entry_screenshot_cache.h"
#include "content/browser/renderer_host/navigation_transitions/navigation_transition_config.h"

#if BUILDFLAG(IS_ANDROID)
#include "ui/android/resources/etc1_utils.h"
#endif

namespace content {

#if BUILDFLAG(IS_ANDROID)
namespace {

BASE_FEATURE(kNavigationEntryScreenshotCompression,
             "NavigationEntryScreenshotCompression",
             base::FEATURE_ENABLED_BY_DEFAULT);

static bool g_disable_compression_for_testing = false;

using CompressionDoneCallback = base::OnceCallback<void(sk_sp<SkPixelRef>)>;
void CompressNavigationScreenshotOnWorkerThread(
    SkBitmap bitmap,
    bool supports_etc_non_power_of_two,
    CompressionDoneCallback done_callback) {
  SCOPED_UMA_HISTOGRAM_TIMER("Navigation.GestureTransition.CompressionTime");
  TRACE_EVENT0("navigation", "CompressNavigationScreenshotOnWorkerThread");

  sk_sp<SkPixelRef> compressed_bitmap = nullptr;
  if (base::FeatureList::IsEnabled(ui::kCompressBitmapAtBackgroundPriority)) {
    compressed_bitmap = ui::Etc1::CompressBitmapAtBackgroundPriority(
        bitmap, supports_etc_non_power_of_two);
  } else {
    compressed_bitmap =
        ui::Etc1::CompressBitmap(bitmap, supports_etc_non_power_of_two);
  }

  if (compressed_bitmap) {
    std::move(done_callback).Run(std::move(compressed_bitmap));
  }
}

}  // namespace
#endif

// static
const void* const NavigationEntryScreenshot::kUserDataKey =
    &NavigationEntryScreenshot::kUserDataKey;

// static
void NavigationEntryScreenshot::SetDisableCompressionForTesting(bool disable) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

#if BUILDFLAG(IS_ANDROID)
  g_disable_compression_for_testing = disable;
#endif
}

NavigationEntryScreenshot::NavigationEntryScreenshot(
    const SkBitmap& bitmap,
    NavigationTransitionData::UniqueId unique_id,
    bool supports_etc_non_power_of_two)
    : performance_scenarios::MatchingScenarioObserver(
          performance_scenarios::kDefaultIdleScenarios),
      bitmap_(cc::UIResourceBitmap(bitmap)),
      unique_id_(unique_id),
      dimensions_without_compression_(bitmap_->GetSize()) {
  CHECK(NavigationTransitionConfig::AreBackForwardTransitionsEnabled());
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  compression_task_ = CompressionTask(bitmap, supports_etc_non_power_of_two);
  if (!compression_task_) {
    return;
  }
  if (NavigationTransitionConfig::ShouldCompressScreenshotWhenQuiet()) {
    auto observer_list =
        performance_scenarios::PerformanceScenarioObserverList::GetForScope(
            performance_scenarios::ScenarioScope::kGlobal);
    if (observer_list) {
      observer_list->AddMatchingObserver(this);
      return;
    }
  }
  StartCompression();
}

NavigationEntryScreenshot::~NavigationEntryScreenshot() {
  if (cache_) {
    cache_->OnNavigationEntryGone(unique_id_);
  }
  if (compression_task_) {
    auto observer_list =
        performance_scenarios::PerformanceScenarioObserverList::GetForScope(
            performance_scenarios::ScenarioScope::kGlobal);
    if (observer_list) {
      observer_list->RemoveMatchingObserver(this);
    }
  }
}

cc::UIResourceBitmap NavigationEntryScreenshot::GetBitmap(cc::UIResourceId uid,
                                                          bool resource_lost) {
  // TODO(liuwilliam): Currently none of the impls of `GetBitmap` uses `uid` or
  // `resource_lost`. Consider deleting them from the interface.
  return GetBitmap();
}

size_t NavigationEntryScreenshot::SetCache(
    NavigationEntryScreenshotCache* cache) {
  CHECK(!cache_ || !cache);
  cache_ = cache;

  if (cache_ && compressed_bitmap_) {
    bitmap_.reset();
  }

  return GetBitmap().SizeInBytes();
}

void NavigationEntryScreenshot::OnScenarioMatchChanged(
    performance_scenarios::ScenarioScope scope,
    bool matches_pattern) {
  if (matches_pattern && compression_task_) {
    StartCompression();
    performance_scenarios::PerformanceScenarioObserverList::GetForScope(
        performance_scenarios::ScenarioScope::kGlobal)
        ->RemoveMatchingObserver(this);
  }
}

SkBitmap NavigationEntryScreenshot::GetBitmapForTesting() const {
  return GetBitmap().GetBitmapForTesting();  // IN-TEST
}

size_t NavigationEntryScreenshot::CompressedSizeForTesting() const {
  return !bitmap_ ? compressed_bitmap_->SizeInBytes() : 0u;
}

base::OnceClosure NavigationEntryScreenshot::CompressionTask(
    const SkBitmap& bitmap,
    bool supports_etc_non_power_of_two) {
#if BUILDFLAG(IS_ANDROID)
  if (!base::FeatureList::IsEnabled(kNavigationEntryScreenshotCompression) ||
      g_disable_compression_for_testing) {
    return base::OnceClosure();
  }

  CompressionDoneCallback done_callback = base::BindPostTask(
      GetUIThreadTaskRunner(),
      base::BindOnce(&NavigationEntryScreenshot::OnCompressionFinished,
                     weak_factory_.GetWeakPtr()));

  return base::BindOnce(&CompressNavigationScreenshotOnWorkerThread, bitmap,
                        supports_etc_non_power_of_two,
                        std::move(done_callback));
#else
  return base::OnceClosure();
#endif
}

void NavigationEntryScreenshot::StartCompression() {
  base::ThreadPool::PostTask(FROM_HERE,
                             {base::TaskPriority::BEST_EFFORT,
                              base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
                             std::move(compression_task_));
}

void NavigationEntryScreenshot::OnCompressionFinished(
    sk_sp<SkPixelRef> compressed_bitmap) {
  CHECK(!compressed_bitmap_);
  CHECK(bitmap_);
  CHECK(compressed_bitmap);

  const auto size =
      gfx::Size(compressed_bitmap->width(), compressed_bitmap->height());
  compressed_bitmap_ = cc::UIResourceBitmap(std::move(compressed_bitmap), size);
  TRACE_EVENT2("navigation", "NavigationEntryScreenshot::OnCompressionFinished",
               "old_size", bitmap_->SizeInBytes(), "new_size",
               compressed_bitmap_->SizeInBytes());

  // We defer discarding the uncompressed bitmap if there is no cache since it
  // may still be in use in the UI.
  if (cache_) {
    bitmap_.reset();
    cache_->OnScreenshotCompressed(unique_id_, GetBitmap().SizeInBytes());
  }
}

const cc::UIResourceBitmap& NavigationEntryScreenshot::GetBitmap() const {
  return bitmap_ ? *bitmap_ : *compressed_bitmap_;
}

}  // namespace content
