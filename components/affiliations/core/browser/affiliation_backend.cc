// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/affiliations/core/browser/affiliation_backend.h"

#include <stdint.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/metrics/histogram_macros.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/clock.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "components/affiliations/core/browser/affiliation_database.h"
#include "components/affiliations/core/browser/affiliation_fetch_throttler.h"
#include "components/affiliations/core/browser/affiliation_fetcher_factory_impl.h"
#include "components/affiliations/core/browser/affiliation_fetcher_interface.h"
#include "components/affiliations/core/browser/affiliation_utils.h"
#include "components/affiliations/core/browser/facet_manager.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace affiliations {

BASE_FEATURE(kFetchChangePasswordUrl,
             "FetchChangePasswordUrl",
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
             // Change-password urls aren't utilized in any way on mobile. No
             // need to fetch them.
             base::FEATURE_DISABLED_BY_DEFAULT);
#else
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif

AffiliationBackend::AffiliationBackend(
    const scoped_refptr<base::SequencedTaskRunner>& task_runner,
    base::Clock* time_source,
    const base::TickClock* time_tick_source)
    : task_runner_(task_runner),
      clock_(time_source),
      tick_clock_(time_tick_source),
      construction_time_(clock_->Now()) {
  DCHECK_LT(base::Time(), clock_->Now());
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

AffiliationBackend::~AffiliationBackend() = default;

void AffiliationBackend::Initialize(
    std::unique_ptr<network::PendingSharedURLLoaderFactory>
        pending_url_loader_factory,
    network::NetworkConnectionTracker* network_connection_tracker,
    const base::FilePath& db_path) {
  TRACE_EVENT0("passwords", "AffiliationBackend::Initialize");
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!throttler_);
  throttler_ = std::make_unique<AffiliationFetchThrottler>(
      this, task_runner_, network_connection_tracker, tick_clock_);

  // TODO(engedy): Currently, when Init() returns false, it always poisons the
  // DB, so subsequent operations will silently fail. Consider either fully
  // committing to this approach and making Init() a void, or handling the
  // return value here. See: https://crbug.com/478831.
  cache_ = std::make_unique<AffiliationDatabase>();
  cache_->Init(db_path);
  DCHECK(pending_url_loader_factory);
  DCHECK(!url_loader_factory_);
  url_loader_factory_ = network::SharedURLLoaderFactory::Create(
      std::move(pending_url_loader_factory));
  fetcher_manager_ = std::make_unique<AffiliationFetcherManager>(
      std::move(url_loader_factory_));
}

void AffiliationBackend::GetAffiliationsAndBranding(
    const FacetURI& facet_uri,
    AffiliationService::ResultCallback callback,
    const scoped_refptr<base::TaskRunner>& callback_task_runner) {
  TRACE_EVENT0("passwords", "AffiliationBackend::GetAffiliationsAndBranding");
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  FacetManager* facet_manager = GetOrCreateFacetManager(facet_uri);
  DCHECK(facet_manager);
  facet_manager->GetAffiliationsAndBranding(std::move(callback),
                                            callback_task_runner);

  if (facet_manager->CanBeDiscarded())
    facet_managers_.erase(facet_uri);
}

void AffiliationBackend::Prefetch(const FacetURI& facet_uri,
                                  const base::Time& keep_fresh_until) {
  TRACE_EVENT0("passwords", "AffiliationBackend::Prefetch");
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  FacetManager* facet_manager = GetOrCreateFacetManager(facet_uri);
  DCHECK(facet_manager);
  facet_manager->Prefetch(keep_fresh_until);

  if (facet_manager->CanBeDiscarded())
    facet_managers_.erase(facet_uri);
}

void AffiliationBackend::CancelPrefetch(const FacetURI& facet_uri,
                                        const base::Time& keep_fresh_until) {
  TRACE_EVENT0("passwords", "AffiliationBackend::CancelPrefetch");
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto facet_manager_it = facet_managers_.find(facet_uri);
  if (facet_manager_it == facet_managers_.end())
    return;
  facet_manager_it->second->CancelPrefetch(keep_fresh_until);

  if (facet_manager_it->second->CanBeDiscarded()) {
    facet_managers_.erase(facet_uri);
    TrimCacheForFacetURI(facet_uri);
  }
}

void AffiliationBackend::KeepPrefetchForFacets(
    std::vector<FacetURI> facet_uris) {
  TRACE_EVENT0("passwords", "AffiliationBackend::KeepPrefetchForFacets");
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Firstly, check which facets are missing from the |facet_managers_| and
  // schedule Prefetch() for them.
  for (const auto& facet : facet_uris) {
    if (facet_managers_.find(facet) == facet_managers_.end()) {
      Prefetch(facet, base::Time::Max());
    }
  }

  // Now remove facets which which aren't retained anymore.
  auto retained_facets = base::MakeFlatSet<std::string>(
      facet_uris, {}, &FacetURI::potentially_invalid_spec);

  std::vector<FacetURI> facets_to_remove;
  for (const auto& facet_manager_pair : facet_managers_) {
    if (retained_facets.contains(
            facet_manager_pair.first.potentially_invalid_spec())) {
      continue;
    }

    facet_manager_pair.second->CancelPrefetch(base::Time::Max());

    if (facet_manager_pair.second->CanBeDiscarded())
      facets_to_remove.push_back(facet_manager_pair.first);
  }

  for (const auto& facet : facets_to_remove) {
    facet_managers_.erase(facet);
    TrimCacheForFacetURI(facet);
  }
}

void AffiliationBackend::TrimCacheForFacetURI(const FacetURI& facet_uri) {
  TRACE_EVENT0("passwords", "AffiliationBackend::TrimCacheForFacetURI");
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  AffiliatedFacetsWithUpdateTime affiliation;
  if (cache_->GetAffiliationsAndBrandingForFacetURI(facet_uri, &affiliation))
    DiscardCachedDataIfNoLongerNeeded(affiliation.facets);
}

void AffiliationBackend::TrimUnusedCache(std::vector<FacetURI> facet_uris) {
  TRACE_EVENT0("passwords", "AffiliationBackend::TrimUnusedCache");
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  cache_->RemoveMissingFacetURI(facet_uris);
}

std::vector<GroupedFacets> AffiliationBackend::GetGroupingInfo(
    std::vector<FacetURI> facet_uris) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::unordered_set<std::string> facets_in_response;
  std::vector<GroupedFacets> result;

  for (FacetURI& facet : facet_uris) {
    // If facet is already in response, nothing else to do.
    if (facets_in_response.contains(facet.potentially_invalid_spec())) {
      continue;
    }
    result.push_back(cache_->GetGroup(facet));
    for (const auto& response : result.back().facets) {
      facets_in_response.insert(response.uri.potentially_invalid_spec());
    }
  }
  return result;
}

std::vector<std::string> AffiliationBackend::GetPSLExtensions() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return cache_->GetPSLExtensions();
}

void AffiliationBackend::UpdateAffiliationsAndBranding(
    const std::vector<FacetURI>& facets,
    base::OnceClosure consumer_closure) {
  TRACE_EVENT0("passwords",
               "AffiliationBackend::UpdateAffiliationsAndBranding");
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // If there is no internet connection we can't do anything. Fail immediately.
  if (!throttler_->HasInternetConnection()) {
    std::move(consumer_closure).Run();
    return;
  }

  std::vector<FacetURI> valid_facets;
  std::copy_if(facets.begin(), facets.end(), std::back_inserter(valid_facets),
               [](const FacetURI& facet) { return facet.is_valid(); });
  // Return early if there are no facets to request.
  if (valid_facets.empty()) {
    std::move(consumer_closure).Run();
    return;
  }
  auto result_callback =
      base::BindOnce(
          [](base::WeakPtr<AffiliationBackend> weak_self,
             AffiliationFetcherInterface::FetchResult fetch_result) {
            if (weak_self && fetch_result.IsSuccessful()) {
              weak_self->ProcessSuccessfulFetch(
                  std::move(fetch_result.data.value()));
            }
          },
          weak_ptr_factory_.GetWeakPtr())
          .Then(std::move(consumer_closure));
  fetcher_manager_->Fetch(valid_facets,
                          {.branding_info = true, .psl_extension_list = true},
                          std::move(result_callback));
}

// static
void AffiliationBackend::DeleteCache(const base::FilePath& db_path) {
  AffiliationDatabase::Delete(db_path);
}

FacetManager* AffiliationBackend::GetOrCreateFacetManager(
    const FacetURI& facet_uri) {
  std::unique_ptr<FacetManager>& facet_manager = facet_managers_[facet_uri];
  if (!facet_manager) {
    facet_manager = std::make_unique<FacetManager>(facet_uri, this, clock_);
  }
  return facet_manager.get();
}

void AffiliationBackend::DiscardCachedDataIfNoLongerNeeded(
    const AffiliatedFacets& affiliated_facets) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Discard the equivalence class if there is no facet in the class whose
  // FacetManager claims that it needs to keep the data.
  for (const auto& facet : affiliated_facets) {
    auto facet_manager_it = facet_managers_.find(facet.uri);
    if (facet_manager_it != facet_managers_.end() &&
        !facet_manager_it->second->CanCachedDataBeDiscarded()) {
      return;
    }
  }

  CHECK(!affiliated_facets.empty());
  cache_->DeleteAffiliationsAndBrandingForFacetURI(affiliated_facets[0].uri);
}

void AffiliationBackend::OnSendNotification(const FacetURI& facet_uri) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto facet_manager_it = facet_managers_.find(facet_uri);
  if (facet_manager_it == facet_managers_.end())
    return;
  facet_manager_it->second->NotifyAtRequestedTime();

  if (facet_manager_it->second->CanBeDiscarded())
    facet_managers_.erase(facet_uri);
}

bool AffiliationBackend::ReadAffiliationsAndBrandingFromDatabase(
    const FacetURI& facet_uri,
    AffiliatedFacetsWithUpdateTime* affiliations) {
  return cache_->GetAffiliationsAndBrandingForFacetURI(facet_uri, affiliations);
}

void AffiliationBackend::SignalNeedNetworkRequest() {
  throttler_->SignalNetworkRequestNeeded();
}

void AffiliationBackend::RequestNotificationAtTime(const FacetURI& facet_uri,
                                                   base::Time time) {
  // TODO(engedy): Avoid spamming the task runner; only ever schedule the first
  // callback. crbug.com/437865.
  task_runner_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&AffiliationBackend::OnSendNotification,
                     weak_ptr_factory_.GetWeakPtr(), facet_uri),
      time - clock_->Now());
}

void AffiliationBackend::ProcessSuccessfulFetch(
    AffiliationFetcherInterface::ParsedFetchResponse result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!result.psl_extensions.empty()) {
    cache_->UpdatePslExtensions(result.psl_extensions);
  }

  auto psl_extensions = base::MakeFlatSet<std::string>(result.psl_extensions);
  result.groupings = MergeRelatedGroups(psl_extensions, result.groupings);
  std::map<std::string, const GroupedFacets*> map_facet_to_group;
  for (const GroupedFacets& grouped_facets : result.groupings) {
    for (const Facet& facet : grouped_facets.facets) {
      map_facet_to_group[facet.uri.canonical_spec()] = &grouped_facets;
    }
  }

  for (const AffiliatedFacets& affiliated_facets : result.affiliations) {
    AffiliatedFacetsWithUpdateTime affiliation;
    affiliation.facets = affiliated_facets;
    affiliation.last_update_time = clock_->Now();
    std::vector<AffiliatedFacetsWithUpdateTime> obsoleted_affiliations;
    GroupedFacets group;
    if (map_facet_to_group.count(affiliated_facets[0].uri.canonical_spec())) {
      // Affiliations are subset of group. So |map_facet_to_group| must hold a
      // vector to the whole group.
      group = *map_facet_to_group[affiliated_facets[0].uri.canonical_spec()];
    }
    cache_->StoreAndRemoveConflicting(affiliation, group,
                                      &obsoleted_affiliations);

    // Cached data in contradiction with newly stored data automatically gets
    // removed from the DB, and will be stored into |obsoleted_affiliations|.
    // TODO(engedy): Currently, handling this is entirely meaningless unless in
    // the edge case when the user has credentials for two Android applications
    // which are now being de-associated. But even in that case, nothing will
    // explode and the only symptom will be that credentials for the Android
    // application that is not being fetched right now, if any, will not be
    // filled into affiliated applications until the next fetch. Still, this
    // should be implemented at some point by letting facet managers know if
    // data. See: https://crbug.com/478832.

    for (const auto& facet : affiliated_facets) {
      auto facet_manager_it = facet_managers_.find(facet.uri);
      if (facet_manager_it == facet_managers_.end())
        continue;
      FacetManager* facet_manager = facet_manager_it->second.get();
      facet_manager->UpdateLastFetchTime(affiliation.last_update_time);
      if (facet_manager->CanBeDiscarded())
        facet_managers_.erase(facet.uri);
    }
  }

  // A subsequent fetch may be needed if any additional
  // GetAffiliationsAndBranding() requests came in while the current fetch was
  // in flight.
  for (const auto& facet_manager_pair : facet_managers_) {
    if (facet_manager_pair.second->DoesRequireFetch()) {
      throttler_->SignalNetworkRequestNeeded();
      return;
    }
  }
}

void AffiliationBackend::RetryRequestIfNeeded() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Trigger a retry if a fetch is still needed.
  for (const auto& facet_manager_pair : facet_managers_) {
    if (facet_manager_pair.second->DoesRequireFetch()) {
      throttler_->SignalNetworkRequestNeeded();
      return;
    }
  }
}

void AffiliationBackend::OnFetchFinished(
    AffiliationFetcherInterface::FetchResult fetch_result) {
  throttler_->InformOfNetworkRequestComplete(fetch_result.IsSuccessful(),
                                             fetch_result.http_status_code);
  if (!fetch_result.IsSuccessful()) {
    RetryRequestIfNeeded();
    return;
  }
  ProcessSuccessfulFetch(std::move(fetch_result.data.value()));
}

bool AffiliationBackend::OnCanSendNetworkRequest() {
  std::vector<FacetURI> requested_facet_uris;
  for (const auto& facet_manager_pair : facet_managers_) {
    if (facet_manager_pair.second->DoesRequireFetch())
      requested_facet_uris.push_back(facet_manager_pair.first);
  }

  // In case a request is no longer needed, return false to indicate this.
  if (requested_facet_uris.empty())
    return false;

  if (!fetcher_manager_->IsFetchPossible()) {
    return false;
  }

  // TODO(crbug.com/40858918): There is no need to request psl extension every
  // time, find a better way of caching it.
  ReportStatistics(requested_facet_uris.size());
  fetcher_manager_->Fetch(requested_facet_uris,
                          {.branding_info = true,
                           .change_password_info = base::FeatureList::IsEnabled(
                               kFetchChangePasswordUrl),
                           .psl_extension_list = true},
                          base::BindOnce(&AffiliationBackend::OnFetchFinished,
                                         weak_ptr_factory_.GetWeakPtr()));
  return true;
}

void AffiliationBackend::ReportStatistics(size_t requested_facet_uri_count) {
  UMA_HISTOGRAM_COUNTS_100("PasswordManager.AffiliationBackend.FetchSize",
                           requested_facet_uri_count);

  if (last_request_time_.is_null()) {
    base::TimeDelta delay = clock_->Now() - construction_time_;
    UMA_HISTOGRAM_CUSTOM_TIMES(
        "PasswordManager.AffiliationBackend.FirstFetchDelay", delay,
        base::Seconds(1), base::Days(3), 50);
  } else {
    base::TimeDelta delay = clock_->Now() - last_request_time_;
    UMA_HISTOGRAM_CUSTOM_TIMES(
        "PasswordManager.AffiliationBackend.SubsequentFetchDelay", delay,
        base::Seconds(1), base::Days(3), 50);
  }
  last_request_time_ = clock_->Now();
}

}  // namespace affiliations
