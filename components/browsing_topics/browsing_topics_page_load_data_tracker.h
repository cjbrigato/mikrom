// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_BROWSING_TOPICS_BROWSING_TOPICS_PAGE_LOAD_DATA_TRACKER_H_
#define COMPONENTS_BROWSING_TOPICS_BROWSING_TOPICS_PAGE_LOAD_DATA_TRACKER_H_

#include "base/containers/flat_set.h"
#include "components/browsing_topics/common/common_types.h"
#include "content/public/browser/page_user_data.h"

namespace history {
class HistoryService;
}  // namespace history

namespace browsing_topics {

// Tracks several page-level (i.e. primary main frame document) Topics signals,
// such as whether the page is eligible to be included in topics calculation
// (i.e. to "observe" topics), the context domains that have requested to
// observe topics, the client side redirect chain's status, etc.
class BrowsingTopicsPageLoadDataTracker
    : public content::PageUserData<BrowsingTopicsPageLoadDataTracker> {
 public:
  BrowsingTopicsPageLoadDataTracker(const BrowsingTopicsPageLoadDataTracker&) =
      delete;
  BrowsingTopicsPageLoadDataTracker& operator=(
      const BrowsingTopicsPageLoadDataTracker&) = delete;

  ~BrowsingTopicsPageLoadDataTracker() override;

  // Called when the document.browsingTopics() API is used in the page.
  void OnBrowsingTopicsApiUsed(const HashedDomain& hashed_context_domain,
                               const std::string& context_domain,
                               history::HistoryService* history_service,
                               bool observe);

  const std::set<HashedHost>& redirect_hosts_with_topics_invoked() const {
    return redirect_hosts_with_topics_invoked_;
  }

  HashedHost hashed_main_frame_host() const { return hashed_main_frame_host_; }

  ukm::SourceId source_id_before_redirects() const {
    return source_id_before_redirects_;
  }

  bool topics_invoked() const { return topics_invoked_; }

 private:
  friend class PageUserData;

  explicit BrowsingTopicsPageLoadDataTracker(content::Page& page);

  BrowsingTopicsPageLoadDataTracker(
      content::Page& page,
      std::set<HashedHost> redirect_hosts_with_topics_invoked,
      ukm::SourceId source_id_before_redirects);

  // Whether this page is eligible to observe topics (i.e. IP was publicly
  // routable AND permissions policy is "allow").
  bool eligible_to_observe_ = false;

  HashedHost hashed_main_frame_host_;

  // Main frame hosts from the redirect chain that invoked the Topics API.
  // Initialized with the *previous* redirect chain's hosts, and may be
  // expanded to include the current host when Topics is called. The size is
  // limited to 5 to prevent unbounded growth.
  std::set<HashedHost> redirect_hosts_with_topics_invoked_;

  ukm::SourceId source_id_;

  base::flat_set<HashedDomain> observed_hashed_context_domains_;

  // The UKM source ID of the page before the client-side redirects.
  ukm::SourceId source_id_before_redirects_;

  bool topics_invoked_ = false;

  PAGE_USER_DATA_KEY_DECL();
};

}  // namespace browsing_topics

#endif  // COMPONENTS_BROWSING_TOPICS_BROWSING_TOPICS_PAGE_LOAD_DATA_TRACKER_H_
