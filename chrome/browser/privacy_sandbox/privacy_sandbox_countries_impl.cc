// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/containers/fixed_flat_set.h"
#include "base/metrics/histogram_functions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/privacy_sandbox/privacy_sandbox_countries.h"
#include "components/privacy_sandbox/privacy_sandbox_features.h"
#include "components/variations/service/variations_service.h"

namespace {

/**
 * Retrieves the latest country code from the variations service.
 *
 * Returns an empty string if the variations service is not available.
 */
std::string GetLatestCountry(
    variations::VariationsService* variations_service) {
  if (!variations_service) {
    return "";
  }
  return variations_service->GetLatestCountry();
}

/**
 * Retrieves the stored permanent country code from the variations service.
 *
 * Returns an empty string if the variations service is not available.
 */
std::string GetStoredPermanentCountry(
    variations::VariationsService* variations_service) {
  if (!variations_service) {
    return "";
  }
  return variations_service->GetStoredPermanentCountry();
}

constexpr auto kPrivacySandboxConsentCountries =
    base::MakeFixedFlatSet<std::string_view>({
        "gb", "at", "ax", "be", "bg", "bl", "ch", "cy", "cz", "de", "dk",
        "ee", "es", "fi", "fr", "gf", "gg", "gi", "gp", "gr", "hr", "hu",
        "ie", "is", "it", "je", "ke", "li", "lt", "lu", "lv", "mf", "mt",
        "mq", "nc", "nl", "no", "pf", "pl", "pm", "pt", "qa", "re", "ro",
        "se", "si", "sk", "sj", "tf", "va", "wf", "yt",
    });

class PrivacySandboxCountriesImpl : public PrivacySandboxCountries {
 public:
  bool IsConsentCountry() override {
    CHECK(g_browser_process);
    return kPrivacySandboxConsentCountries.contains(
        GetStoredPermanentCountry(g_browser_process->variations_service()));
  }

  bool IsRestOfWorldCountry() override {
    CHECK(g_browser_process);
    base::UmaHistogramBoolean(
        "PrivacySandbox.NoticeRequirement.IsVariationServiceReady",
        g_browser_process->variations_service() != nullptr);
    std::string country =
        GetStoredPermanentCountry(g_browser_process->variations_service());
    base::UmaHistogramBoolean(
        "PrivacySandbox.NoticeRequirement.IsVariationCountryEmpty",
        country.empty());
    return !country.empty() &&
           !kPrivacySandboxConsentCountries.contains(country);
  }

  bool IsLatestCountryChina() override {
    CHECK(g_browser_process);
    return GetLatestCountry(g_browser_process->variations_service()) == "cn";
  }
};

}  // namespace

raw_ptr<PrivacySandboxCountries> GetSingletonPrivacySandboxCountries() {
  static PrivacySandboxCountriesImpl instance;
  return &instance;
}
