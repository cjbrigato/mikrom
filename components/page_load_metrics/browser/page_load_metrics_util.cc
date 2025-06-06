// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/page_load_metrics/browser/page_load_metrics_util.h"

#include <algorithm>
#include <string_view>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "components/page_load_metrics/browser/features.h"
#include "components/page_load_metrics/common/page_load_timing.h"
#include "components/page_load_metrics/common/page_visit_final_status.h"
#include "net/base/url_util.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/metrics/public/cpp/ukm_recorder.h"

namespace page_load_metrics {

namespace {

bool IsBackgroundAbort(const PageLoadMetricsObserverDelegate& delegate) {
  if (!delegate.StartedInForeground() || !delegate.GetTimeToFirstBackground())
    return false;

  if (!delegate.GetTimeToPageEnd())
    return true;

  return delegate.GetTimeToFirstBackground() <= delegate.GetTimeToPageEnd();
}

PageAbortReason GetAbortReasonForEndReason(PageEndReason end_reason) {
  switch (end_reason) {
    case END_RELOAD:
      return ABORT_RELOAD;
    case END_FORWARD_BACK:
      return ABORT_FORWARD_BACK;
    case END_NEW_NAVIGATION:
      return ABORT_NEW_NAVIGATION;
    case END_STOP:
      return ABORT_STOP;
    case END_CLOSE:
      return ABORT_CLOSE;
    case END_OTHER:
      return ABORT_OTHER;
    default:
      return ABORT_NONE;
  }
}

// Common helper for QueryContainsComponent and QueryContainsComponentPrefix.
bool QueryContainsComponentHelper(std::string_view query,
                                  std::string_view component,
                                  bool component_is_prefix) {
  if (query.empty() || component.empty() ||
      component.length() > query.length()) {
    return false;
  }

  // Ensures that the first character of |query| is not a query or fragment
  // delimiter character (? or #). Including it can break the later test for
  // |component| being at the start of the query string.
  // Note: This heuristic can cause a component string that starts with one of
  // these characters to not match a query string which contains it at the
  // beginning.
  const std::string_view trimmed_query =
      base::TrimString(query, "?#", base::TrimPositions::TRIM_LEADING);

  // We shouldn't try to find matches beyond the point where there aren't enough
  // characters left in query to fully match the component.
  const size_t last_search_start = trimmed_query.length() - component.length();

  // We need to search for matches in a loop, rather than stopping at the first
  // match, because we may initially match a substring that isn't a full query
  // string component. Consider, for instance, the query string 'ab=cd&b=c'. If
  // we search for component 'b=c', the first substring match will be characters
  // 1-3 (zero-based) in the query string. However, this isn't a full component
  // (the full component is ab=cd) so the match will fail. Thus, we must
  // continue our search to find the second substring match, which in the
  // example is at characters 6-8 (the end of the query string) and is a
  // successful component match.
  for (size_t start_offset = 0; start_offset <= last_search_start;
       start_offset += component.length()) {
    start_offset = trimmed_query.find(component, start_offset);
    if (start_offset == std::string::npos) {
      // We searched to end of string and did not find a match.
      return false;
    }
    // Verify that the character prior to the component is valid (either we're
    // at the beginning of the query string, or are preceded by an ampersand).
    if (start_offset != 0 && trimmed_query[start_offset - 1] != '&') {
      continue;
    }
    if (!component_is_prefix) {
      // Verify that the character after the component substring is valid
      // (either we're at the end of the query string, or are followed by an
      // ampersand).
      const size_t after_offset = start_offset + component.length();
      if (after_offset < trimmed_query.length() &&
          trimmed_query[after_offset] != '&') {
        continue;
      }
    }
    return true;
  }
  return false;
}

// Returns the category ID given a string if it matches the configured pattern.
std::optional<uint32_t> GetCategoryId(const std::string& category) {
  auto category_prefix = features::kBeaconLeakageLoggingCategoryPrefix.Get();
  if (category_prefix.empty() || !category.starts_with(category_prefix)) {
    return std::nullopt;
  }

  uint32_t category_id;
  if (!base::StringToUint(category.substr(category_prefix.size()),
                          &category_id)) {
    return std::nullopt;
  }

  return category_id;
}

}  // namespace

void UmaMaxCumulativeShiftScoreHistogram10000x(
    const std::string& name,
    const page_load_metrics::NormalizedCLSData& normalized_cls_data) {
  base::UmaHistogramCustomCounts(
      name,
      page_load_metrics::LayoutShiftUmaValue10000(
          normalized_cls_data.session_windows_gap1000ms_max5000ms_max_cls),
      1, 24000, 50);
}

bool WasStartedInForegroundOptionalEventInForeground(
    const std::optional<base::TimeDelta>& event,
    const PageLoadMetricsObserverDelegate& delegate) {
  return delegate.StartedInForeground() && event &&
         (!delegate.GetTimeToFirstBackground() ||
          event.value() <= delegate.GetTimeToFirstBackground().value());
}

// There is a copy of this function in prerender_page_load_metrics_observer.cc.
// Please keep this consistent with the function.
bool WasActivatedInForegroundOptionalEventInForeground(
    const std::optional<base::TimeDelta>& event,
    const PageLoadMetricsObserverDelegate& delegate) {
  return delegate.WasPrerenderedThenActivatedInForeground() && event &&
         (!delegate.GetTimeToFirstBackground() ||
          event.value() <= delegate.GetTimeToFirstBackground().value());
}

bool WasStartedInForegroundOptionalEventInForegroundAfterBackForwardCacheRestore(
    const std::optional<base::TimeDelta>& event,
    const PageLoadMetricsObserverDelegate& delegate,
    size_t index) {
  const auto& back_forward_cache_restore =
      delegate.GetBackForwardCacheRestore(index);
  std::optional<base::TimeDelta> first_background_time =
      back_forward_cache_restore.first_background_time;
  return back_forward_cache_restore.was_in_foreground && event &&
         (!first_background_time ||
          event.value() <= first_background_time.value());
}

bool WasStartedInBackgroundOptionalEventInForeground(
    const std::optional<base::TimeDelta>& event,
    const PageLoadMetricsObserverDelegate& delegate) {
  return !delegate.StartedInForeground() && event &&
         delegate.GetTimeToFirstForeground() &&
         delegate.GetTimeToFirstForeground().value() <= event.value() &&
         (!delegate.GetTimeToFirstBackground() ||
          event.value() <= delegate.GetTimeToFirstBackground().value());
}

bool WasInForeground(const PageLoadMetricsObserverDelegate& delegate) {
  return delegate.StartedInForeground() || delegate.GetTimeToFirstForeground();
}

std::optional<base::TimeDelta> GetNonPrerenderingBackgroundStartTiming(
    const PageLoadMetricsObserverDelegate& delegate) {
  switch (delegate.GetPrerenderingState()) {
    case PrerenderingState::kNoPrerendering:
    case PrerenderingState::kInPreview:
      if (delegate.StartedInForeground()) {
        return delegate.GetTimeToFirstBackground();
      } else {
        return base::Seconds(0);
      }
    case PrerenderingState::kInPrerendering:
    case PrerenderingState::kActivatedNoActivationStart:
      return std::nullopt;
    case PrerenderingState::kActivated:
      if (delegate.GetVisibilityAtActivation() == PageVisibility::kForeground) {
        return delegate.GetTimeToFirstBackground();
      } else {
        return delegate.GetActivationStart();
      }
  }
}

bool EventOccurredBeforeNonPrerenderingBackgroundStart(
    const PageLoadMetricsObserverDelegate& delegate,
    const base::TimeDelta& event) {
  // If background start is nullopt, it'll must be greater than already
  // occurred event.
  const base::TimeDelta bg_start =
      GetNonPrerenderingBackgroundStartTiming(delegate).value_or(
          base::TimeDelta::Max());
  return event < bg_start;
}

// Currently, multiple implementations of PageLoadMetricsObserver is ongoing.
// We'll left the old version for a while.
// TODO(crbug.com/40222513): Use the above version and delete this.
bool EventOccurredBeforeNonPrerenderingBackgroundStart(
    const PageLoadMetricsObserverDelegate& delegate,
    const page_load_metrics::mojom::PageLoadTiming& timing,
    const base::TimeDelta& event) {
  return EventOccurredBeforeNonPrerenderingBackgroundStart(delegate, event);
}

base::TimeDelta CorrectEventAsNavigationOrActivationOrigined(
    const PageLoadMetricsObserverDelegate& delegate,
    const base::TimeDelta& event) {
  base::TimeDelta zero = base::Seconds(0);

  switch (delegate.GetPrerenderingState()) {
    case PrerenderingState::kNoPrerendering:
    case PrerenderingState::kInPreview:
      return event;
    case PrerenderingState::kInPrerendering:
    case PrerenderingState::kActivatedNoActivationStart:
      return zero;
    case PrerenderingState::kActivated: {
      base::TimeDelta corrected = event - delegate.GetActivationStart().value();
      CHECK_GE(corrected, zero);
      return corrected;
    }
  }
}

// Currently, multiple implementations of PageLoadMetricsObserver is ongoing.
// We'll left the old version for a while.
// TODO(crbug.com/40222513): Use the above version and delete this.
base::TimeDelta CorrectEventAsNavigationOrActivationOrigined(
    const PageLoadMetricsObserverDelegate& delegate,
    const page_load_metrics::mojom::PageLoadTiming& timing,
    const base::TimeDelta& event) {
  return CorrectEventAsNavigationOrActivationOrigined(delegate, event);
}

PageAbortInfo GetPageAbortInfo(
    const PageLoadMetricsObserverDelegate& delegate) {
  if (IsBackgroundAbort(delegate)) {
    // Though most cases where a tab is backgrounded are user initiated, we
    // can't be certain that we were backgrounded due to a user action. For
    // example, on Android, the screen times out after a period of inactivity,
    // resulting in a non-user-initiated backgrounding.
    return {ABORT_BACKGROUND, UserInitiatedInfo::NotUserInitiated(),
            delegate.GetTimeToFirstBackground().value()};
  }

  PageAbortReason abort_reason =
      GetAbortReasonForEndReason(delegate.GetPageEndReason());
  if (abort_reason == ABORT_NONE)
    return PageAbortInfo();

  return {abort_reason, delegate.GetPageEndUserInitiatedInfo(),
          delegate.GetTimeToPageEnd().value()};
}

std::optional<base::TimeDelta> GetInitialForegroundDuration(
    const PageLoadMetricsObserverDelegate& delegate,
    base::TimeTicks app_background_time) {
  if (!delegate.StartedInForeground())
    return std::nullopt;

  std::optional<base::TimeDelta> time_on_page = OptionalMin(
      delegate.GetTimeToFirstBackground(), delegate.GetTimeToPageEnd());

  // If we don't have a time_on_page value yet, and we have an app background
  // time, use the app background time as our end time. This addresses cases
  // where the Chrome app is backgrounded before the page load is complete, on
  // platforms where Chrome may be killed once it goes into the background
  // (Android). In these cases, we use the app background time as the 'end
  // time'.
  if (!time_on_page && !app_background_time.is_null()) {
    time_on_page = app_background_time - delegate.GetNavigationStart();
  }
  return time_on_page;
}

bool DidObserveLoadingBehaviorInAnyFrame(
    const PageLoadMetricsObserverDelegate& delegate,
    blink::LoadingBehaviorFlag behavior) {
  const int all_frame_loading_behavior_flags =
      delegate.GetMainFrameMetadata().behavior_flags |
      delegate.GetSubframeMetadata().behavior_flags;

  return (all_frame_loading_behavior_flags & behavior) != 0;
}

bool IsZstdUrl(const GURL& url) {
  return url.DomainIs("facebook.com") || url.DomainIs("instagram.com") ||
         url.DomainIs("whatsapp.com") || url.DomainIs("messenger.com");
}

bool QueryContainsComponent(std::string_view query,
                            std::string_view component) {
  return QueryContainsComponentHelper(query, component, false);
}

bool QueryContainsComponentPrefix(std::string_view query,
                                  std::string_view component) {
  return QueryContainsComponentHelper(query, component, true);
}

int64_t LayoutShiftUkmValue(float shift_score) {
  // Report (shift_score * 100) as an int in the range [0, 1000].
  return static_cast<int>(roundf(std::min(shift_score, 10.0f) * 100.0f));
}

int32_t LayoutShiftUmaValue(float shift_score) {
  // Report (shift_score * 10) as an int in the range [0, 100].
  return static_cast<int>(roundf(std::min(shift_score, 10.0f) * 10.0f));
}

int32_t LayoutShiftUmaValue10000(float shift_score) {
  // Report (shift_score * 10000) as an int in the range [0, 100000].
  return static_cast<int>(roundf(std::min(shift_score, 10.0f) * 10000.0f));
}

PageVisitFinalStatus RecordPageVisitFinalStatusForTiming(
    const page_load_metrics::mojom::PageLoadTiming& timing,
    const PageLoadMetricsObserverDelegate& delegate,
    ukm::SourceId source_id) {
  PageVisitFinalStatus page_visit_status =
      PageVisitFinalStatus::kNeverForegrounded;
  if (page_load_metrics::WasInForeground(delegate)) {
    page_visit_status = timing.paint_timing->first_contentful_paint.has_value()
                            ? PageVisitFinalStatus::kReachedFCP
                            : PageVisitFinalStatus::kAborted;
  }
  ukm::builders::UserPerceivedPageVisit pageVisitBuilder(source_id);
  pageVisitBuilder.SetPageVisitFinalStatus(static_cast<int>(page_visit_status));
  pageVisitBuilder.Record(ukm::UkmRecorder::Get());
  return page_visit_status;
}

std::optional<uint32_t> GetCategoryIdFromUrl(const GURL& url) {
  std::string category;
  if (net::GetValueForKeyInQuery(
          url, features::kBeaconLeakageLoggingCategoryParamName.Get(),
          &category)) {
    return GetCategoryId(category);
  }
  return std::nullopt;
}

}  // namespace page_load_metrics
