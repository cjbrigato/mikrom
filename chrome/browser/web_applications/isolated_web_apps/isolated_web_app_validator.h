// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WEB_APPLICATIONS_ISOLATED_WEB_APPS_ISOLATED_WEB_APP_VALIDATOR_H_
#define CHROME_BROWSER_WEB_APPLICATIONS_ISOLATED_WEB_APPS_ISOLATED_WEB_APP_VALIDATOR_H_

#include <memory>
#include <optional>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/types/expected.h"
#include "chrome/browser/web_applications/isolated_web_apps/error/unusable_swbn_file_error.h"

class GURL;
class Profile;

namespace web_package {
class SignedWebBundleId;
class SignedWebBundleIntegrityBlock;
}  // namespace web_package

namespace web_app {

class IsolatedWebAppValidator {
 public:
  // Validates that the integrity block of the Isolated Web App contains trusted
  // public keys given the `expected_web_bundle_id`.
  //
  // This function also makes sure that the `expected_web_bundle_id` is actually
  // derived from the `integrity_block`.
  //
  // Important: This method does not verify the signatures themselves - it only
  // checks whether the public keys associated with these signatures correspond
  // to trusted parties.
  static base::expected<void, UnusableSwbnFileError> ValidateIntegrityBlock(
      Profile& profile,
      const web_package::SignedWebBundleId& expected_web_bundle_id,
      const web_package::SignedWebBundleIntegrityBlock& integrity_block,
      bool dev_mode);

  // Validates that the metadata of the Isolated Web App is valid given the
  // `web_bundle_id`.
  static base::expected<void, UnusableSwbnFileError> ValidateMetadata(
      const web_package::SignedWebBundleId& web_bundle_id,
      const std::optional<GURL>& primary_url,
      const std::vector<GURL>& entries);
};

}  // namespace web_app

#endif  // CHROME_BROWSER_WEB_APPLICATIONS_ISOLATED_WEB_APPS_ISOLATED_WEB_APP_VALIDATOR_H_
