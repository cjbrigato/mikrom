// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/attribution_reporting/attribution_host.h"

#include <stdint.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/check_op.h"
#include "base/containers/flat_set.h"
#include "base/debug/crash_logging.h"
#include "base/feature_list.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/strcat.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "components/attribution_reporting/data_host.mojom.h"
#include "components/attribution_reporting/features.h"
#include "components/attribution_reporting/registration_eligibility.mojom.h"
#include "components/attribution_reporting/suitable_origin.h"
#include "components/metrics/dwa/dwa_builders.h"
#include "components/metrics/dwa/dwa_recorder.h"
#include "content/browser/attribution_reporting/attribution_data_host_manager.h"
#include "content/browser/attribution_reporting/attribution_input_event.h"
#include "content/browser/attribution_reporting/attribution_manager.h"
#include "content/browser/attribution_reporting/attribution_suitable_context.h"
#include "content/browser/renderer_host/frame_tree.h"
#include "content/browser/renderer_host/frame_tree_node.h"
#include "content/browser/renderer_host/navigation_request.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_handle_user_data.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_features.h"
#include "mojo/public/cpp/bindings/message.h"
#include "net/url_request/url_request.h"
#include "services/metrics/public/cpp/metrics_utils.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/metrics/public/cpp/ukm_recorder.h"
#include "third_party/blink/public/common/navigation/impression.h"
#include "url/gurl.h"
#include "url/origin.h"

#if BUILDFLAG(IS_ANDROID)
#include "content/browser/attribution_reporting/attribution_input_event_tracker_android.h"
#endif

namespace content {

namespace {

using ::attribution_reporting::SuitableOrigin;

// Auxiliary data that lives alongside a NavigationHandle, tracking
// whether the navigation is "insecurely tainted" i.e. has seen an
// insecure hop in its path. This should only be present for navigations which
// have had at least one secure response with Attribution headers.
//
// For now, it is only used for metrics collection, but it may be extended in
// the future to block secure registration attempts after an insecure redirect
// was seen. See https://github.com/WICG/attribution-reporting-api/issues/767.
class InsecureTaintTracker
    : public NavigationHandleUserData<InsecureTaintTracker> {
 public:
  explicit InsecureTaintTracker(NavigationHandle&) {}
  ~InsecureTaintTracker() override {
    if (had_any_secure_attempts_) {
      base::UmaHistogramExactLinear("Conversions.IncrementalTaintingFailures",
                                    num_secure_registrations_after_tainting_,
                                    net::URLRequest::kMaxRedirects + 1);
    }
  }

  InsecureTaintTracker(const InsecureTaintTracker&) = delete;
  InsecureTaintTracker& operator=(const InsecureTaintTracker&) = delete;

  void TaintInsecure() { navigation_is_tainted_ = true; }

  void NotifySecureRegistrationAttempt() {
    had_any_secure_attempts_ = true;
    num_secure_registrations_after_tainting_ += navigation_is_tainted_;
  }

  NAVIGATION_HANDLE_USER_DATA_KEY_DECL();

 private:
  int num_secure_registrations_after_tainting_ = 0;
  bool navigation_is_tainted_ = false;
  bool had_any_secure_attempts_ = false;
};
NAVIGATION_HANDLE_USER_DATA_KEY_IMPL(InsecureTaintTracker);

void ClientBounceHistogram(std::string_view user_interaction_type,
                           std::string_view timeout,
                           int value) {
  base::UmaHistogramCounts100(
      base::StrCat({"Conversions.NumDataHostsRegisteredOnClientBounce.",
                    user_interaction_type, ".", timeout}),
      value);
}

}  // namespace

AttributionHost::PrimaryMainFrameData::PrimaryMainFrameData() = default;

AttributionHost::PrimaryMainFrameData::PrimaryMainFrameData(
    PrimaryMainFrameData&&) = default;

AttributionHost::PrimaryMainFrameData&
AttributionHost::PrimaryMainFrameData::operator=(PrimaryMainFrameData&&) =
    default;

AttributionHost::PrimaryMainFrameData::~PrimaryMainFrameData() = default;

AttributionHost::AttributionHost(WebContents* web_contents)
    : WebContentsObserver(web_contents),
      WebContentsUserData<AttributionHost>(*web_contents),
      receivers_(web_contents, this) {
  DCHECK(base::FeatureList::IsEnabled(
      attribution_reporting::features::kConversionMeasurement));

#if BUILDFLAG(IS_ANDROID)
  input_event_tracker_android_ =
      std::make_unique<AttributionInputEventTrackerAndroid>(web_contents);
#endif
}

AttributionHost::~AttributionHost() {
  DCHECK(ongoing_registration_eligible_navigations_.empty());
}

AttributionInputEvent AttributionHost::GetMostRecentNavigationInputEvent()
    const {
  AttributionInputEvent input;
#if BUILDFLAG(IS_ANDROID)
  if (input_event_tracker_android_) {
    AttributionInputEventTrackerAndroid::InputEvent input_event =
        input_event_tracker_android_->GetMostRecentEvent();
    input.input_event = std::move(input_event.event);
    input.input_event_id = input_event.id;
  }
#endif
  return input;
}

void AttributionHost::DidStartNavigation(NavigationHandle* navigation_handle) {
  // Impression navigations need to navigate the primary main frame to be valid.
  // Impressions should never be attached to same-document navigations but can
  // be the result of a bad renderer.

  // A navigation is considered client bounce if it navigates the current page
  // away within a short period of time without any user interaction with the
  // page, and the navigation is not user initiated. Client bounce detection
  // only cares about primary main frame and non-same-document navigations.

  if (!navigation_handle->IsInPrimaryMainFrame() ||
      navigation_handle->IsSameDocument()) {
    return;
  }

  MaybeLogClientBounce(navigation_handle);

  const auto& impression = navigation_handle->GetImpression();

  // TODO(crbug.com/40262156): Consider checking for navigations taking place in
  // a prerendered main frame.
  if (!impression) {
    return;
  }
  RenderFrameHostImpl* initiator_frame_host =
      navigation_handle->GetInitiatorFrameToken().has_value()
          ? RenderFrameHostImpl::FromFrameToken(
                navigation_handle->GetInitiatorProcessId(),
                navigation_handle->GetInitiatorFrameToken().value())
          : nullptr;

  // The initiator frame host may be deleted by this point. In that case, ignore
  // this navigation and drop the impression associated with it.

  UMA_HISTOGRAM_BOOLEAN("Conversions.ImpressionNavigationHasDeadInitiator",
                        initiator_frame_host == nullptr);

  // Look up the initiator root's origin which will be used as the impression
  // origin. This works because we won't update the origin for the initiator RFH
  // until we receive confirmation from the renderer that it has committed.
  // Since frame mutation is all serialized on the Blink main thread, we get an
  // implicit ordering: a navigation with an impression attached won't be
  // processed after a navigation commit in the initiator RFH, so reading the
  // origin off is safe at the start of the navigation.
  auto suitable_context =
      AttributionSuitableContext::Create(initiator_frame_host);
  if (!suitable_context.has_value()) {
    return;
  }

  auto* navigation_request = static_cast<NavigationRequest*>(navigation_handle);

  AttributionDataHostManager* manager = suitable_context->data_host_manager();
  manager->NotifyNavigationRegistrationStarted(
      *std::move(suitable_context), impression->attribution_src_token,

      navigation_handle->GetNavigationId(),
      // The devtools_navigation_token is going to be used as the
      // navigation's request devtools inspector ID if there is an enabled
      // agent host.
      navigation_request->devtools_navigation_token().ToString());
  auto [_, inserted] = ongoing_registration_eligible_navigations_.emplace(
      navigation_handle->GetNavigationId());
  CHECK(inserted);
}

void AttributionHost::DidRedirectNavigation(
    NavigationHandle* navigation_handle) {
  NotifyNavigationRegistrationData(navigation_handle);
}

void AttributionHost::DidFinishNavigation(NavigationHandle* navigation_handle) {
  auto* attribution_manager =
      AttributionManager::FromWebContents(web_contents());

  base::Time now = base::Time::Now();

  if (attribution_manager && navigation_handle->GetNetErrorCode() == net::OK) {
    attribution_manager->UpdateLastNavigationTime(now);
  }

  if (navigation_handle->IsInPrimaryMainFrame()) {
    if (!navigation_handle->IsSameDocument()) {
      if (primary_main_frame_data_.has_value()) {
        // Resets for further client redirects.
        primary_main_frame_data_->num_data_hosts_registered = 0;
        primary_main_frame_data_->num_data_hosts_registered_by_reporting_origin
            .clear();
      }

      // Sets current time to detect further client redirects.
      last_navigation_time_ = now;

      if (navigation_handle->HasCommitted()) {
        primary_main_frame_data_ = PrimaryMainFrameData();
      }
    }

    if (navigation_handle->HasCommitted()) {
      // Note that we cache the UKM source ID of the most recently navigated
      // primary page as a workaround for prerendered pages.
      // `RenderFrameHost::GetPageUkmSourceId()` would return the UKM source ID
      // for the prerendering navigation even after the page is activated which
      // is not associated with any URL for privacy reasons. See
      // //content/browser/preloading/prerender/README.md#ukm-source-ids for
      // more details.
      last_primary_frame_ukm_source_id_ =
          ukm::ConvertToSourceId(navigation_handle->GetNavigationId(),
                                 ukm::SourceIdType::NAVIGATION_ID);
    }
  }

  const auto& impression = navigation_handle->GetImpression();
  if (!impression.has_value()) {
    return;
  }

  NotifyNavigationRegistrationData(navigation_handle);

  CHECK(attribution_manager);
  attribution_manager->GetDataHostManager()
      ->NotifyNavigationRegistrationCompleted(
          impression->attribution_src_token);

  ongoing_registration_eligible_navigations_.erase(
      navigation_handle->GetNavigationId());
}

void AttributionHost::FrameReceivedUserActivation(
    RenderFrameHost* render_frame_host) {
  // We consider user activation from all frames in the page. This event tracks
  // clicks, taps, types, but not scrolls.
  // https://html.spec.whatwg.org/multipage/interaction.html#tracking-user-activation
  if (primary_main_frame_data_.has_value()) {
    primary_main_frame_data_->has_user_activation = true;
  }
}

void AttributionHost::DidGetUserInteraction(const blink::WebInputEvent& event) {
  // This event tracks clicks, taps, types, and scrolls, see
  // `IsUserInteractionInputType()`.
  if (primary_main_frame_data_.has_value()) {
    primary_main_frame_data_->has_user_interaction = true;
  }
}

void AttributionHost::NotifyNavigationRegistrationData(
    NavigationHandle* navigation_handle) {
  if (!ongoing_registration_eligible_navigations_.contains(
          navigation_handle->GetNavigationId())) {
    return;
  }

  const std::optional<blink::Impression>& impression =
      navigation_handle->GetImpression();
  // If there is an ongoing_registration_eligible_navigation, the navigation
  // must have an associated impression, be in the primary main frame and not in
  // the same document.
  CHECK(impression.has_value());
  CHECK(navigation_handle->IsInPrimaryMainFrame());
  CHECK(!navigation_handle->IsSameDocument());

  // Populates `is_final_response` based on the headers to handle the case of an
  // intercepted redirect. See https://crbug.com/1520612.
  auto* headers = navigation_handle->GetResponseHeaders();
  const bool is_final_response =
      !(headers && headers->IsRedirect(/*location=*/nullptr));

  // On redirect, the reporting origin should be the origin of the request
  // responsible for initiating the redirect. At this point, the navigation
  // handle reflects the URL being navigated to, so instead use the second to
  // last URL in the redirect chain.
  //
  // On final response, the reporting origin should be the origin of the request
  // responsible for initiating the last (and only if no redirections) request
  // in the chain.
  const size_t offset = is_final_response ? 1 : 2;
  const std::vector<GURL>& redirect_chain =
      navigation_handle->GetRedirectChain();
  if (redirect_chain.size() < offset) {
    return;
  }
  const GURL& reporting_url = redirect_chain[redirect_chain.size() - offset];
  // Pass the suitability as a proxy for the potentially trustworthy check, as
  // redirects should only happen for HTTP-based navigations.
  auto* tracker =
      InsecureTaintTracker::GetOrCreateForNavigationHandle(*navigation_handle);
  if (!SuitableOrigin::IsSuitable(url::Origin::Create(reporting_url))) {
    tracker->TaintInsecure();
    return;
  }

  auto* attribution_manager =
      AttributionManager::FromWebContents(web_contents());
  CHECK(attribution_manager);

  bool had_header =
      attribution_manager->GetDataHostManager()
          ->NotifyNavigationRegistrationData(impression->attribution_src_token,
                                             headers, reporting_url);

  if (had_header) {
    tracker->NotifySecureRegistrationAttempt();
  }
}

void AttributionHost::RegisterDataHost(
    mojo::PendingReceiver<attribution_reporting::mojom::DataHost> data_host,
    attribution_reporting::mojom::RegistrationEligibility
        registration_eligibility,
    bool is_for_background_requests,
    const std::vector<url::Origin>& reporting_origins) {
  auto suitable_context = AttributionSuitableContext::Create(
      static_cast<RenderFrameHostImpl*>(receivers_.GetCurrentTargetFrame()));
  if (!suitable_context.has_value()) {
    return;
  }

  if (primary_main_frame_data_.has_value()) {
    primary_main_frame_data_->num_data_hosts_registered++;
    for (const url::Origin& reporting_origin : reporting_origins) {
      auto [it, _] =
          primary_main_frame_data_
              ->num_data_hosts_registered_by_reporting_origin.try_emplace(
                  reporting_origin, 0);
      it->second++;
    }
  }

  AttributionDataHostManager* manager = suitable_context->data_host_manager();
  manager->RegisterDataHost(std::move(data_host), *std::move(suitable_context),
                            registration_eligibility,
                            is_for_background_requests);
}

void AttributionHost::NotifyNavigationWithBackgroundRegistrationsWillStart(
    const blink::AttributionSrcToken& attribution_src_token,
    uint32_t expected_registrations) {
  auto suitable_context = AttributionSuitableContext::Create(
      static_cast<RenderFrameHostImpl*>(receivers_.GetCurrentTargetFrame()));
  if (!suitable_context.has_value()) {
    return;
  }

  if (!suitable_context->data_host_manager()
           ->NotifyNavigationWithBackgroundRegistrationsWillStart(
               attribution_src_token, expected_registrations)) {
    mojo::ReportBadMessage(
        "Renderer attempted to notify of expected registrations with a "
        "duplicate AttributionSrcToken or an invalid number of expected "
        "registrations.");
    return;
  }
}

void AttributionHost::RegisterNavigationDataHost(
    mojo::PendingReceiver<attribution_reporting::mojom::DataHost> data_host,
    const blink::AttributionSrcToken& attribution_src_token) {
  auto suitable_context = AttributionSuitableContext::Create(
      static_cast<RenderFrameHostImpl*>(receivers_.GetCurrentTargetFrame()));
  if (!suitable_context.has_value()) {
    return;
  }

  if (!suitable_context->data_host_manager()->RegisterNavigationDataHost(
          std::move(data_host), attribution_src_token)) {
    mojo::ReportBadMessage(
        "Renderer attempted to register a data host with a duplicate "
        "AttributionSrcToken.");
    return;
  }
}

void AttributionHost::MaybeLogClientBounce(
    NavigationHandle* navigation_handle) const {
  if (!primary_main_frame_data_.has_value()) {
    return;
  }

  // Note that `NavigationHandle::HasUserGesture()` does not capture
  // browser-initiated navigations. The negation of
  // `NavigationHandle::IsRendererInitiated()` tells us whether the navigation
  // is browser-initiated.
  //
  // A user gesture indicates no client-redirect.
  if (navigation_handle->HasUserGesture() ||
      !navigation_handle->IsRendererInitiated()) {
    return;
  }

  int num_data_hosts_registered =
      primary_main_frame_data_->num_data_hosts_registered;
  if (num_data_hosts_registered == 0) {
    return;
  }

  static constexpr std::string_view kUserActivationStr = "UserActivation";
  static constexpr std::string_view kUserInteractionStr = "UserInteraction";

  static constexpr std::string_view k1sStr = "1s";
  static constexpr std::string_view k5sStr = "5s";
  static constexpr std::string_view k10sStr = "10s";

  // We don't consider a client-redirect to be a bounce if there was user
  // activation/interaction on the page or if we timed out on the client bounce
  // detection timers.
  CHECK(last_navigation_time_.has_value());
  base::TimeDelta time_since_last_navigation =
      base::Time::Now() - *last_navigation_time_;

  int64_t num_data_hosts_registered_bucket =
      ukm::GetExponentialBucketMinForCounts1000(num_data_hosts_registered);

  ukm::builders::Conversions_ClientBounce ukm_builder(GetPageUkmSourceId());

  if (!primary_main_frame_data_->has_user_activation) {
    if (time_since_last_navigation < base::Seconds(1)) {
      ClientBounceHistogram(kUserActivationStr, k1sStr,
                            num_data_hosts_registered);
      ukm_builder.SetUserActivation_1s(num_data_hosts_registered_bucket);
    }
    if (time_since_last_navigation < base::Seconds(5)) {
      ClientBounceHistogram(kUserActivationStr, k5sStr,
                            num_data_hosts_registered);
      ukm_builder.SetUserActivation_5s(num_data_hosts_registered_bucket);
    }
    if (time_since_last_navigation < base::Seconds(10)) {
      ClientBounceHistogram(kUserActivationStr, k10sStr,
                            num_data_hosts_registered);
      ukm_builder.SetUserActivation_10s(num_data_hosts_registered_bucket);
    }
  }

  if (!primary_main_frame_data_->has_user_interaction) {
    if (time_since_last_navigation < base::Seconds(1)) {
      ClientBounceHistogram(kUserInteractionStr, k1sStr,
                            num_data_hosts_registered);
      ukm_builder.SetUserInteraction_1s(num_data_hosts_registered_bucket);
    }
    if (time_since_last_navigation < base::Seconds(5)) {
      ClientBounceHistogram(kUserInteractionStr, k5sStr,
                            num_data_hosts_registered);
      ukm_builder.SetUserInteraction_5s(num_data_hosts_registered_bucket);
    }
    if (time_since_last_navigation < base::Seconds(10)) {
      ClientBounceHistogram(kUserInteractionStr, k10sStr,
                            num_data_hosts_registered);
      ukm_builder.SetUserInteraction_10s(num_data_hosts_registered_bucket);
    }
  }

  ukm_builder.Record(ukm::UkmRecorder::Get());

  for (const auto& [reporting_origin,
                    num_data_hosts_registered_for_reporting_origin] :
       primary_main_frame_data_
           ->num_data_hosts_registered_by_reporting_origin) {
    const int64_t num_data_hosts_registered_for_reporting_origin_bucket =
        ukm::GetExponentialBucketMinForCounts1000(
            num_data_hosts_registered_for_reporting_origin);

    dwa::builders::AttributionConversionsClientBounce dwa_builder;

    if (!primary_main_frame_data_->has_user_activation) {
      if (time_since_last_navigation < base::Seconds(1)) {
        dwa_builder.SetUserActivation_1s(
            num_data_hosts_registered_for_reporting_origin_bucket);
      }
      if (time_since_last_navigation < base::Seconds(5)) {
        dwa_builder.SetUserActivation_5s(
            num_data_hosts_registered_for_reporting_origin_bucket);
      }
      if (time_since_last_navigation < base::Seconds(10)) {
        dwa_builder.SetUserActivation_10s(
            num_data_hosts_registered_for_reporting_origin_bucket);
      }
    }

    if (!primary_main_frame_data_->has_user_interaction) {
      if (time_since_last_navigation < base::Seconds(1)) {
        dwa_builder.SetUserInteraction_1s(
            num_data_hosts_registered_for_reporting_origin_bucket);
      }
      if (time_since_last_navigation < base::Seconds(5)) {
        dwa_builder.SetUserInteraction_5s(
            num_data_hosts_registered_for_reporting_origin_bucket);
      }
      if (time_since_last_navigation < base::Seconds(10)) {
        dwa_builder.SetUserInteraction_10s(
            num_data_hosts_registered_for_reporting_origin_bucket);
      }
    }

    dwa_builder.SetContent(reporting_origin.Serialize())
        .Record(metrics::dwa::DwaRecorder::Get());
  }
}

// static
void AttributionHost::BindReceiver(
    mojo::PendingAssociatedReceiver<blink::mojom::AttributionHost> receiver,
    RenderFrameHost* rfh) {
  auto* web_contents = WebContents::FromRenderFrameHost(rfh);
  if (!web_contents) {
    return;
  }
  auto* attribution_host = AttributionHost::FromWebContents(web_contents);
  if (!attribution_host) {
    return;
  }
  attribution_host->receivers_.Bind(rfh, std::move(receiver));
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(AttributionHost);

}  // namespace content
