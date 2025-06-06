// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/page_load_metrics/browser/observers/prerender_page_load_metrics_observer.h"

#include "base/metrics/histogram_functions.h"
#include "base/no_destructor.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "base/trace_event/trace_id_helper.h"
#include "base/tracing/protos/chrome_track_event.pbzero.h"
#include "components/page_load_metrics/browser/metrics_web_contents_observer.h"
#include "components/page_load_metrics/browser/navigation_handle_user_data.h"
#include "components/page_load_metrics/browser/observers/core/largest_contentful_paint_handler.h"
#include "components/page_load_metrics/browser/page_load_metrics_util.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "net/http/http_response_headers.h"
#include "services/metrics/public/cpp/metrics_utils.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/network/public/mojom/fetch_api.mojom.h"

namespace {

void RecordShiftedTimeHistogram(const std::string& histogram_name,
                                base::TimeDelta time) {
  // Generated with Histogram::InitializeBucketRanges. Firstly generate an array
  // by `Histogram::InitializeBucketRanges(0, 60000, ranges)`, and then expand
  // the array to two sides of the axis of 60000.
  static const int ranges[] = {
      0,     11600, 20957, 28505, 34594, 39506, 43468, 46664,  49242, 51322,
      53000, 54353, 55445, 56326, 57036, 57609, 58071, 58444,  58745, 58988,
      59184, 59342, 59469, 59572, 59655, 59722, 59776, 59819,  59854, 59882,
      59905, 59923, 59938, 59950, 59960, 59968, 59974, 59979,  59983, 59986,
      59989, 59991, 59993, 59994, 59995, 59996, 59997, 59998,  59999, 60000,
      60001, 60002, 60003, 60004, 60005, 60006, 60007, 60009,  60011, 60014,
      60017, 60021, 60026, 60032, 60040, 60050, 60062, 60077,  60095, 60118,
      60146, 60181, 60224, 60278, 60345, 60428, 60531, 60658,  60816, 61012,
      61255, 61556, 61929, 62391, 62964, 63674, 64555, 65647,  67000, 68678,
      70758, 73336, 76532, 80494, 85406, 91495, 99043, 108400, 120000};
  base::HistogramBase* time_histogram = base::CustomHistogram::FactoryGet(
      histogram_name, base::CustomHistogram::ArrayToCustomEnumRanges(ranges),
      base::HistogramBase::kUmaTargetedHistogramFlag);
  time_histogram->Add(time.InMilliseconds());
}

}  // namespace

namespace internal {

const char kHistogramPrerenderNavigationToActivation[] =
    "PageLoad.Clients.Prerender.NavigationToActivation";
const char kHistogramPrerenderActivationToFirstPaint[] =
    "PageLoad.Clients.Prerender.PaintTiming.ActivationToFirstPaint";
const char kHistogramPrerenderActivationToFirstContentfulPaint[] =
    "PageLoad.Clients.Prerender.PaintTiming.ActivationToFirstContentfulPaint";
const char kHistogramPrerenderActivationToLargestContentfulPaint2[] =
    "PageLoad.Clients.Prerender.PaintTiming."
    "ActivationToLargestContentfulPaint2";
const char kHistogramPrerenderFirstInputDelay4[] =
    "PageLoad.Clients.Prerender.InteractiveTiming.FirstInputDelay4";
const char kHistogramPrerenderCumulativeShiftScore[] =
    "PageLoad.Clients.Prerender.LayoutInstability.CumulativeShiftScore";
const char kHistogramPrerenderCumulativeShiftScoreMainFrame[] =
    "PageLoad.Clients.Prerender.LayoutInstability.CumulativeShiftScore."
    "MainFrame";
const char
    kHistogramPrerenderMaxCumulativeShiftScoreSessionWindowGap1000msMax5000ms2
        [] = "PageLoad.Clients.Prerender.LayoutInstability."
             "MaxCumulativeShiftScore.SessionWindow."
             "Gap1000ms.Max5000ms2";

// Responsiveness metrics.
const char kHistogramPrerenderNumInteractions[] =
    "PageLoad.InteractiveTiming.NumInteractions.Prerender";
const char
    kHistogramPrerenderUserInteractionLatencyHighPercentile2MaxEventDuration[] =
        "PageLoad.InteractiveTiming.UserInteractionLatency."
        "HighPercentile2.MaxEventDuration.Prerender";
const char
    kHistogramPrerenderUserInteractionLatencyHighPercentile2MaxEventDurationIncognito
        [] = "PageLoad.InteractiveTiming.UserInteractionLatency."
             "HighPercentile2.MaxEventDuration.Prerender.Incognito";
const char kHistogramPrerenderWorstUserInteractionLatencyMaxEventDuration[] =
    "PageLoad.InteractiveTiming.WorstUserInteractionLatency.MaxEventDuration."
    "Prerender";

// Monitors serving preload cache to prerender.
const char kPageLoadPrerenderActivatedPageLoaderStatus[] =
    "PageLoad.Internal.Prerender2.ActivatedPageLoaderStatus";

// Lead time brought by prerender
const char kDomContentLoadedToActivation[] =
    "PageLoad.Internal.Prerender2.DomContentLoadedToActivation3";
const char kMainResourceParseStartToActivation[] =
    "PageLoad.Internal.Prerender2.MainResourceParseStartToActivation";

}  // namespace internal

PrerenderPageLoadMetricsObserver::PrerenderPageLoadMetricsObserver(
    bool is_incognito)
    : is_incognito_(is_incognito) {}
PrerenderPageLoadMetricsObserver::~PrerenderPageLoadMetricsObserver() = default;

enum PrerenderPageLoadMetricsObserver::PaintingTimeType : uint8_t {
  kFirstContentfulPaint,
  kLargestContentfulPaint,
};

page_load_metrics::PageLoadMetricsObserver::ObservePolicy
PrerenderPageLoadMetricsObserver::OnStart(
    content::NavigationHandle* navigation_handle,
    const GURL& currently_committed_url,
    bool started_in_foreground) {
  return STOP_OBSERVING;
}

page_load_metrics::PageLoadMetricsObserver::ObservePolicy
PrerenderPageLoadMetricsObserver::OnFencedFramesStart(
    content::NavigationHandle* navigation_handle,
    const GURL& currently_committed_url) {
  // TODO(crbug.com/40228553): Prerendering pages embedding FencedFrames
  // are not supported. So, this class doesn't need forwarding.
  DCHECK(!navigation_handle->IsInPrerenderedMainFrame());
  return STOP_OBSERVING;
}

page_load_metrics::PageLoadMetricsObserver::ObservePolicy
PrerenderPageLoadMetricsObserver::OnPrerenderStart(
    content::NavigationHandle* navigation_handle,
    const GURL& currently_committed_url) {
  // TODO(crbug.com/40228553): Prerendering pages embedding FencedFrames
  // are not supported.
  DCHECK(navigation_handle->GetNavigatingFrameType() !=
         content::FrameType::kFencedFrameRoot);
  return CONTINUE_OBSERVING;
}

void PrerenderPageLoadMetricsObserver::DidActivatePrerenderedPage(
    content::NavigationHandle* navigation_handle) {
  // Copy the trigger type and histogram suffix for an embedder. These data will
  // be lost after NavigationRequest is destroyed.
  DCHECK(!trigger_type_.has_value());
  trigger_type_ = navigation_handle->GetPrerenderTriggerType();
  embedder_histogram_suffix_ =
      navigation_handle->GetPrerenderEmbedderHistogramSuffix();

  const net::HttpResponseHeaders* response_headers =
      navigation_handle->GetResponseHeaders();
  if (response_headers) {
    main_frame_resource_has_no_store_ =
        response_headers->HasHeaderValue("cache-control", "no-store");
  }

  // |navigation_handle| here is for the activation navigation, while
  // |GetDelegate().GetNavigationStart()| is the start time of initial prerender
  // navigation.
  navigation_to_activation_time_ =
      navigation_handle->NavigationStart() - GetDelegate().GetNavigationStart();
  base::UmaHistogramCustomTimes(
      AppendSuffix(internal::kHistogramPrerenderNavigationToActivation),
      navigation_to_activation_time_.value(), base::Milliseconds(10),
      base::Minutes(10), 100);

  ukm::builders::PrerenderPageLoad builder(GetDelegate().GetPageUkmSourceId());
  if (main_frame_resource_has_no_store_.has_value()) {
    builder.SetMainFrameResource_RequestHasNoStore(
        main_frame_resource_has_no_store_.value() ? 1 : 0);
  }

  auto prerender_trigger_type =
      page_load_metrics::NavigationHandleUserData::InitiatorLocation::kOther;
  auto* navigation_userdata =
      page_load_metrics::NavigationHandleUserData::GetForNavigationHandle(
          *navigation_handle);
  if (navigation_userdata) {
    prerender_trigger_type = navigation_userdata->navigation_type();
  }

  builder.SetWasPrerendered(true)
      .SetTiming_NavigationToActivation(
          navigation_to_activation_time_.value().InMilliseconds())
      .SetNavigation_PageTransition(navigation_handle->GetPageTransition())
      .SetNavigation_InitiatorLocation(
          static_cast<int>(prerender_trigger_type));
  builder.Record(ukm::UkmRecorder::Get());
}

void PrerenderPageLoadMetricsObserver::OnFirstPaintInPage(
    const page_load_metrics::mojom::PageLoadTiming& timing) {
  if (!WasActivatedInForegroundOptionalEventInForeground(
          timing.paint_timing->first_paint, GetDelegate())) {
    return;
  }
  base::TimeDelta activation_to_fp =
      page_load_metrics::CorrectEventAsNavigationOrActivationOrigined(
          GetDelegate(), timing.paint_timing->first_paint.value());
  base::UmaHistogramCustomTimes(
      AppendSuffix(internal::kHistogramPrerenderActivationToFirstPaint),
      activation_to_fp, base::Milliseconds(10), base::Minutes(10), 100);
}

void PrerenderPageLoadMetricsObserver::OnFirstContentfulPaintInPage(
    const page_load_metrics::mojom::PageLoadTiming& timing) {
  if (!WasActivatedInForegroundOptionalEventInForeground(
          timing.paint_timing->first_contentful_paint, GetDelegate())) {
    return;
  }
  base::TimeDelta activation_to_fcp =
      page_load_metrics::CorrectEventAsNavigationOrActivationOrigined(
          GetDelegate(), timing.paint_timing->first_contentful_paint.value());
  base::UmaHistogramCustomTimes(
      AppendSuffix(
          internal::kHistogramPrerenderActivationToFirstContentfulPaint),
      activation_to_fcp, base::Milliseconds(10), base::Minutes(10), 100);
  ukm::builders::PrerenderPageLoad(GetDelegate().GetPageUkmSourceId())
      .SetTiming_ActivationToFirstContentfulPaint(
          activation_to_fcp.InMilliseconds())
      .Record(ukm::UkmRecorder::Get());
  EmitPaintingMetricsTraceEvent(
      PrerenderPageLoadMetricsObserver::PaintingTimeType::kFirstContentfulPaint,
      timing.paint_timing->first_contentful_paint.value());
}

void PrerenderPageLoadMetricsObserver::OnFirstInputInPage(
    const page_load_metrics::mojom::PageLoadTiming& timing) {
  if (!WasActivatedInForegroundOptionalEventInForeground(
          timing.interactive_timing->first_input_timestamp, GetDelegate())) {
    return;
  }

  base::TimeDelta first_input_delay =
      timing.interactive_timing->first_input_delay.value();
  base::UmaHistogramCustomTimes(
      AppendSuffix(internal::kHistogramPrerenderFirstInputDelay4),
      first_input_delay, base::Milliseconds(1), base::Seconds(60), 50);
  ukm::builders::PrerenderPageLoad(GetDelegate().GetPageUkmSourceId())
      .SetInteractiveTiming_FirstInputDelay4(first_input_delay.InMilliseconds())
      .Record(ukm::UkmRecorder::Get());
}

void PrerenderPageLoadMetricsObserver::OnComplete(
    const page_load_metrics::mojom::PageLoadTiming& timing) {
  RecordSessionEndHistograms(timing);
}

void PrerenderPageLoadMetricsObserver::OnLoadedResource(
    const page_load_metrics::ExtraRequestCompleteInfo&
        extra_request_complete_info) {
  // Return early if this load is not for the main resource of the main frame of
  // the prerendered page.
  if (extra_request_complete_info.request_destination !=
      network::mojom::RequestDestination::kDocument) {
    return;
  }

  CHECK(!main_resource_load_status_);
  main_resource_load_status_ =
      static_cast<net::Error>(extra_request_complete_info.net_error);
}

void PrerenderPageLoadMetricsObserver::MaybeRecordMainResourceLoadStatus() {
  // Only record UMA for the activated prerendered page that has started
  // loading. If the trigger type is not set, the page is not activated since it
  // is set at `DidActivatePrerenderedPage`.  If the main_resource_load_status_
  // is not set, the page is not loaded.
  if (!trigger_type_ || !main_resource_load_status_.has_value()) {
    return;
  }
  base::UmaHistogramSparse(
      AppendSuffix(internal::kPageLoadPrerenderActivatedPageLoaderStatus),
      std::abs(*main_resource_load_status_));
}

page_load_metrics::PageLoadMetricsObserver::ObservePolicy
PrerenderPageLoadMetricsObserver::FlushMetricsOnAppEnterBackground(
    const page_load_metrics::mojom::PageLoadTiming& timing) {
  RecordSessionEndHistograms(timing);
  return STOP_OBSERVING;
}

void PrerenderPageLoadMetricsObserver::MaybeRecordDocumentLoadMetrics(
    const page_load_metrics::mojom::PageLoadTiming& main_frame_timing) {
  if (!trigger_type_ || !main_resource_load_status_.has_value()) {
    return;
  }
  CHECK(navigation_to_activation_time_.has_value());
  const base::TimeDelta upper_bound = base::Minutes(1);

  // If the event is not set, treat it's timestamp as infinite and then clamp it
  // to the lower bound of the histogram, which indicates that the event is
  // never fired.
  base::TimeDelta main_resource_parse_start =
      main_frame_timing.parse_timing->parse_start.value_or(
          upper_bound + navigation_to_activation_time_.value());
  base::TimeDelta dom_content_loaded_event_start =
      main_frame_timing.document_timing->dom_content_loaded_event_start
          .value_or(upper_bound + navigation_to_activation_time_.value());

  // Shift the duration by the upper bound because UMA cannot handle negative
  // values.
  base::TimeDelta shifted_main_resource_parse_start_duration =
      navigation_to_activation_time_.value() - main_resource_parse_start +
      upper_bound;
  base::TimeDelta shifted_dom_content_loaded_event_start_duration =
      navigation_to_activation_time_.value() - dom_content_loaded_event_start +
      upper_bound;

  RecordShiftedTimeHistogram(
      AppendSuffix(internal::kDomContentLoadedToActivation),
      shifted_dom_content_loaded_event_start_duration);
  RecordShiftedTimeHistogram(
      AppendSuffix(internal::kMainResourceParseStartToActivation),
      shifted_main_resource_parse_start_duration);
  // TODO(crbug.com/40240492): Add more metrics to track the loading progress on
  // the renderer side, e.g., loaded the blocking resources, etc.
}

void PrerenderPageLoadMetricsObserver::RecordSessionEndHistograms(
    const page_load_metrics::mojom::PageLoadTiming& main_frame_timing) {
  MaybeRecordMainResourceLoadStatus();
  MaybeRecordDocumentLoadMetrics(main_frame_timing);

  if (!GetDelegate().WasPrerenderedThenActivatedInForeground() ||
      !main_frame_timing.activation_start) {
    // Even if the page was activated, activation_start may not yet been
    // notified by the renderer. Ignore such page loads.
    return;
  }

  // Records Largest Contentful Paint (LCP) to UMA and UKM.
  const page_load_metrics::ContentfulPaintTimingInfo& largest_contentful_paint =
      GetDelegate()
          .GetLargestContentfulPaintHandler()
          .MergeMainFrameAndSubframes();
  if (largest_contentful_paint.ContainsValidTime() &&
      WasActivatedInForegroundOptionalEventInForeground(
          largest_contentful_paint.Time(), GetDelegate())) {
    base::TimeDelta activation_to_lcp =
        largest_contentful_paint.Time().value() -
        main_frame_timing.activation_start.value();
    base::UmaHistogramCustomTimes(
        AppendSuffix(
            internal::kHistogramPrerenderActivationToLargestContentfulPaint2),
        activation_to_lcp, base::Milliseconds(10), base::Minutes(10), 100);
    ukm::builders::PrerenderPageLoad(GetDelegate().GetPageUkmSourceId())
        .SetTiming_ActivationToLargestContentfulPaint(
            activation_to_lcp.InMilliseconds())
        .Record(ukm::UkmRecorder::Get());
    EmitPaintingMetricsTraceEvent(PrerenderPageLoadMetricsObserver::
                                      PaintingTimeType::kLargestContentfulPaint,
                                  largest_contentful_paint.Time().value());
  }

  // Record metrics only when a prerendered page is successfully activated.
  // TODO(crbug.com/40238929): add tests to make sure that CLS and INP metrics
  // are not recorded when prerendering is canceled.
  if (GetDelegate().GetPrerenderingState() ==
      page_load_metrics::PrerenderingState::kActivated) {
    RecordLayoutShiftScoreMetrics(main_frame_timing);
    RecordNormalizedResponsivenessMetrics();
  }
}

void PrerenderPageLoadMetricsObserver::RecordLayoutShiftScoreMetrics(
    const page_load_metrics::mojom::PageLoadTiming& main_frame_timing) {
  DCHECK(GetDelegate().WasPrerenderedThenActivatedInForeground());
  DCHECK(main_frame_timing.activation_start);

  base::UmaHistogramCounts100(
      AppendSuffix(internal::kHistogramPrerenderCumulativeShiftScore),
      page_load_metrics::LayoutShiftUmaValue(
          GetDelegate().GetPageRenderData().layout_shift_score));
  base::UmaHistogramCounts100(
      AppendSuffix(internal::kHistogramPrerenderCumulativeShiftScoreMainFrame),
      page_load_metrics::LayoutShiftUmaValue(
          GetDelegate().GetMainFrameRenderData().layout_shift_score));

  const page_load_metrics::NormalizedCLSData& normalized_cls_data =
      GetDelegate().GetNormalizedCLSData(
          page_load_metrics::PageLoadMetricsObserverDelegate::BfcacheStrategy::
              ACCUMULATE);
  if (normalized_cls_data.data_tainted) {
    return;
  }

  page_load_metrics::UmaMaxCumulativeShiftScoreHistogram10000x(
      AppendSuffix(
          internal::
              kHistogramPrerenderMaxCumulativeShiftScoreSessionWindowGap1000msMax5000ms2),
      normalized_cls_data);
  const float max_cls =
      normalized_cls_data.session_windows_gap1000ms_max5000ms_max_cls;
  ukm::builders::PrerenderPageLoad(GetDelegate().GetPageUkmSourceId())
      .SetLayoutInstability_MaxCumulativeShiftScore_SessionWindow_Gap1000ms_Max5000ms(
          page_load_metrics::LayoutShiftUkmValue(max_cls))
      .Record(ukm::UkmRecorder::Get());
}

void PrerenderPageLoadMetricsObserver::RecordNormalizedResponsivenessMetrics() {
  DCHECK(GetDelegate().WasPrerenderedThenActivatedInForeground());

  const page_load_metrics::ResponsivenessMetricsNormalization&
      responsiveness_metrics_normalization =
          GetDelegate().GetResponsivenessMetricsNormalization();
  if (!responsiveness_metrics_normalization.num_user_interactions()) {
    return;
  }

  base::TimeDelta high_percentile2_max_event_duration =
      responsiveness_metrics_normalization.ApproximateHighPercentile()
          .value()
          .interaction_latency;

  UmaHistogramCustomTimes(
      internal::kHistogramPrerenderWorstUserInteractionLatencyMaxEventDuration,
      responsiveness_metrics_normalization.worst_latency()
          .value()
          .interaction_latency,
      base::Milliseconds(1), base::Seconds(60), 50);
  UmaHistogramCustomTimes(
      internal::
          kHistogramPrerenderUserInteractionLatencyHighPercentile2MaxEventDuration,
      high_percentile2_max_event_duration, base::Milliseconds(1),
      base::Seconds(60), 50);

  if (is_incognito_) {
    UmaHistogramCustomTimes(
        internal::
            kHistogramPrerenderUserInteractionLatencyHighPercentile2MaxEventDurationIncognito,
        high_percentile2_max_event_duration, base::Milliseconds(1),
        base::Seconds(60), 50);
  }

  base::UmaHistogramCounts1000(
      internal::kHistogramPrerenderNumInteractions,
      responsiveness_metrics_normalization.num_user_interactions());

  ukm::builders::PrerenderPageLoad builder(GetDelegate().GetPageUkmSourceId());
  builder.SetInteractiveTiming_WorstUserInteractionLatency_MaxEventDuration(
      responsiveness_metrics_normalization.worst_latency()
          .value()
          .interaction_latency.InMilliseconds());

  builder
      .SetInteractiveTiming_UserInteractionLatency_HighPercentile2_MaxEventDuration(
          high_percentile2_max_event_duration.InMilliseconds());
  builder.SetInteractiveTiming_NumInteractions(
      ukm::GetExponentialBucketMinForCounts1000(
          responsiveness_metrics_normalization.num_user_interactions()));

  builder.Record(ukm::UkmRecorder::Get());
}

void PrerenderPageLoadMetricsObserver::EmitPaintingMetricsTraceEvent(
    PaintingTimeType type,
    base::TimeDelta paint_timing) const {
  CHECK(navigation_to_activation_time_.has_value());
  const base::TimeTicks navigation_start = GetDelegate().GetNavigationStart();
  const base::TimeTicks activation_start =
      navigation_start + navigation_to_activation_time_.value();
  const perfetto::Track track(base::trace_event::GetNextGlobalTraceId(),
                              perfetto::ProcessTrack::Current());
  switch (type) {
    case PaintingTimeType::kFirstContentfulPaint:
      TRACE_EVENT_BEGIN(
          "loading,interactions",
          "PageLoadMetrics.NavigationToFirstContentfulPaint", track,
          activation_start, [&](perfetto::EventContext& ctx) {
            auto* page_load_proto =
                ctx.event<perfetto::protos::pbzero::ChromeTrackEvent>()
                    ->set_page_load();
            page_load_proto->set_url(
                GetDelegate().GetUrl().possibly_invalid_spec());
            page_load_proto->set_navigation_id(GetDelegate().GetNavigationId());
          });

      TRACE_EVENT_END("loading,interactions", track,
                      navigation_start + paint_timing);
      break;
    case PaintingTimeType::kLargestContentfulPaint:
      TRACE_EVENT_BEGIN(
          "loading,interactions",
          "PageLoadMetrics.NavigationToLargestContentfulPaint", track,
          activation_start, [&](perfetto::EventContext& ctx) {
            auto* page_load_proto =
                ctx.event<perfetto::protos::pbzero::ChromeTrackEvent>()
                    ->set_page_load();
            page_load_proto->set_navigation_id(GetDelegate().GetNavigationId());
          });

      TRACE_EVENT_END("loading,interactions", track,
                      navigation_start + paint_timing);
      break;
  }
}

std::string PrerenderPageLoadMetricsObserver::AppendSuffix(
    const std::string& histogram_name) const {
  DCHECK(trigger_type_.has_value());
  switch (trigger_type_.value()) {
    case content::PreloadingTriggerType::kSpeculationRule:
      DCHECK(embedder_histogram_suffix_.empty());
      return histogram_name + ".SpeculationRule";
    case content::PreloadingTriggerType::kSpeculationRuleFromIsolatedWorld:
      DCHECK(embedder_histogram_suffix_.empty());
      return histogram_name + ".SpeculationRuleFromIsolatedWorld";
    case content::PreloadingTriggerType::
        kSpeculationRuleFromAutoSpeculationRules:
      DCHECK(embedder_histogram_suffix_.empty());
      return histogram_name + ".SpeculationRuleFromAutoSpeculationRules";
    case content::PreloadingTriggerType::kEmbedder:
      DCHECK(!embedder_histogram_suffix_.empty());
      return histogram_name + ".Embedder_" + embedder_histogram_suffix_;
  }
  NOTREACHED();
}
