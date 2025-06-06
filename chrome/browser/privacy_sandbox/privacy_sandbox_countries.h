// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PRIVACY_SANDBOX_PRIVACY_SANDBOX_COUNTRIES_H_
#define CHROME_BROWSER_PRIVACY_SANDBOX_PRIVACY_SANDBOX_COUNTRIES_H_

#include "base/memory/raw_ptr.h"

class PrivacySandboxCountries {
 public:
  virtual ~PrivacySandboxCountries() = default;
  // Determines if the user is in a consented country to Privacy Sandbox.
  virtual bool IsConsentCountry() = 0;

  // Determines if the user is in a ROW (Rest of World) country.
  virtual bool IsRestOfWorldCountry() = 0;

  // Determines if the user's latest country from the variation service is
  // China.
  virtual bool IsLatestCountryChina() = 0;
};

raw_ptr<PrivacySandboxCountries> GetSingletonPrivacySandboxCountries();

#endif  // CHROME_BROWSER_PRIVACY_SANDBOX_PRIVACY_SANDBOX_COUNTRIES_H_
