// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PAYMENTS_CONTENT_PAYMENT_METHOD_MANIFEST_TABLE_H_
#define COMPONENTS_PAYMENTS_CONTENT_PAYMENT_METHOD_MANIFEST_TABLE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/strings/cstring_view.h"
#include "base/time/time.h"
#include "components/payments/content/browser_binding/browser_bound_key_metadata.h"
#include "components/webdata/common/web_database_table.h"

class WebDatabase;

namespace payments {

struct SecurePaymentConfirmationCredential;

// This class manages Web Payment tables in SQLite database. It expects the
// following schema.
//
// payment_method_manifest This table stores WebAppManifestSection.id of the
//                         supported web app in this payment method manifest.
//                         Note that a payment method manifest might contain
//                         multiple supported web apps ids.
//
//   expire_date           The expire date in seconds from 1601-01-01 00:00:00
//                         UTC.
//   method_name           The method name.
//   web_app_id            The supported web app id.
//                         (WebAppManifestSection.id).
//
// secure_payment_confirmation_instrument
//                         This table stores credential information for secure
//                         payment confirmation method. Historically it also
//                         stored instrument information, hence the name and
//                         the (no longer used) label and icon fields.
//                         This table is only used when credential store APIs
//                         are unavailable: This is controlled by the
//                         platform-specific
//                         SecurePaymentConfirmationUseCredentialStoreAPIs flag:
//                         On Android, currently, this table is not used.
//
//   credential_id         The WebAuthn credential identifier blob. Primary key.
//   relying_party_id      The relying party identifier string.
//   label                 The instrument human-readable label string.
//   icon                  The serialized SkBitmap blob.
//   data_created          The creation date in micro seconds from 1601-01-01
//                         00:00:00 UTC.
//
// secure_payment_confirmation_browser_bound_key
//                         This table stores browser bound key information for
//                         payment credentials. The primary key of this table is
//                         the pair of `credential_id` and `relying_party_id`.
//
//   credential_id         The WebAuthn credential identifier blob.
//   relying_party_id      The relying party identifier string.
//   browser_bound_key_id  The identifier of the browser bound key.
class PaymentMethodManifestTable : public WebDatabaseTable {
 public:
  PaymentMethodManifestTable();
  ~PaymentMethodManifestTable() override;

  PaymentMethodManifestTable(const PaymentMethodManifestTable& other) = delete;
  PaymentMethodManifestTable& operator=(
      const PaymentMethodManifestTable& other) = delete;

  // Retrieves the PaymentMethodManifestTable* owned by `db`.
  static PaymentMethodManifestTable* FromWebDatabase(WebDatabase* db);

  // WebDatabaseTable:
  WebDatabaseTable::TypeKey GetTypeKey() const override;
  bool CreateTablesIfNecessary() override;
  bool MigrateToVersion(int version, bool* update_compatible_version) override;

  // Remove expired data.
  void RemoveExpiredData();

  // Clears all of the secure payment confirmation credential information
  // created in the given time range `begin` and `end`. Return false for
  // failure.
  bool ClearSecurePaymentConfirmationCredentials(base::Time begin,
                                                 base::Time end);

  // Adds `payment_method`'s manifest. `web_app_ids` contains supported web apps
  // ids.
  bool AddManifest(const std::string& payment_method,
                   const std::vector<std::string>& web_app_ids);

  // Gets manifest for `payment_method`. Return empty vector if no manifest
  // exists for this method.
  std::vector<std::string> GetManifest(const std::string& payment_method);

  // Adds a secure payment confirmation `credential`. All existing data for the
  // credential's (relying_party_id, credential_id) tuple is erased before the
  // new data is added.
  //
  // Each field in the `credential` should be non-empty and `relying_party_id`
  // field should be a valid domain string. See:
  // https://url.spec.whatwg.org/#valid-domain-string
  //
  // Returns false for invalid data, e.g., credential reuse between relying
  // parties, or on failure.
  bool AddSecurePaymentConfirmationCredential(
      const SecurePaymentConfirmationCredential& credential);

  // Executes a SQL statement for testing.
  //
  // Returns true if all statements execute successfully. If a statement fails,
  // stops and returns false. Calls should be wrapped in ASSERT_TRUE().
  bool ExecuteForTest(const base::cstring_view sql);

  // Raze the database to the ground for testing.
  //
  // false is returned if the database is locked by some other
  // process.
  bool RazeForTest();

  // Returns true if a column with the given name exists in the given table.
  bool DoesColumnExistForTest(const base::cstring_view table_name,
                              const base::cstring_view column_name);

  // Gets the list of secure payment confirmation credentials for the given list
  // of `credential_ids`.
  //
  // Returns an empty vector when no data is found or when a read error occurs.
  // Does not return invalid credentials.
  //
  // Please use `std::move()` for `credential_ids` parameter to avoid extra
  // copies.
  std::vector<std::unique_ptr<SecurePaymentConfirmationCredential>>
  GetSecurePaymentConfirmationCredentials(
      std::vector<std::vector<uint8_t>> credential_ids,
      const std::string& relying_party_id);

  // Sets a browser bound key identifier for the credential id, relying party id
  // pair. If a browser bound key exists, no updates are performed and false is
  // returned.
  //
  // Returns whether the browser bound key id was set.
  bool SetBrowserBoundKey(std::vector<uint8_t> credential_id,
                          std::string_view relying_party_id,
                          std::vector<uint8_t> browser_bound_key_id);

  // Gets the browser bound key id for the given credential id, relying party id
  // pair.
  //
  // Returns the browser bound key id or nullopt when not found (or error
  // occurred during retrieval).
  std::optional<std::vector<uint8_t>> GetBrowserBoundKey(
      std::vector<uint8_t> credential_id,
      std::string_view relying_party_id);

  // Gets all browser bound key entries.
  //
  // Returns the possibly empty vector of entries or an empty vector when a read
  // error occurs.
  std::vector<BrowserBoundKeyMetadata> GetAllBrowserBoundKeys();

  // Deletes the given browser bound key entries by relying_party_id and
  // credential_id.
  bool DeleteBrowserBoundKeys(
      std::vector<BrowserBoundKeyMetadata::RelyingPartyAndCredentialId>
          passkeys);
};

}  // namespace payments

#endif  // COMPONENTS_PAYMENTS_CONTENT_PAYMENT_METHOD_MANIFEST_TABLE_H_
