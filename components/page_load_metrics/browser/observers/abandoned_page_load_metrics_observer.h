// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PAGE_LOAD_METRICS_BROWSER_OBSERVERS_ABANDONED_PAGE_LOAD_METRICS_OBSERVER_H_
#define COMPONENTS_PAGE_LOAD_METRICS_BROWSER_OBSERVERS_ABANDONED_PAGE_LOAD_METRICS_OBSERVER_H_

#include <set>

#include "components/page_load_metrics/browser/page_load_metrics_observer.h"
#include "content/public/browser/navigation_handle_timing.h"
#include "net/base/net_errors.h"
#include "services/metrics/public/cpp/ukm_builders.h"

namespace internal {
// Exposed for tests.
extern const char kAbandonedPageLoadMetricsHistogramPrefix[];
extern const char kSuffixWasBackgrounded[];
extern const char kSuffixTabWasHiddenAtStartStaysHidden[];
extern const char kSuffixTabWasHiddenAtStartLaterShown[];
extern const char kSuffixTabWasHiddenStaysHidden[];
extern const char kSuffixTabWasHiddenLaterShown[];
extern const char kRendererProcessCreatedBeforeNavHistogramName[];
extern const char kRendererProcessInitHistogramName[];
extern const char kNavigationTypeBrowserNav[];

}  // namespace internal

// Observes and records UMA for navigations which might or might get
// "abandoned" at some point during the navigation / loading, for navigations
// that target any URL. This observer will log the navigation milestones even if
// the navigation didn't end up reaching all the milestones. This allows us to
// look at the amount of navigations that reached each milestone, to see where
// the navigation gets abandoned. In addition to that, this observer will also
// log the abandonment reason and the last navigation milestone the navigation
// reached before getting abandoned.
class AbandonedPageLoadMetricsObserver
    : public page_load_metrics::PageLoadMetricsObserver {
 public:
  // The different navigation milestones that the tracked page load can reach.
  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  //
  // LINT.IfChange(NavigationMilestone)
  enum class NavigationMilestone {
    kNavigationStart = 0,
    kLoaderStart = 1,
    // The `kFirstRedirected*` milestones are optional. They will not be
    // recorded if the navigation did not involve any redirects.
    kFirstRedirectedRequestStart = 2,
    kFirstRedirectResponseStart = 3,
    kFirstRedirectResponseLoaderCallback = 4,
    kNonRedirectedRequestStart = 5,
    kNonRedirectResponseStart = 6,
    kNonRedirectResponseLoaderCallback = 7,
    kCommitSent = 8,
    kCommitReceived = 9,
    kDidCommit = 10,
    kParseStart = 11,
    kFirstContentfulPaint = 12,
    kDOMContentLoaded = 13,
    kLoadEventStarted = 14,
    kLargestContentfulPaint = 15,

    // TODO(crbug.com/390216631): Move `kAFT*`, `kHeaderChunk*`, `kBodyChunk*`
    // milestones to GWSAbandonedPLMO as they may only appear in certain
    // navigations.
    kAFTStart = 16,
    kAFTEnd = 17,
    kHeaderChunkStart = 18,
    kHeaderChunkEnd = 19,
    kBodyChunkStart = 20,
    kBodyChunkEnd = 21,

    // The `kSecondRedirected*` milestones are optional. They will not be
    // recorded if the navigation had at most one redirect.
    kSecondRedirectedRequestStart = 22,
    kSecondRedirectResponseStart = 23,

    // `kCommitReplySent` is the milestone placed between `kCommitReceived` and
    // `kDidCommit`.
    kCommitReplySent = 24,

    kMaxValue = kCommitReplySent,
    // `kFirstEssentialLoadingEvent` and `kLastEssentialLoadingEvent` aliases to
    // the first / last loading milestones which always exists unless abandoned.
    // Note that events such as `kAFT*`, `kHeaderChunk*` and `kBodyChunk*` only
    // appears in certain navigations, hence these events may not be mandatory.
    kFirstEssentialLoadingEvent = kParseStart,
    kLastEssentialLoadingEvent = kLargestContentfulPaint,

    // `kFirstGwsEssentialLoadingEvent` and `kLastGwsEssentialLoadingEvent`
    // aliases to the first / last loading milestones which always exists
    // unless abandoned for navigations to Gws.
    kFirstGwsEssentialLoadingEvent = kFirstEssentialLoadingEvent,
    kLastGwsEssentialLoadingEvent = kBodyChunkEnd,
  };
  // LINT.ThenChange(//tools/metrics/histograms/metadata/page/enums.xml:NavigationMilestoneEnum2)

  // The different abandonment reasons that the tracked page load can encounter.
  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  //
  // LINT.IfChange(AbandonReason)
  enum class AbandonReason {
    kFrameRemoved = 0,
    kExplicitCancellation = 1,
    kInternalCancellation = 2,
    kRenderProcessGone = 3,
    kNeverStarted = 4,
    kFailedSecurityCheck = 5,
    kOther = 6,
    kHidden = 7,
    kErrorPage = 8,
    kAppBackgrounded = 9,
    kNewReloadNavigation = 10,
    kNewHistoryNavigation = 11,
    kNewOtherNavigationBrowserInitiated = 12,
    kNewOtherNavigationRendererInitiated = 13,
    kNewDuplicateNavigation = 14,
    kMaxValue = kNewDuplicateNavigation,
  };
  // When updating milestones, the corresponding milestones list in test
  // `GWSAbandonedPageLoadMetricsObserverBrowserTest::all_milestones()` should
  // be updated as well. As LINT can't annotate multiple targets, we need to
  // take care of it manually.
  //
  // LINT.ThenChange(//tools/metrics/histograms/metadata/page/enums.xml:NavigationAbandonReasonEnum)

  static std::string AbandonReasonToString(AbandonReason abandon_reason);
  static bool IsEventAfter(base::TimeTicks event_time,
                           base::TimeTicks time_to_compare) {
    return !time_to_compare.is_null() && event_time > time_to_compare;
  }
  static std::string NavigationMilestoneToString(
      NavigationMilestone navigation_milestone);
  static std::string GetMilestoneHistogramNameWithoutPrefixSuffix(
      NavigationMilestone milestone);
  static std::string GetMilestoneToAbandonHistogramNameWithoutPrefixSuffix(
      NavigationMilestone milestone,
      std::optional<AbandonReason> abandon_reason);
  static std::string
  GetAbandonReasonAtMilestoneHistogramNameWithoutPrefixSuffix(
      NavigationMilestone milestone);
  static std::string
  GetLastMilestoneBeforeAbandonHistogramNameWithoutPrefixSuffix(
      std::optional<AbandonReason> abandon_reason);
  static std::string GetTimeToAbandonFromNavigationStartWithoutPrefixSuffix(
      NavigationMilestone milestone);
  static std::string GetNavigationTypeToAbandonWithoutPrefixSuffix(
      const std::string_view& tracked_navigation_type,
      std::optional<AbandonReason> abandon_reason);

  AbandonedPageLoadMetricsObserver();
  ~AbandonedPageLoadMetricsObserver() override;

  AbandonedPageLoadMetricsObserver(const AbandonedPageLoadMetricsObserver&) =
      delete;
  AbandonedPageLoadMetricsObserver& operator=(
      const AbandonedPageLoadMetricsObserver&) = delete;

  // page_load_metrics::PageLoadMetricsObserver implementation:
  const char* GetObserverName() const override;
  ObservePolicy OnStart(content::NavigationHandle* navigation_handle,
                        const GURL& currently_committed_url,
                        bool started_in_foreground) override;
  ObservePolicy OnRedirect(
      content::NavigationHandle* navigation_handle) override;
  ObservePolicy OnNavigationHandleTimingUpdated(
      content::NavigationHandle* navigation_handle) override;
  ObservePolicy OnCommit(content::NavigationHandle* navigation_handle) override;

  // Post-commit loading events:
  void OnParseStart(
      const page_load_metrics::mojom::PageLoadTiming& timing) override;
  void OnFirstContentfulPaintInPage(
      const page_load_metrics::mojom::PageLoadTiming& timing) override;
  void OnDomContentLoadedEventStart(
      const page_load_metrics::mojom::PageLoadTiming& timing) override;
  void OnLoadEventStart(
      const page_load_metrics::mojom::PageLoadTiming& timing) override;
  void OnComplete(
      const page_load_metrics::mojom::PageLoadTiming& timing) override;
  void OnCustomUserTimingMarkObserved(
      const std::vector<page_load_metrics::mojom::CustomUserTimingMarkPtr>&
          timings) override;

  // Signals that the navigation is abandoned: backgrounded, hidden, or failed.
  ObservePolicy FlushMetricsOnAppEnterBackground(
      const page_load_metrics::mojom::PageLoadTiming& timing) override;
  ObservePolicy OnHidden(
      const page_load_metrics::mojom::PageLoadTiming& timing) override;
  ObservePolicy OnShown() override;
  void OnFailedProvisionalLoad(
      const page_load_metrics::FailedProvisionalLoadInfo&
          failed_provisional_load_info) override;
  void OnDidInternalNavigationAbort(
      content::NavigationHandle* navigation_handle) override;
  void ReadyToCommitNextNavigation(
      content::NavigationHandle* navigation_handle) override;

  // Prerender, fenced-frame, bfcache cases are excluded.
  // TODO(https://crbug.com/347706997): Consider logging for these cases, but
  // mark them specifically to avoid skewing the timings.
  ObservePolicy OnPrerenderStart(content::NavigationHandle* navigation_handle,
                                 const GURL& currently_committed_url) override;
  ObservePolicy OnFencedFramesStart(
      content::NavigationHandle* navigation_handle,
      const GURL& currently_committed_url) override;
  void OnPrimaryPageRenderProcessGone() override;

 protected:
  virtual std::vector<std::string> GetAdditionalSuffixes() const;
  virtual ObservePolicy OnNavigationEvent(
      content::NavigationHandle* navigation_handle);
  bool IsResponseFromCache() const { return was_cached_; }
  int64_t navigation_id() const { return navigation_id_; }
  const base::TimeTicks navigation_start_time() const {
    return navigation_start_time_;
  }
  const std::map<NavigationMilestone, base::TimeDelta>& loading_milestones()
      const {
    return loading_milestones_;
  }

  // Adds the abandonment related metrics to UKM Builder `T`.
  template <typename T>
  void LogUKMHistogramsForAbandonMetrics(
      T& builder,
      AbandonReason abandon_reason,
      AbandonedPageLoadMetricsObserver::NavigationMilestone milestone,
      base::TimeTicks event_time,
      base::TimeTicks relative_start_time);

  // Adds the milestone related metrics to UKM Builder `T`.
  template <typename T>
  std::set<AbandonedPageLoadMetricsObserver::NavigationMilestone>
  LogUKMHistogramsForMilestoneMetrics(T& builder, base::TimeTicks event_time);

  // Whether we've reached and logged all loading milestones, from
  // `kFirstEssentialLoadingEvent` to `kLastEssentialLoadingEvent`.
  virtual bool DidLogAllLoadingMilestones() const;

 private:
  using LoadingMilestone = std::pair<NavigationMilestone, base::TimeDelta>;
  // Returns the suffix to be added to the histograms logged. This is not static
  // since the return value depends on events that happened while the tracked
  // page load is ongoing.
  std::string GetHistogramSuffix(NavigationMilestone navigation_milestone,
                                 base::TimeTicks event_time) const;

  void LogNavigationMilestoneMetrics();
  void LogMetricsOnAbandon(AbandonReason abandon_reason,
                           base::TimeTicks navigation_abandon_time);
  void LogAbandonHistograms(AbandonReason abandon_reason,
                            NavigationMilestone milestone,
                            base::TimeTicks event_time,
                            base::TimeTicks relative_start_time);
  virtual void LogUMAHistograms(AbandonReason abandon_reason,
                                NavigationMilestone milestone,
                                base::TimeTicks event_time,
                                base::TimeTicks relative_start_time);
  virtual void LogUKMHistograms(AbandonReason abandon_reason,
                                NavigationMilestone milestone,
                                base::TimeTicks event_time,
                                base::TimeTicks relative_start_time);
  void LogMilestoneHistogram(NavigationMilestone milestone,
                             base::TimeTicks event_time,
                             base::TimeTicks relative_start_time);
  // Log histogram with base::TimeDelta. Expects the delta is from the
  // navigation start time.
  void LogMilestoneHistogram(NavigationMilestone milestone,
                             base::TimeDelta event_time);
  // Call `LogMilestoneHistogram()` with the given loading milestone and taken
  // time from the navigation start. Also updates `latest_loading_milestone_` if
  // the given milestone is newer than the current `latest_loading_milestone_`.
  void LogLoadingMilestone(NavigationMilestone milestone, base::TimeDelta time);

  void LogNetError(net::Error error_code,
                   base::TimeTicks navigation_abandon_time);

  bool WasBackgrounded() const {
    return !first_backgrounded_timestamp_.is_null();
  }
  bool WasHidden() const { return !first_hidden_timestamp_.is_null(); }

  void OnHiddenInternal();
  void LogPreviousBackgroundingIfNeeded();
  void LogPreviousHidingIfNeeded();

  // Carveouts for child classes that want to differentiate the logged histogram
  // or react differently on the navigation events (e.g. filtering the URL).
  virtual std::string GetHistogramPrefix() const;
  virtual bool IsAllowedToLogMetrics() const;
  virtual const base::flat_map<std::string, NavigationMilestone>&
  GetCustomUserTimingMarkNames() const;
  virtual bool IsAllowedToLogUMA() const;
  virtual bool IsAllowedToLogUKM() const;
  virtual void AddSRPMetricsToUKMIfNeeded(
      ukm::builders::AbandonedSRPNavigation& builder) {}

  // Gets LCP and records UMA if it's valid. This is called from `OnComplete()`
  // and `FlushMetricsOnAppEnterBackground()` because LCP is finalized when the
  // page load is complete (or navigate away from the page).
  void FinalizeLCP();

  // The ID, start time, and type of the navigation being tracked.
  int64_t navigation_id_ = 0;
  base::TimeTicks navigation_start_time_;
  std::string_view navigation_type_;

  base::TimeTicks renderer_process_init_time_;

  // Timestamp of the first time `FlushMetricsOnAppEnterBackground()` or
  // `OnHidden()` are called, respectively. This is tracked in case the
  // abandonments are not logged immediately, e.g. when we're not sure if the
  // navigation we're tracking will involve SRP (i.e. `involved_srp_url` is
  // false).
  base::TimeTicks first_backgrounded_timestamp_;
  base::TimeTicks first_hidden_timestamp_;
  bool started_in_foreground_ = false;

  // Timestamp of the first and latest `OnShown()` call.
  base::TimeTicks first_shown_timestamp_;
  base::TimeTicks last_shown_timestamp_;

  // Whether we've previously logged backgrounding/hiding time. This is useful
  // because we will keep observing when backgrounding/hiding happens, unlike
  // other abandonment triggers. This ensures we will only log those events
  // once.
  bool did_log_backgrounding_ = false;
  bool did_log_hiding_ = false;
  // Whether the navigation has been abandoned terminally before
  // (non-backgrounding/hiding).
  bool did_terminally_abandon_navigation_ = false;

  // Whether the NavigationStart histogram, which should only be logged once per
  // navigation, has been logged before.
  bool did_log_navigation_start_ = false;

  // Whether the Navigation Response came from http cache or not.
  bool was_cached_ = false;

  // LCP is finalized in `FlushMetricsOnAppEnterBackground()` or `OnComplete()`.
  // In `AbandonedPageLoadMetricsObserver`, we keep observing events even after
  // the app is backgrounded, and that means both events are called. To prevent
  // LCP being recorded twice, `is_lcp_finalized_` will be true once the LCP is
  // recorded.
  bool is_lcp_finalized_ = false;

  // The most up-to-date NavigationHandleTiming for the navigation we're
  // tracking, updated from `OnNavigationHandleTimingUpdated()`.
  content::NavigationHandleTiming latest_navigation_handle_timing_;
  // The `latest_navigation_handle_timing_` value of the last time we called
  // `LogNavigationMilestoneMetrics()`. This is needed because that function can
  // be called multiple times, but we only want to log the milestones that we
  // haven't logged on a previous call before.
  content::NavigationHandleTiming last_logged_navigation_handle_timing_;

  // The array of achieved LoadingMilesone we're tracking, added from each
  // post-commit loading events that this class implements e.g. OnParseStart().
  // We need this because we'd like to log the latest milestone when the
  // navigation is abandoned, and send all logged milestones to UKM as well.
  std::map<NavigationMilestone, base::TimeDelta> loading_milestones_;
};

template <typename T>
void AbandonedPageLoadMetricsObserver::LogUKMHistogramsForAbandonMetrics(
    T& builder,
    AbandonReason abandon_reason,
    NavigationMilestone milestone,
    base::TimeTicks event_time,
    base::TimeTicks relative_start_time) {
  builder.SetAbandonReason(static_cast<int>(abandon_reason));
  builder.SetLastMilestoneBeforeAbandon(static_cast<int>(milestone));

  builder.SetAbandonTimingFromNavigationStart(
      (event_time - navigation_start_time_).InMilliseconds());
  builder.SetAbandonTimingFromLastMilestone(
      (event_time - relative_start_time).InMilliseconds());

  // Internal behavior metrics
  if (IsEventAfter(event_time, first_backgrounded_timestamp_)) {
    builder.SetPreviousBackgroundedTime(
        (first_backgrounded_timestamp_ - navigation_start_time_)
            .InMilliseconds());
  }

  if (IsEventAfter(event_time, first_hidden_timestamp_)) {
    builder.SetPreviousHiddenTime(
        (first_hidden_timestamp_ - navigation_start_time_).InMilliseconds());
  }

  if (IsEventAfter(event_time, renderer_process_init_time_)) {
    builder.SetRendererProcessInitTime(
        (renderer_process_init_time_ - navigation_start_time_)
            .InMilliseconds());
  }
}

template <typename T>
std::set<AbandonedPageLoadMetricsObserver::NavigationMilestone>
AbandonedPageLoadMetricsObserver::LogUKMHistogramsForMilestoneMetrics(
    T& builder,
    base::TimeTicks event_time) {
  std::set<NavigationMilestone> milestones;
  if (IsEventAfter(event_time,
                   latest_navigation_handle_timing_.loader_start_time)) {
    builder.SetLoaderStartTime(
        (latest_navigation_handle_timing_.loader_start_time -
         navigation_start_time_)
            .InMilliseconds());
    milestones.insert(NavigationMilestone::kLoaderStart);
  }

  if (latest_navigation_handle_timing_.first_loader_callback_time !=
          latest_navigation_handle_timing_
              .non_redirect_response_loader_callback_time &&
      IsEventAfter(
          event_time,
          latest_navigation_handle_timing_.first_loader_callback_time)) {
    builder.SetFirstRedirectResponseReceived(true);
    if (IsEventAfter(
            event_time,
            latest_navigation_handle_timing_.first_request_start_time)) {
      builder.SetFirstRedirectedRequestStartTime(
          (latest_navigation_handle_timing_.first_request_start_time -
           navigation_start_time_)
              .InMilliseconds());
    }
    milestones.insert(NavigationMilestone::kFirstRedirectResponseStart);
  } else {
    builder.SetFirstRedirectResponseReceived(false);
  }

  bool non_redirect_response_recieved =
      !latest_navigation_handle_timing_
           .non_redirect_response_loader_callback_time.is_null();
  builder.SetNonRedirectResponseReceived(non_redirect_response_recieved);
  if (non_redirect_response_recieved) {
    milestones.insert(NavigationMilestone::kNonRedirectResponseStart);
  }
  if (IsEventAfter(
          event_time,
          latest_navigation_handle_timing_.non_redirected_request_start_time)) {
    builder.SetNonRedirectedRequestStartTime(
        (latest_navigation_handle_timing_.non_redirected_request_start_time -
         navigation_start_time_)
            .InMilliseconds());
  }

  if (IsEventAfter(
          event_time,
          latest_navigation_handle_timing_.navigation_commit_sent_time)) {
    builder.SetCommitSentTime(
        (latest_navigation_handle_timing_.navigation_commit_sent_time -
         navigation_start_time_)
            .InMilliseconds());
    milestones.insert(NavigationMilestone::kCommitSent);
  }

  if (IsEventAfter(
          event_time,
          latest_navigation_handle_timing_.navigation_commit_received_time)) {
    builder.SetCommitReceivedTime(
        (latest_navigation_handle_timing_.navigation_commit_received_time -
         navigation_start_time_)
            .InMilliseconds());
    milestones.insert(NavigationMilestone::kCommitReceived);
  }

  if (IsEventAfter(
          event_time,
          latest_navigation_handle_timing_.navigation_did_commit_time)) {
    builder.SetDidCommitTime(
        (latest_navigation_handle_timing_.navigation_did_commit_time -
         navigation_start_time_)
            .InMilliseconds());
    milestones.insert(NavigationMilestone::kDidCommit);
  }

  for (const auto& loading_milestone : loading_milestones_) {
    if (!IsEventAfter(event_time,
                      loading_milestone.second + navigation_start_time_)) {
      continue;
    }
    milestones.insert(loading_milestone.first);
    auto time = loading_milestone.second.InMilliseconds();
    switch (loading_milestone.first) {
      case NavigationMilestone::kParseStart:
        builder.SetParseStartTime(time);
        break;
      case NavigationMilestone::kFirstContentfulPaint:
        builder.SetFirstContentfulPaintTime(time);
        break;
      case NavigationMilestone::kDOMContentLoaded:
        builder.SetDOMContentLoadedTime(time);
        break;
      case NavigationMilestone::kLoadEventStarted:
        builder.SetLoadEventStartedTime(time);
        break;
      case NavigationMilestone::kLargestContentfulPaint:
        builder.SetLargestContentfulPaintTime(time);
        break;
      case NavigationMilestone::kAFTStart:
      case NavigationMilestone::kAFTEnd:
      case NavigationMilestone::kHeaderChunkStart:
      case NavigationMilestone::kHeaderChunkEnd:
      case NavigationMilestone::kBodyChunkStart:
      case NavigationMilestone::kBodyChunkEnd:
      case NavigationMilestone::kNavigationStart:
      case NavigationMilestone::kLoaderStart:
      case NavigationMilestone::kFirstRedirectedRequestStart:
      case NavigationMilestone::kFirstRedirectResponseStart:
      case NavigationMilestone::kFirstRedirectResponseLoaderCallback:
      case NavigationMilestone::kNonRedirectedRequestStart:
      case NavigationMilestone::kNonRedirectResponseStart:
      case NavigationMilestone::kNonRedirectResponseLoaderCallback:
      case NavigationMilestone::kCommitSent:
      case NavigationMilestone::kCommitReceived:
      case NavigationMilestone::kCommitReplySent:
      case NavigationMilestone::kDidCommit:
      case NavigationMilestone::kSecondRedirectResponseStart:
      case NavigationMilestone::kSecondRedirectedRequestStart:
        break;
    }
  }
  return milestones;
}

#endif  // COMPONENTS_PAGE_LOAD_METRICS_BROWSER_OBSERVERS_ABANDONED_PAGE_LOAD_METRICS_OBSERVER_H_
