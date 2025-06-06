// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/tpcd_heuristics/opener_heuristic_tab_helper.h"

#include <cstdint>
#include <utility>
#include <variant>

#include "base/functional/bind.h"
#include "base/functional/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "base/rand_util.h"
#include "base/time/clock.h"
#include "base/time/default_clock.h"
#include "base/time/time.h"
#include "components/content_settings/core/common/features.h"
#include "content/browser/btm/btm_bounce_detector.h"
#include "content/browser/btm/btm_service_impl.h"
#include "content/browser/tpcd_heuristics/opener_heuristic_metrics.h"
#include "content/common/features.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/common/content_client.h"
#include "net/base/schemeful_site.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/metrics/public/cpp/ukm_recorder.h"
#include "services/metrics/public/cpp/ukm_source_id.h"

using content_settings::features::EnableForIframeTypes;

namespace content {

namespace {

// We don't need to protect this with a lock since it's only set while
// single-threaded in tests.
base::Clock* g_clock = nullptr;

base::Clock* GetClock() {
  return g_clock ? g_clock : base::DefaultClock::GetInstance();
}

}  // namespace

OpenerHeuristicTabHelper::OpenerHeuristicTabHelper(WebContents* web_contents)
    : WebContentsObserver(web_contents),
      WebContentsUserData<OpenerHeuristicTabHelper>(*web_contents) {}

OpenerHeuristicTabHelper::~OpenerHeuristicTabHelper() = default;

/* static */
base::Clock* OpenerHeuristicTabHelper::SetClockForTesting(base::Clock* clock) {
  return std::exchange(g_clock, clock);
}

void OpenerHeuristicTabHelper::InitPopup(
    const GURL& popup_url,
    base::WeakPtr<OpenerHeuristicTabHelper> opener) {
  if (web_contents()->IsPartitionedPopin()) {
    // Skip heuristics if this is a popin as we should treat this context as as
    // embedded and not its own window for storage access purposes.
    // See https://explainers-by-googlers.github.io/partitioned-popins/
    return;
  }

  popup_observer_ =
      std::make_unique<PopupObserver>(web_contents(), popup_url, opener);

  BtmServiceImpl* btm_service =
      BtmServiceImpl::Get(web_contents()->GetBrowserContext());
  if (!btm_service) {
    // If BTM is disabled, we can't look up past interaction.
    // TODO(rtarpine): consider falling back to SiteEngagementService.
    return;
  }

  btm_service->storage()
      ->AsyncCall(&BtmStorage::Read)
      .WithArgs(popup_url)
      .Then(base::BindOnce(&OpenerHeuristicTabHelper::GotPopupDipsState,
                           weak_factory_.GetWeakPtr()));
}

void OpenerHeuristicTabHelper::GotPopupDipsState(const BtmState& state) {
  popup_observer_->SetPastInteractionTimeAndType(
      state.user_activation_times(), state.web_authn_assertion_times());
}

void OpenerHeuristicTabHelper::PrimaryPageChanged(Page& page) {
  page_id_++;
}

void OpenerHeuristicTabHelper::DidOpenRequestedURL(
    WebContents* new_contents,
    RenderFrameHost* source_render_frame_host,
    const GURL& url,
    const Referrer& referrer,
    WindowOpenDisposition disposition,
    ui::PageTransition transition,
    bool started_from_context_menu,
    bool renderer_initiated) {
  if (source_render_frame_host->GetMainFrame() !=
      web_contents()->GetPrimaryMainFrame()) {
    // Not sure exactly when this happens, but it seems to involve devtools.
    // Cf. crbug.com/1448789
    return;
  }

  if (!PassesIframeInitiatorCheck(source_render_frame_host)) {
    return;
  }

  if (!new_contents->HasOpener()) {
    // Ignore if popup doesn't have opener access.
    return;
  }

  // Create an OpenerHeuristicTabHelper for the popup.
  //
  // Note: TabHelpers::AttachTabHelpers() creates OpenerHeuristicTabHelper, but
  // on Android that can happen after DidOpenRequestedURL() is called (on other
  // platforms it seems to happen first). So create it now if it doesn't already
  // exist.
  OpenerHeuristicTabHelper::CreateForWebContents(new_contents);
  OpenerHeuristicTabHelper::FromWebContents(new_contents)
      ->InitPopup(url, weak_factory_.GetWeakPtr());
}

bool OpenerHeuristicTabHelper::PassesIframeInitiatorCheck(
    RenderFrameHost* source_render_frame_host) {
  if (source_render_frame_host->IsInPrimaryMainFrame()) {
    return true;
  }

  switch (
      content_settings::features::kTpcdPopupHeuristicEnableForIframeInitiator
          .Get()) {
    case EnableForIframeTypes::kNone:
      return false;
    case EnableForIframeTypes::kAll:
      return true;
    case EnableForIframeTypes::kFirstParty: {
      // Check that the frame tree consists of only first-party iframes.
      std::string main_frame_site = GetSiteForBtm(
          source_render_frame_host->GetMainFrame()->GetLastCommittedURL());
      RenderFrameHost* rfh_itr = source_render_frame_host;
      while (rfh_itr->GetParent() != nullptr) {
        if (GetSiteForBtm(rfh_itr->GetLastCommittedURL()) != main_frame_site) {
          return false;
        }
        rfh_itr = rfh_itr->GetParent();
      }
      return true;
    }
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(OpenerHeuristicTabHelper);

OpenerHeuristicTabHelper::PopupObserver::PopupObserver(
    WebContents* web_contents,
    const GURL& initial_url,
    base::WeakPtr<OpenerHeuristicTabHelper> opener)
    : WebContentsObserver(web_contents),
      popup_id_(static_cast<int32_t>(base::RandUint64())),
      initial_url_(initial_url),
      opener_(opener),
      opener_page_id_(opener->page_id()),
      opener_source_id_(
          opener->web_contents()->GetPrimaryMainFrame()->GetPageUkmSourceId()),
      opener_origin_(opener->web_contents()
                         ->GetPrimaryMainFrame()
                         ->GetLastCommittedOrigin()) {}

OpenerHeuristicTabHelper::PopupObserver::~PopupObserver() = default;

void OpenerHeuristicTabHelper::PopupObserver::SetPastInteractionTimeAndType(
    TimestampRange user_activation_times,
    TimestampRange web_authn_assertion_times) {
  CHECK(std::holds_alternative<FieldNotSet>(time_since_interaction_))
      << "SetPastInteractionTime() called more than once";

  base::Time most_recent_user_activation =
      user_activation_times ? user_activation_times.value().second
                            : base::Time::Min();
  base::Time most_recent_authentication =
      web_authn_assertion_times ? web_authn_assertion_times.value().second
                                : base::Time::Min();

  base::Time most_recent_interaction;
  if (most_recent_user_activation >= most_recent_authentication) {
    most_recent_interaction = most_recent_user_activation;
    past_interaction_type_ = BtmInteractionType::UserActivation;
  } else {
    most_recent_interaction = most_recent_authentication;
    past_interaction_type_ = BtmInteractionType::UserActivation;
  }

  if (most_recent_interaction != base::Time::Min()) {
    // Technically we should use the time when the pop-up first opened. But
    // since we only report this metric at hourly granularity, it shouldn't
    // matter.
    time_since_interaction_ = GetClock()->Now() - most_recent_interaction;
  } else {
    time_since_interaction_ = NoInteraction();
    past_interaction_type_ = BtmInteractionType::NoInteraction;
  }

  // TODO(rtarpine): consider ignoring interactions that are too old. (This
  // shouldn't happen since BTM already discards old timestamps.)

  EmitPastInteractionIfReady();
}

void OpenerHeuristicTabHelper::PopupObserver::EmitPastInteractionIfReady() {
  if (std::holds_alternative<FieldNotSet>(time_since_interaction_) ||
      !initial_source_id_.has_value()) {
    // Not enough information to emit event yet.
    return;
  }

  auto has_iframe = GetOpenerHasSameSiteIframe(initial_url_);
  int32_t bucketized_time = -1;
  if (auto* time = std::get_if<base::TimeDelta>(&time_since_interaction_)) {
    bucketized_time =
        Bucketize3PCDHeuristicSample(time->InHours(), base::Days(30).InHours());
  }

  // Record past interaction in UKM.
  ukm::builders::OpenerHeuristic_PopupPastInteraction(
      initial_source_id_.value())
      .SetHoursSinceLastInteraction(bucketized_time)
      .SetOpenerHasSameSiteIframe(static_cast<int64_t>(has_iframe))
      .SetPopupId(popup_id_)
      .SetInteractionType(static_cast<int32_t>(past_interaction_type_))
      .Record(ukm::UkmRecorder::Get());

  EmitTopLevelAndCreateGrant(
      initial_url_, has_iframe, /*is_current_interaction=*/false,
      /*interaction_type=*/BtmInteractionType::UserActivation,
      /*should_record_popup_and_maybe_grant=*/
      std::holds_alternative<base::TimeDelta>(time_since_interaction_),
      /*grant_duration=*/
      content_settings::features::kTpcdWritePopupPastInteractionHeuristicsGrants
          .Get());
}

void OpenerHeuristicTabHelper::PopupObserver::DidFinishNavigation(
    NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->HasCommitted() ||
      navigation_handle->IsSameDocument()) {
    return;
  }

  url_index_ += navigation_handle->GetRedirectChain().size();

  // This is only called on the first committed navigation in the new popup.
  // Only get the source id, time, and ad-tagged status for the first commit.
  // Ignore the rest.
  if (!initial_source_id_.has_value()) {
    commit_time_ = GetClock()->Now();

    if (navigation_handle->GetRedirectChain().size() > 1) {
      // Get a source id for the URL the popup was originally opened with,
      // even though the user was redirected elsewhere.
      initial_source_id_ = btm::GetRedirectSourceId(navigation_handle, 0);
    } else {
      // No redirect happened, get the source id for the committed page.
      initial_source_id_ = navigation_handle->GetNextPageUkmSourceId();
    }

    is_last_navigation_ad_tagged_ =
        navigation_handle->GetNavigationInitiatorActivationAndAdStatus() ==
        blink::mojom::NavigationInitiatorActivationAndAdStatus::
            kStartedWithTransientActivationFromAd;

    EmitPastInteractionIfReady();
  }
}

void OpenerHeuristicTabHelper::PopupObserver::FrameReceivedUserActivation(
    RenderFrameHost* render_frame_host) {
  RecordInteractionAndCreateGrant(render_frame_host,
                                  BtmInteractionType::UserActivation);
}

void OpenerHeuristicTabHelper::PopupObserver::WebAuthnAssertionRequestSucceeded(
    RenderFrameHost* render_frame_host) {
  RecordInteractionAndCreateGrant(render_frame_host,
                                  BtmInteractionType::Authentication);
}

void OpenerHeuristicTabHelper::PopupObserver::RecordInteractionAndCreateGrant(
    RenderFrameHost* render_frame_host,
    BtmInteractionType interaction_type) {
  if (!render_frame_host->IsInPrimaryMainFrame()) {
    return;
  }

  if (interaction_reported_) {
    // Only report the first interaction.
    return;
  }

  if (!commit_time_.has_value()) {
    // Not sure if this can happen. What happens if the user clicks before the
    // popup loads its initial URL?
    return;
  }

  const GURL& interaction_url = render_frame_host->GetLastCommittedURL();
  auto time_since_committed = GetClock()->Now() - *commit_time_;
  auto has_iframe = GetOpenerHasSameSiteIframe(interaction_url);

  ukm::builders::OpenerHeuristic_PopupInteraction(
      render_frame_host->GetPageUkmSourceId())
      .SetSecondsSinceCommitted(Bucketize3PCDHeuristicSample(
          time_since_committed.InSeconds(), base::Minutes(3).InSeconds()))
      .SetUrlIndex(url_index_)
      .SetOpenerHasSameSiteIframe(static_cast<int64_t>(has_iframe))
      .SetPopupId(popup_id_)
      .SetInteractionType(static_cast<int32_t>(interaction_type))
      .Record(ukm::UkmRecorder::Get());

  interaction_reported_ = true;

  EmitTopLevelAndCreateGrant(
      interaction_url, has_iframe,
      /*is_current_interaction=*/true, interaction_type,
      /*should_record_popup_and_maybe_grant=*/true,
      /*grant_duration=*/
      content_settings::features::
          kTpcdWritePopupCurrentInteractionHeuristicsGrants.Get());
}

void OpenerHeuristicTabHelper::OnCookiesAccessed(
    RenderFrameHost* render_frame_host,
    const CookieAccessDetails& details) {
  if (!render_frame_host->IsInLifecycleState(
          RenderFrameHost::LifecycleState::kPrerendering)) {
    OnCookiesAccessed(render_frame_host->GetPageUkmSourceId(), details);
  }
}

void OpenerHeuristicTabHelper::OnCookiesAccessed(
    NavigationHandle* navigation_handle,
    const CookieAccessDetails& details) {
  OnCookiesAccessed(navigation_handle->GetNextPageUkmSourceId(), details);
}

void OpenerHeuristicTabHelper::OnCookiesAccessed(
    const ukm::SourceId& source_id,
    const CookieAccessDetails& details) {
  BtmServiceImpl* btm_service =
      BtmServiceImpl::Get(web_contents()->GetBrowserContext());
  if (!btm_service) {
    // If BTM is disabled, we can't look up past popup events.
    // TODO(rtarpine): consider falling back to SiteEngagementService.
    return;
  }

  // Ignore same-domain cookie access.
  if (details.first_party_url.is_empty() ||
      GetSiteForBtm(details.first_party_url) == GetSiteForBtm(details.url)) {
    return;
  }

  btm_service->storage()
      ->AsyncCall(&BtmStorage::ReadPopup)
      .WithArgs(GetSiteForBtm(details.first_party_url),
                GetSiteForBtm(details.url))
      .Then(base::BindOnce(&OpenerHeuristicTabHelper::EmitPostPopupCookieAccess,
                           weak_factory_.GetWeakPtr(), source_id, details));
}

void OpenerHeuristicTabHelper::EmitPostPopupCookieAccess(
    const ukm::SourceId& source_id,
    const CookieAccessDetails& details,
    std::optional<PopupsStateValue> value) {
  if (!value.has_value()) {
    return;
  }
  int32_t hours_since_opener = Bucketize3PCDHeuristicSample(
      (GetClock()->Now() - value->last_popup_time).InHours(),
      base::Days(30).InHours());
  OptionalBool is_ad_tagged_cookie = IsAdTaggedCookieForHeuristics(details);

  ukm::builders::OpenerHeuristic_PostPopupCookieAccess(source_id)
      .SetAccessId(value->access_id)
      .SetAccessSucceeded(!details.blocked_by_policy)
      .SetIsAdTagged(static_cast<int64_t>(is_ad_tagged_cookie))
      .SetHoursSincePopupOpened(hours_since_opener)
      .Record(ukm::UkmRecorder::Get());
}

void OpenerHeuristicTabHelper::PopupObserver::EmitTopLevelAndCreateGrant(
    const GURL& popup_url,
    OptionalBool has_iframe,
    bool is_current_interaction,
    BtmInteractionType interaction_type,
    bool should_record_popup_and_maybe_grant,
    base::TimeDelta grant_duration) {
  uint64_t access_id = base::RandUint64();

  if (should_record_popup_and_maybe_grant) {
    if (BtmServiceImpl* btm_service =
            BtmServiceImpl::Get(web_contents()->GetBrowserContext())) {
      btm_service->storage()
          ->AsyncCall(&BtmStorage::WritePopup)
          .WithArgs(GetSiteForBtm(opener_origin_), GetSiteForBtm(popup_url),
                    access_id,
                    /*popup_time=*/GetClock()->Now(), is_current_interaction,
                    /*is_authentication_interaction=*/interaction_type ==
                        BtmInteractionType::Authentication)
          .Then(base::BindOnce([](bool succeeded) { DCHECK(succeeded); }));
    }

    MaybeCreateOpenerHeuristicGrant(popup_url, grant_duration);
  }

  // Don't record multiple interaction UKM events for the same top level.
  if (!toplevel_reported_) {
    ukm::builders::OpenerHeuristic_TopLevel(opener_source_id_)
        .SetAccessId(access_id)
        .SetHasSameSiteIframe(static_cast<int64_t>(has_iframe))
        .SetPopupProvider(static_cast<int64_t>(GetPopupProvider(initial_url_)))
        .SetPopupId(popup_id_)
        .SetIsAdTaggedPopupClick(is_last_navigation_ad_tagged_)
        .Record(ukm::UkmRecorder::Get());
  }

  toplevel_reported_ = true;
}

void OpenerHeuristicTabHelper::PopupObserver::MaybeCreateOpenerHeuristicGrant(
    const GURL& url,
    base::TimeDelta grant_duration) {
  if (!base::FeatureList::IsEnabled(
          content_settings::features::kTpcdHeuristicsGrants) ||
      !grant_duration.is_positive()) {
    return;
  }

  if (is_last_navigation_ad_tagged_ &&
      content_settings::features::kTpcdPopupHeuristicDisableForAdTaggedPopups
          .Get()) {
    return;
  }

  GetContentClient()->browser()->GrantCookieAccessDueToHeuristic(
      web_contents()->GetBrowserContext(), net::SchemefulSite(opener_origin_),
      net::SchemefulSite(url::Origin::Create(url)), grant_duration,
      /*ignore_schemes=*/false);
}

OptionalBool
OpenerHeuristicTabHelper::PopupObserver::GetOpenerHasSameSiteIframe(
    const GURL& popup_url) {
  if (opener_ && opener_->page_id() == opener_page_id_) {
    return ToOptionalBool(
        HasSameSiteIframe(opener_->web_contents(), popup_url));
  }

  return OptionalBool::kUnknown;
}

}  // namespace content
