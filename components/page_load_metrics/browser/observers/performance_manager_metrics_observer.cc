// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/page_load_metrics/browser/observers/performance_manager_metrics_observer.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/notreached.h"
#include "base/sequence_checker.h"
#include "base/strings/strcat.h"
#include "base/task/bind_post_task.h"
#include "base/time/time.h"
#include "components/page_load_metrics/browser/observers/core/largest_contentful_paint_handler.h"
#include "components/page_load_metrics/browser/page_load_metrics_observer.h"
#include "components/page_load_metrics/browser/page_load_metrics_observer_delegate.h"
#include "components/page_load_metrics/common/page_load_metrics.mojom.h"
#include "components/performance_manager/public/graph/graph.h"
#include "components/performance_manager/public/graph/graph_registered.h"
#include "components/performance_manager/public/graph/page_node.h"
#include "components/performance_manager/public/performance_manager.h"
#include "content/public/browser/web_contents.h"

namespace {

using ObservePolicy = page_load_metrics::PageLoadMetricsObserver::ObservePolicy;
using Visibility = PerformanceManagerMetricsObserver::Visibility;
using performance_manager::Graph;
using performance_manager::GraphOwnedAndRegistered;
using performance_manager::PageNode;
using performance_manager::PageNodeObserver;
using performance_manager::PerformanceManager;

constexpr char kLCPToLoadedIdleHistogram[] =
    "PageLoad.Clients.PerformanceManager.LCPToLoadedIdle";
constexpr char kLCPWithoutLoadedIdleHistogram[] =
    "PageLoad.Clients.PerformanceManager.LCPWithoutLoadedIdle";
constexpr char kLoadedIdleWithoutLCPHistogram[] =
    "PageLoad.Clients.PerformanceManager.LoadedIdleWithoutLCP";
constexpr char kNavigationToLoadedIdleHistogram[] =
    "PageLoad.Clients.PerformanceManager.NavigationToLoadedIdle";
constexpr char kNavigationWithoutLoadedIdleHistogram[] =
    "PageLoad.Clients.PerformanceManager.NavigationWithoutLoadedIdle";

class LoadedIdleObserver final
    : public PageNodeObserver,
      public GraphOwnedAndRegistered<LoadedIdleObserver> {
 public:
  // Gets a singleton LoadedIdleObserver registered with `graph`. It will be
  // deleted when `graph` is torn down, so in unit tests a new
  // LoadedIdleObserver is created for each test.
  static LoadedIdleObserver& GetOrCreate(Graph* graph);

  ~LoadedIdleObserver() final = default;

  LoadedIdleObserver(const LoadedIdleObserver&) = delete;
  LoadedIdleObserver operator=(const LoadedIdleObserver&) = delete;

  // Starts watching `page_node` and invokes `on_loaded_idle_callback` with the
  // time that it reaches LoadedIdle.
  void AddWatchedPageNode(
      base::WeakPtr<PageNode> page_node,
      base::OnceCallback<void(base::TimeTicks)> on_loaded_idle_callback);

  // PageNodeObserver:
  void OnLoadingStateChanged(const PageNode* page_node,
                             PageNode::LoadingState) final;
  void OnBeforePageNodeRemoved(const PageNode* page_node) final;

  // GraphOwned:
  void OnPassedToGraph(Graph* graph) final;
  void OnTakenFromGraph(Graph* graph) final;

 private:
  LoadedIdleObserver() = default;

  SEQUENCE_CHECKER(sequence_checker_);

  // Maps each PageNode to a callback to invoke with the time that it reaches
  // LoadedIdle.
  std::map<const PageNode*, base::OnceCallback<void(base::TimeTicks)>>
      watched_pages_ GUARDED_BY_CONTEXT(sequence_checker_);
};

// static
LoadedIdleObserver& LoadedIdleObserver::GetOrCreate(Graph* graph) {
  auto* observer = LoadedIdleObserver::GetFromGraph(graph);
  if (!observer) {
    observer = graph->PassToGraph(base::WrapUnique(new LoadedIdleObserver()));
  }
  return *observer;
}

void LoadedIdleObserver::AddWatchedPageNode(
    base::WeakPtr<PageNode> page_node,
    base::OnceCallback<void(base::TimeTicks)> on_loaded_idle_callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!page_node) {
    return;
  }
  // `page_node` may already be in `watched_pages_` if there are several
  // navigations in the page. If so, replace the previous callback without
  // calling it as the previous load was aborted without reaching LoadedIdle.
  watched_pages_[page_node.get()] = std::move(on_loaded_idle_callback);
}

void LoadedIdleObserver::OnLoadingStateChanged(const PageNode* page_node,
                                               PageNode::LoadingState) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (page_node->GetLoadingState() != PageNode::LoadingState::kLoadedIdle) {
    return;
  }
  const auto now = base::TimeTicks::Now();
  auto it = watched_pages_.find(page_node);
  if (it != watched_pages_.end()) {
    std::move(it->second).Run(now);
    watched_pages_.erase(it);
  }
}

void LoadedIdleObserver::OnBeforePageNodeRemoved(const PageNode* page_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  watched_pages_.erase(page_node);
}

void LoadedIdleObserver::OnPassedToGraph(Graph* graph) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  graph->AddPageNodeObserver(this);
}

void LoadedIdleObserver::OnTakenFromGraph(Graph* graph) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  graph->RemovePageNodeObserver(this);
}

void WatchLoadedIdleObserver(
    base::WeakPtr<PageNode> page_node,
    base::OnceCallback<void(base::TimeTicks)> on_loaded_idle_callback,
    Graph* graph) {
  LoadedIdleObserver::GetOrCreate(graph).AddWatchedPageNode(
      page_node, std::move(on_loaded_idle_callback));
}

const char* GetVisibilitySuffix(Visibility visibility) {
  switch (visibility) {
    case Visibility::kBackground:
      return ".Background";
    case Visibility::kForeground:
      return ".Foreground";
    case Visibility::kMixed:
      return ".MixedBGFG";
    case Visibility::kUnknown:
      NOTREACHED();
  }
  NOTREACHED();
}

}  // namespace

PerformanceManagerMetricsObserver::PerformanceManagerMetricsObserver() =
    default;

PerformanceManagerMetricsObserver::~PerformanceManagerMetricsObserver() =
    default;

void PerformanceManagerMetricsObserver::OnPageNodeLoadedIdle(
    base::TimeTicks loaded_idle_time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(loaded_idle_time_.is_null());
  loaded_idle_time_ = loaded_idle_time;
  LogMetricsIfAvailable();
}

ObservePolicy PerformanceManagerMetricsObserver::OnStart(
    content::NavigationHandle*,
    const GURL&,
    bool started_in_foreground) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK_EQ(visibility_, Visibility::kUnknown);
  visibility_ =
      started_in_foreground ? Visibility::kForeground : Visibility::kBackground;
  WatchForLoadedIdle(PerformanceManager::GetPrimaryPageNodeForWebContents(
      GetDelegate().GetWebContents()));
  return CONTINUE_OBSERVING;
}

ObservePolicy PerformanceManagerMetricsObserver::OnFencedFramesStart(
    content::NavigationHandle*,
    const GURL&) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK_EQ(visibility_, Visibility::kUnknown);
  // TODO(https://crbug.com/344923216): Handle fenced frames.
  return STOP_OBSERVING;
}

ObservePolicy PerformanceManagerMetricsObserver::OnPrerenderStart(
    content::NavigationHandle*,
    const GURL&) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK_EQ(visibility_, Visibility::kUnknown);
  // TODO(https://crbug.com/344923216): Handle prerendering.
  return STOP_OBSERVING;
}

ObservePolicy PerformanceManagerMetricsObserver::OnHidden(
    const page_load_metrics::mojom::PageLoadTiming&) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(visibility_ == Visibility::kForeground ||
        visibility_ == Visibility::kMixed);
  visibility_ = Visibility::kMixed;
  return CONTINUE_OBSERVING;
}

ObservePolicy PerformanceManagerMetricsObserver::OnShown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(visibility_ == Visibility::kBackground ||
        visibility_ == Visibility::kMixed);
  visibility_ = Visibility::kMixed;
  return CONTINUE_OBSERVING;
}

ObservePolicy
PerformanceManagerMetricsObserver::FlushMetricsOnAppEnterBackground(
    const page_load_metrics::mojom::PageLoadTiming&) {
  return LogMetricsIfAvailable();
}

void PerformanceManagerMetricsObserver::OnComplete(
    const page_load_metrics::mojom::PageLoadTiming&) {
  LogFinalMetrics();
}

void PerformanceManagerMetricsObserver::OnFailedProvisionalLoad(
    const page_load_metrics::FailedProvisionalLoadInfo&) {
  LogFinalMetrics();
}

std::optional<base::TimeDelta>
PerformanceManagerMetricsObserver::DeltaFromNavigationStartTime(
    base::TimeTicks time) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const base::TimeTicks navigation_start_time =
      GetDelegate().GetNavigationStart();
  CHECK(!navigation_start_time.is_null());
  // `navigation_start_time` is reported from renderers so can't be guaranteed
  // monotonically increasing compared to TimeTicks::Now() taken in this
  // process. Return nullopt if it's not valid.
  if (time < navigation_start_time) {
    return std::nullopt;
  }
  return time - navigation_start_time;
}

ObservePolicy PerformanceManagerMetricsObserver::LogMetricsIfAvailable() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (loaded_idle_time_.is_null()) {
    // Keep waiting for loaded idle time to come in.
    return CONTINUE_OBSERVING;
  }

  const std::optional<base::TimeDelta> loaded_idle_delta =
      DeltaFromNavigationStartTime(loaded_idle_time_);
  if (!loaded_idle_delta.has_value()) {
    // Bail out if the delta from navigation start isn't valid.
    return STOP_OBSERVING;
  }

  // Log metrics about LoadedIdle time.
  if (!logged_load_metrics_) {
    // Broken down by visibility.
    base::UmaHistogramMediumTimes(
        base::StrCat({kNavigationToLoadedIdleHistogram,
                      GetVisibilitySuffix(visibility_)}),
        loaded_idle_delta.value());
    // All page loads.
    base::UmaHistogramMediumTimes(kNavigationToLoadedIdleHistogram,
                                  loaded_idle_delta.value());
    logged_load_metrics_ = true;
  }

  // Log time between LCP and LoadedIdle.
  if (!logged_lcp_metrics_) {
    const page_load_metrics::ContentfulPaintTimingInfo& lcp_info =
        GetDelegate()
            .GetLargestContentfulPaintHandler()
            .MergeMainFrameAndSubframes();
    if (!lcp_info.ContainsValidTime()) {
      // Keep waiting for LCP to come in.
      return CONTINUE_OBSERVING;
    }

    // If LoadedIdle came before LCP (unexpected) the negative TimeDelta will
    // be logged in the 0 bucket.
    CHECK(!lcp_info.Time()->is_negative());
    CHECK(!loaded_idle_delta->is_negative());
    base::TimeDelta loaded_idle_delta_from_lcp =
        loaded_idle_delta.value() - lcp_info.Time().value();

    // Broken down by visibility.
    base::UmaHistogramMediumTimes(
        base::StrCat(
            {kLCPToLoadedIdleHistogram, GetVisibilitySuffix(visibility_)}),
        loaded_idle_delta_from_lcp);
    // All page loads.
    base::UmaHistogramMediumTimes(kLCPToLoadedIdleHistogram,
                                  loaded_idle_delta_from_lcp);

    logged_lcp_metrics_ = true;
  }

  if (logged_load_metrics_ && logged_lcp_metrics_) {
    // Nothing more to log.
    return STOP_OBSERVING;
  }
  // Keep waiting.
  return CONTINUE_OBSERVING;
}

void PerformanceManagerMetricsObserver::LogFinalMetrics() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  LogMetricsIfAvailable();

  if (!logged_load_metrics_) {
    // Log that navigation stopped without receiving LoadIdleTime.
    if (!loaded_idle_time_.is_null()) {
      // After calling LogMetricsIfAvailable() above, only expects non null
      // loaded_idle_time_ when timestamps from different processes are out of
      // order.
      CHECK(!DeltaFromNavigationStartTime(loaded_idle_time_).has_value());
      // Skip logging data, timestamps are not reliable.
      return;
    }

    const std::optional<base::TimeDelta> navigation_delta =
        DeltaFromNavigationStartTime(base::TimeTicks::Now());
    if (navigation_delta.has_value()) {
      // Broken down by visibility.
      base::UmaHistogramMediumTimes(
          base::StrCat({kNavigationWithoutLoadedIdleHistogram,
                        GetVisibilitySuffix(visibility_)}),
          navigation_delta.value());
      // All page loads.
      base::UmaHistogramMediumTimes(kNavigationWithoutLoadedIdleHistogram,
                                    navigation_delta.value());
      logged_load_metrics_ = true;
    }
  }

  if (logged_lcp_metrics_) {
    // Nothing more to log.
    return;
  }

  const page_load_metrics::ContentfulPaintTimingInfo& lcp_info =
      GetDelegate()
          .GetLargestContentfulPaintHandler()
          .MergeMainFrameAndSubframes();
  if (lcp_info.ContainsValidTime()) {
    // Page never reached LoadedIdle.
    CHECK(loaded_idle_time_.is_null());
    CHECK(!lcp_info.Time()->is_negative());

    // Broken down by visibility.
    base::UmaHistogramMediumTimes(
        base::StrCat(
            {kLCPWithoutLoadedIdleHistogram, GetVisibilitySuffix(visibility_)}),
        lcp_info.Time().value());
    // All page loads.
    base::UmaHistogramMediumTimes(kLCPWithoutLoadedIdleHistogram,
                                  lcp_info.Time().value());

    logged_lcp_metrics_ = true;
  } else if (!loaded_idle_time_.is_null()) {
    // Page reached LoadedIdle without recording LCP.
    const std::optional<base::TimeDelta> loaded_idle_delta =
        DeltaFromNavigationStartTime(loaded_idle_time_);
    if (loaded_idle_delta.has_value()) {
      // Broken down by visibility.
      base::UmaHistogramMediumTimes(
          base::StrCat({kLoadedIdleWithoutLCPHistogram,
                        GetVisibilitySuffix(visibility_)}),
          loaded_idle_delta.value());
      // All page loads.
      base::UmaHistogramMediumTimes(kLoadedIdleWithoutLCPHistogram,
                                    loaded_idle_delta.value());

      logged_lcp_metrics_ = true;
    }
  }
}

void PerformanceManagerMetricsObserver::WatchForLoadedIdle(
    base::WeakPtr<PageNode> page_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!PerformanceManager::IsAvailable()) {
    return;
  }

  auto on_loaded_idle_callback =
      base::BindOnce(&PerformanceManagerMetricsObserver::OnPageNodeLoadedIdle,
                     weak_factory_.GetWeakPtr());
  // When `page_node` enters the LoadedIdle state in PerformanceManager, call
  // `on_loaded_idle_callback` on this sequence.
  WatchLoadedIdleObserver(std::move(page_node),
                          std::move(on_loaded_idle_callback),
                          PerformanceManager::GetGraph());
}
