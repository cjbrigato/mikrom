// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/safe_browsing/model/safe_browsing_client_impl.h"

#import "base/check.h"
#import "base/memory/weak_ptr.h"
#import "components/safe_browsing/core/browser/realtime/url_lookup_service.h"
#import "components/security_interstitials/core/unsafe_resource.h"
#import "ios/chrome/browser/enterprise/connectors/connectors_service.h"
#import "ios/chrome/browser/enterprise/connectors/connectors_util.h"
#import "ios/chrome/browser/prerender/model/prerender_service.h"
#import "ios/chrome/browser/shared/model/application_context/application_context.h"
#import "ios/web/public/web_state.h"

SafeBrowsingClientImpl::SafeBrowsingClientImpl(
    PrefService* pref_service,
    safe_browsing::HashRealTimeService* hash_real_time_service,
    PrerenderService* prerender_service,
    UrlLookupServiceFactory url_lookup_service_factory,
    enterprise_connectors::ConnectorsService* connectors_service)
    : pref_service_(pref_service),
      hash_real_time_service_(hash_real_time_service),
      prerender_service_(prerender_service),
      url_lookup_service_factory_(url_lookup_service_factory),
      connectors_service_(connectors_service) {
  CHECK(connectors_service_);
}

SafeBrowsingClientImpl::~SafeBrowsingClientImpl() = default;

base::WeakPtr<SafeBrowsingClient> SafeBrowsingClientImpl::AsWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

PrefService* SafeBrowsingClientImpl::GetPrefs() {
  return pref_service_;
}

SafeBrowsingService* SafeBrowsingClientImpl::GetSafeBrowsingService() {
  return GetApplicationContext()->GetSafeBrowsingService();
}

safe_browsing::RealTimeUrlLookupServiceBase*
SafeBrowsingClientImpl::GetRealTimeUrlLookupService() {
  return url_lookup_service_factory_.Run();
}

safe_browsing::HashRealTimeService*
SafeBrowsingClientImpl::GetHashRealTimeService() {
  return hash_real_time_service_;
}

variations::VariationsService* SafeBrowsingClientImpl::GetVariationsService() {
  return GetApplicationContext()->GetVariationsService();
}

bool SafeBrowsingClientImpl::ShouldBlockUnsafeResource(
    const security_interstitials::UnsafeResource& resource) const {
  // Send do-not-proceed signal if the WebState is for a prerender tab.
  web::WebState* web_state = resource.weak_web_state.get();
  return prerender_service_ &&
         prerender_service_->IsWebStatePrerendered(web_state);
}

bool SafeBrowsingClientImpl::OnMainFrameUrlQueryCancellationDecided(
    web::WebState* web_state,
    const GURL& url) {
  // When a prendered page is unsafe, cancel the prerender.
  if (prerender_service_ &&
      prerender_service_->IsWebStatePrerendered(web_state)) {
    prerender_service_->CancelPrerender();
    return false;
  }

  return true;
}

bool SafeBrowsingClientImpl::ShouldForceSyncRealTimeUrlChecks() const {
  return connectors_service_ &&
         enterprise_connectors::IsEnterpriseUrlFilteringEnabled(
             connectors_service_->GetAppliedRealTimeUrlCheck());
}
