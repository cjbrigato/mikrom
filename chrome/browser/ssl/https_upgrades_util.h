// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SSL_HTTPS_UPGRADES_UTIL_H_
#define CHROME_BROWSER_SSL_HTTPS_UPGRADES_UTIL_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/stack_allocated.h"
#include "base/values.h"
#include "components/security_interstitials/core/https_only_mode_metrics.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/metrics/public/cpp/ukm_recorder.h"
#include "url/gurl.h"

class PrefService;
class Profile;

// Helper for applying the HttpAllowlist enterprise policy. Checks if the
// hostname of `url` matches any of the hostnames or hostname patterns in the
// `allowed_hosts` list. Does not allow blanket host wildcards (i.e., "*" which
// matches all hosts), but does allow partial domain wildcards (e.g.,
// "[*.]example.com"). Entries in `allowed_hosts` should follow the rules in
// https://chromeenterprise.google/policies/url-patterns/ (or they'll be
// ignored).
bool IsHostnameInHttpAllowlist(const GURL& url, PrefService* prefs);

// Adds `hostnames` to the HttpAllowlist enterprise policy for testing.
// Expects the allowlist to be empty before updating it.
void AllowHttpForHostnamesForTesting(const std::vector<std::string>& hostnames,
                                     PrefService* prefs);

// Clears HttpAllowlist enterprise policy for testing.
void ClearHttpAllowlistForHostnamesForTesting(PrefService* prefs);

// Returns true if the HTTPS-First Balanced Mode feature flag is enabled.
bool IsBalancedModeAvailable();

// Returns true if HTTPS-First Balanced Mode is enabled by the user's selection
// in security settings.
bool IsBalancedModeEnabled(PrefService* prefs);

// Returns true if the HTTPS-First Mode interstitial is enabled globally by the
// UI pref or by one of the Site Engagement and Typically Secure User
// heuristics.
bool IsInterstitialEnabled(
    const security_interstitials::https_only_mode::HttpInterstitialState&
        state);

// Same as IsInterstitialEnabled, but excludes HFM variants (notably, 'balanced'
// mode) that should not trigger on captive portals or similar exclusions.
bool IsStrictInterstitialEnabled(
    const security_interstitials::https_only_mode::HttpInterstitialState&
        state);

// Returns true if non-unique hostnames should be exempted from warnings.
// Different feature variations have different strictness levels on what to warn
// the user about.
bool ShouldExemptNonUniqueHostnames(
    const security_interstitials::https_only_mode::HttpInterstitialState&
        state);

// Returns true if the given URL should be excluded from interstitials in the
// current HFM mode. Does NOT mean that an upgrade shouldn't be attempted.
bool ShouldExcludeUrlFromInterstitial(
    const security_interstitials::https_only_mode::HttpInterstitialState& state,
    const GURL& url);

// Returns true if the Site Engagement heuristic for HTTPS-First Mode must be
// disabled and not cause an interstitial. Other conditions such as relevant
// prefs already having been set may also disable the heuristic on startup, but
// this is the bare minimum that must be checked.
bool MustDisableSiteEngagementHeuristic(Profile* profile);

// Returns true if the Typically Secure User heuristic for HTTPS-First Mode must
// be disabled and not cause an interstitial. Other conditions such as relevant
// prefs already having been set may also disable the heuristic on startup, but
// this is the bare minimum that must be checked.
bool MustDisableTypicallySecureUserHeuristic(Profile* profile);

void RecordHttpsFirstModeUKM(
    ukm::SourceId source_id,
    security_interstitials::https_only_mode::BlockingResult result);

// An instance of this class adds `hostnames` to the HttpAllowlist enterprise
// policy for testing and clears the allowlist when it goes out of scope.
class ScopedAllowHttpForHostnamesForTesting {
  STACK_ALLOCATED();

 public:
  explicit ScopedAllowHttpForHostnamesForTesting(
      const std::vector<std::string>& hostnames,
      PrefService* prefs);
  ScopedAllowHttpForHostnamesForTesting(
      const ScopedAllowHttpForHostnamesForTesting&) = delete;
  ScopedAllowHttpForHostnamesForTesting& operator=(
      const ScopedAllowHttpForHostnamesForTesting&) = delete;

  ~ScopedAllowHttpForHostnamesForTesting();

 private:
  raw_ptr<PrefService> prefs_;
};

#endif  // CHROME_BROWSER_SSL_HTTPS_UPGRADES_UTIL_H_
