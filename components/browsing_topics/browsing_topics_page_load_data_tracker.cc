// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/browsing_topics/browsing_topics_page_load_data_tracker.h"

#include "base/containers/contains.h"
#include "base/metrics/histogram_functions.h"
#include "components/browsing_topics/util.h"
#include "components/history/content/browser/history_context_helper.h"
#include "components/history/core/browser/history_service.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browsing_topics_site_data_manager.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "services/metrics/public/cpp/metrics_utils.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/metrics/public/cpp/ukm_recorder.h"
#include "services/network/public/mojom/permissions_policy/permissions_policy_feature.mojom.h"
#include "third_party/blink/public/common/features.h"

namespace browsing_topics {

// Max number of context domains to keep track of per page load to limit in-use
// memory. This should also be greater than
// `kBrowsingTopicsMaxNumberOfApiUsageContextDomainsToStorePerPageLoad`.
constexpr int kMaxNumberOfContextDomainsToMonitor = 1000;

BrowsingTopicsPageLoadDataTracker::~BrowsingTopicsPageLoadDataTracker() {
  if (observed_hashed_context_domains_.empty())
    return;

  // TODO(yaoxia): consider also recording metrics when the Chrome app goes into
  // background, in which case the destructor may never be called.
  ukm::UkmRecorder* ukm_recorder = ukm::UkmRecorder::Get();
  ukm::builders::BrowsingTopics_PageLoad builder(source_id_);

  builder.SetTopicsRequestingContextDomainsCount(
      ukm::GetExponentialBucketMinForCounts1000(
          observed_hashed_context_domains_.size()));

  builder.Record(ukm_recorder->Get());
}

BrowsingTopicsPageLoadDataTracker::BrowsingTopicsPageLoadDataTracker(
    content::Page& page)
    : BrowsingTopicsPageLoadDataTracker(
          page,
          /*redirect_hosts_with_topics_invoked=*/{},
          /*source_id_before_redirects=*/
          page.GetMainDocument().GetPageUkmSourceId()) {}

BrowsingTopicsPageLoadDataTracker::BrowsingTopicsPageLoadDataTracker(
    content::Page& page,
    std::set<HashedHost> redirect_hosts_with_topics_invoked,
    ukm::SourceId source_id_before_redirects)
    : content::PageUserData<BrowsingTopicsPageLoadDataTracker>(page),
      hashed_main_frame_host_(HashMainFrameHostForStorage(
          page.GetMainDocument().GetLastCommittedOrigin().host())),
      redirect_hosts_with_topics_invoked_(
          std::move(redirect_hosts_with_topics_invoked)),
      source_id_before_redirects_(source_id_before_redirects) {
  source_id_ = page.GetMainDocument().GetPageUkmSourceId();

  // TODO(yaoxia): consider dropping the permissions policy checks. We require
  // that the API is used in the page, and that already implies that the
  // permissions policy is allowed.

  if ((page.GetMainDocument().IsLastCommitIPAddressPubliclyRoutable() ||
       base::FeatureList::IsEnabled(
           blink::features::kBrowsingTopicsBypassIPIsPubliclyRoutableCheck)) &&
      page.GetMainDocument().IsFeatureEnabled(
          network::mojom::PermissionsPolicyFeature::kBrowsingTopics) &&
      page.GetMainDocument().IsFeatureEnabled(
          network::mojom::PermissionsPolicyFeature::
              kBrowsingTopicsBackwardCompatible) &&
      page.GetMainDocument().IsLastCrossDocumentNavigationStartedByUser()) {
    eligible_to_observe_ = true;
  }
}

void BrowsingTopicsPageLoadDataTracker::OnBrowsingTopicsApiUsed(
    const HashedDomain& hashed_context_domain,
    const std::string& context_domain,
    history::HistoryService* history_service,
    bool observe) {
  CHECK(page().IsPrimary());

  if (!topics_invoked_) {
    if (redirect_hosts_with_topics_invoked_.size() < 5) {
      bool host_inserted =
          redirect_hosts_with_topics_invoked_.insert(hashed_main_frame_host_)
              .second;

      if (host_inserted) {
        // If this is the first Topics call on the page, and this site wasn't
        // part of a previous redirect chain that invoked Topics, record the
        // number of distinct sites in the current redirect chain (including
        // this page) that have called Topics, capped at 5. This ensures each
        // count (from 1 to the maximum count) is emitted exactly once per
        // redirect chain.
        int kExclusiveMaxBucket = 6;
        base::UmaHistogramExactLinear(
            "BrowsingTopics.RedirectChain.OnTopicsFirstInvokedForSite."
            "TopicsCallingSitesCount",
            redirect_hosts_with_topics_invoked_.size(), kExclusiveMaxBucket);
      }
    }

    topics_invoked_ = true;
  }

  if (!observe || !eligible_to_observe_) {
    return;
  }

  // On the first Topics observation in the page, set the allowed bit in
  // history.
  if (observed_hashed_context_domains_.empty()) {
    content::WebContents* web_contents =
        content::WebContents::FromRenderFrameHost(&page().GetMainDocument());

    history_service->SetBrowsingTopicsAllowed(
        history::ContextIDForWebContents(web_contents),
        web_contents->GetController().GetLastCommittedEntry()->GetUniqueID(),
        web_contents->GetLastCommittedURL());
  }

  CHECK_LE(
      blink::features::
          kBrowsingTopicsMaxNumberOfApiUsageContextDomainsToStorePerPageLoad
              .Get(),
      kMaxNumberOfContextDomainsToMonitor);

  // Cap the number of context domains to keep track of per page load to limit
  // in-use memory.
  if (observed_hashed_context_domains_.size() >=
      kMaxNumberOfContextDomainsToMonitor) {
    return;
  }

  bool is_new_domain =
      observed_hashed_context_domains_.insert(hashed_context_domain).second;

  // Ignore this context if we've already added it.
  if (!is_new_domain)
    return;

  // Cap the number of context domains to store to disk per page load to limit
  // disk memory usage.
  if (observed_hashed_context_domains_.size() >
      static_cast<size_t>(
          blink::features::
              kBrowsingTopicsMaxNumberOfApiUsageContextDomainsToStorePerPageLoad
                  .Get())) {
    return;
  }

  // Persist the usage now rather than at the end of the page load, as when the
  // app enters background, it may be killed without further notification.
  page()
      .GetMainDocument()
      .GetBrowserContext()
      ->GetDefaultStoragePartition()
      ->GetBrowsingTopicsSiteDataManager()
      ->OnBrowsingTopicsApiUsed(hashed_main_frame_host_, hashed_context_domain,
                                context_domain, base::Time::Now());
}

PAGE_USER_DATA_KEY_IMPL(BrowsingTopicsPageLoadDataTracker);

}  // namespace browsing_topics
