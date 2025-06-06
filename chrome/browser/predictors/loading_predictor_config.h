// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PREDICTORS_LOADING_PREDICTOR_CONFIG_H_
#define CHROME_BROWSER_PREDICTORS_LOADING_PREDICTOR_CONFIG_H_

#include <cstddef>
#include <string>

class Profile;

namespace predictors {

// Returns whether the Loading Predictor is enabled for the given |profile|. If
// true, the predictor can observe page load events, build historical database
// and perform allowed speculative actions based on this database.
bool IsLoadingPredictorEnabled(Profile* profile);

// Returns whether the current |profile| settings allow to perform preresolve
// and preconnect actions. This setting is controlled by the user, so the return
// value shouldn't be cached.
bool IsPreconnectAllowed(Profile* profile);

// Indicates what caused the page load hint.
enum class HintOrigin {
  // Triggered at the start of each navigation.
  NAVIGATION,

  // Used when a preconnect is triggered by an external Android app.
  EXTERNAL,

  // Triggered by omnibox.
  OMNIBOX,

  // Triggered by navigation predictor service.
  NAVIGATION_PREDICTOR,

  // Used when a prerender initiated by Omnibox is unsuccessful, and instead a
  // preconnect is initiated. Preconnect triggered by
  // OMNIBOX_PRERENDER_FALLBACK may be handled differently than preconnects
  // triggered by OMNIBOX since the former are triggered at higher confidence.
  OMNIBOX_PRERENDER_FALLBACK,

  // Triggered by optimization guide.
  OPTIMIZATION_GUIDE,

  // Triggered by bookmark bar.
  BOOKMARK_BAR,

  // Triggered by New Tab Page.
  NEW_TAB_PAGE,
};

// Gets the string that can be used to record histograms for the hint origin.
//
// Keep in sync with LoadingPredictorHintOrigin in histograms.xml. Will DCHECK
// if an origin is added that is not listed in histograms.xml.
std::string GetStringNameForHintOrigin(HintOrigin hint_origin);

// Represents the config for the Loading predictor.
struct LoadingPredictorConfig {
  // Initializes the config with default values.
  LoadingPredictorConfig();
  LoadingPredictorConfig(const LoadingPredictorConfig& other);
  ~LoadingPredictorConfig();

  // If a navigation hasn't seen a load complete event in this much time, it
  // is considered abandoned.
  size_t max_navigation_lifetime_seconds;

  // Size of LRU caches for the host data.
  size_t max_hosts_to_track;

  // Size of LRU caches for the host data for LCP critical path predictor
  // (LCPP).
  size_t max_hosts_to_track_for_lcpp;

  // The maximum number of origins to store per entry.
  size_t max_origins_per_entry;
  // The number of consecutive misses after which we stop tracking a resource
  // URL.
  size_t max_consecutive_misses;
  // The number of consecutive misses after which we stop tracking a redirect
  // endpoint.
  size_t max_redirect_consecutive_misses;

  // Delay between writing data to the predictors database memory cache and
  // flushing it to disk.
  size_t flush_data_to_disk_delay_seconds;

  // Parameters for LCPP multiple key support (crbug.com/328487899)
  // In LCPP multiple key support, hint data (aka. statistics) are stored not
  // per origin but origin + the first-path name.
  // In LcppMultipleKeyTypes::kLcppKeyStat option, the number of the first-path
  // name per origin is limited and we use the top-k element algorithm
  // (https://docs.google.com/document/d/1T80d4xW8xIEqfo792g1nC1deFqzMraunFJW_5ft4ziQ/edit#heading=h.fzlb8kz89fgy)
  // to decide an entry to evict.lcpp_multiple_key_histogram_sliding_window_size
  // and lcpp_multiple_key_max_histgram_buckets are paraieters used for the
  // algorithm. The former is is the virtual sliding window size, and the
  // latter is max number of buckets.
  size_t lcpp_multiple_key_histogram_sliding_window_size;
  size_t lcpp_multiple_key_max_histogram_buckets;

  // Parameters for double keyed LCPP (crbug.com/343093433)
  // In double keyd LCPP, hint data (aka. statistics) are stored
  // per navigation initiator origin + destination origin.
  // Initiator origins are kept by the above top-k algorithm too with the below
  // two parameters.
  // This feature is orthogonal to LCPP multiple key support.
  size_t lcpp_initiator_origin_histogram_sliding_window_size;
  size_t lcpp_initiator_origin_max_histogram_buckets;
};

}  // namespace predictors

#endif  // CHROME_BROWSER_PREDICTORS_LOADING_PREDICTOR_CONFIG_H_
