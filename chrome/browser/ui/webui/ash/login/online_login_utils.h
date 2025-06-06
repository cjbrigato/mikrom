// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_ASH_LOGIN_ONLINE_LOGIN_UTILS_H_
#define CHROME_BROWSER_UI_WEBUI_ASH_LOGIN_ONLINE_LOGIN_UTILS_H_

#include <memory>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ash/login/login_client_cert_usage_observer.h"
#include "chrome/browser/ash/login/signin_partition_manager.h"
#include "chrome/browser/ui/ash/login/login_display_host.h"
#include "chrome/browser/ui/ash/login/signin_ui.h"
#include "chromeos/ash/components/login/auth/public/challenge_response_key.h"
#include "chromeos/ash/components/login/auth/public/saml_password_attributes.h"
#include "components/account_id/account_id.h"
#include "components/login/base_screen_handler_utils.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_ui.h"
#include "google_apis/gaia/gaia_id.h"
#include "google_apis/gaia/gaia_urls.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"

namespace ash {

class SyncTrustedVaultKeys;
class UserContext;

namespace login {

// A class that's used to specify the way how Gaia should be loaded.
struct GaiaContext {
  GaiaContext();
  GaiaContext(GaiaContext const&);

  // Email of the current user.
  std::string email;

  // GAIA ID of the current user.
  GaiaId gaia_id;

  // GAPS cookie.
  std::string gaps_cookie;
};

// Structure for holding the data from Gaia cookies that ChromeOS wants to
// store in the user context after a successful authentication.
struct GaiaCookiesData {
  GaiaCookiesData();
  GaiaCookiesData(GaiaCookiesData const&);
  ~GaiaCookiesData();

  void TransferCookiesToUserContext(UserContext& user_context);

  // login::kOAUTHCodeCookie - Not optional.
  std::string auth_code;

  // login::kGAPSCookie
  std::optional<std::string> gaps_cookie = std::nullopt;
  // Re-Auth Proof Token -- login::kRAPTCookie
  std::optional<std::string> rapt = std::nullopt;
};

// Artifacts that are generated by Gaia after successful online signin. Created
// when the authenticator fires the 'HandleCompleteAuthentication' event with
// data from the frontend and some extra fields provided by the Gaia screen.
struct OnlineSigninArtifacts {
  OnlineSigninArtifacts();
  OnlineSigninArtifacts(OnlineSigninArtifacts&& original);
  ~OnlineSigninArtifacts();

  GaiaId gaia_id;
  std::string email;
  bool using_saml;

  std::optional<std::string> password;
  std::optional<::login::StringList> scraped_saml_passwords;
  std::optional<::login::StringList> services_list;
  std::optional<SamlPasswordAttributes> saml_password_attributes;
  std::optional<SyncTrustedVaultKeys> sync_trusted_vault_keys;
  // Client certificate data for SmartCard flows. Only for SAML.
  std::optional<ChallengeResponseKey> challenge_response_key;
  std::optional<GaiaCookiesData> cookies;
};

using LoadGaiaWithPartition = base::OnceCallback<void(const std::string&)>;

using OnSetCookieForLoadGaiaWithPartition =
    base::OnceCallback<void(::net::CookieAccessResult)>;

using ChallengeResponseKeyOrError =
    base::expected<ChallengeResponseKey, SigninError>;

// Return Signin Session callback
base::OnceClosure GetStartSigninSession(::content::WebUI* web_ui,
                                        LoadGaiaWithPartition callback);

// Callback that set GAPS cookie for the partition.
void SetCookieForPartition(
    const login::GaiaContext& context,
    login::SigninPartitionManager* signin_partition_manager,
    OnSetCookieForLoadGaiaWithPartition callback);

// Return whether the user is regular user or child user.
user_manager::UserType GetUsertypeFromServicesString(
    const ::login::StringList& services);

// Extracts a client certificate that was used during authentication.
ChallengeResponseKeyOrError ExtractClientCertificates(
    const LoginClientCertUsageObserver&
        extension_provided_client_cert_usage_observer);

// Builds the UserContext with the information from the given Gaia user
// sign-in.
// Returns a `unique_ptr` to the `UserContext` that was just built from the
// provided parameters.
// Note: The return value is a `unique_ptr` to `UserContext` and not a
// `UserContext` by itself to signify that there should only be a single
// instance of `UserContext` active at any given time. This contract is
// currently not enforced, but may be enforced in the future.
std::unique_ptr<UserContext> BuildUserContextForGaiaSignIn(
    user_manager::UserType user_type,
    const AccountId& account_id,
    bool using_saml,
    bool using_saml_api,
    const std::string& password,
    const SamlPasswordAttributes& password_attributes,
    const std::optional<SyncTrustedVaultKeys>& sync_trusted_vault_keys,
    const std::optional<ChallengeResponseKey>& challenge_response_key);

// Returns user canonical e-mail. Finds already used account alias, if
// user has already signed in.
AccountId GetAccountId(const std::string& authenticated_email,
                       const std::string& id,
                       const AccountType& account_type);

// Common utility for checking whether family link is allowed.
bool IsFamilyLinkAllowed();

}  // namespace login

// GaiaCookieRetriever is used for retrieving the cookies that are set by Gaia
// by directly interacting with a |SigninPartitionManager| and delivering the
// results via the |OnCookieRetrievedCallback|. If a timeout occurs, the given
// |OnCookieTimeoutCallback| is invoked.
class GaiaCookieRetriever : public network::mojom::CookieChangeListener {
 public:
  using OnCookieTimeoutCallback = base::OnceCallback<void(void)>;
  using OnCookieRetrievedCallback =
      base::OnceCallback<void(login::GaiaCookiesData)>;

  explicit GaiaCookieRetriever(
      std::string signin_partition_name,
      login::SigninPartitionManager* signin_partition_manager,
      OnCookieTimeoutCallback on_cookie_timeout_callback,
      bool allow_empty_auth_code_for_testing = false);

  GaiaCookieRetriever(const GaiaCookieRetriever&) = delete;
  GaiaCookieRetriever& operator=(const GaiaCookieRetriever&) = delete;

  ~GaiaCookieRetriever() override;

  void RetrieveCookies(OnCookieRetrievedCallback on_cookie_retrieved_callback);

 private:
  // network::mojom::CookieChangeListener:
  void OnCookieChange(const net::CookieChangeInfo& change) override;

  void OnCookieWaitTimeout();

  void OnGetCookieListResponse(
      const net::CookieAccessResultList& cookies,
      const net::CookieAccessResultList& excluded_cookies);

  std::string signin_partition_name_;

  raw_ptr<login::SigninPartitionManager> signin_partition_manager_;

  // Connection to the CookieManager that signals when the GAIA cookies change.
  mojo::Receiver<network::mojom::CookieChangeListener> oauth_code_listener_{
      this};

  std::unique_ptr<UserContext> pending_user_context_;

  std::unique_ptr<base::OneShotTimer> cookie_waiting_timer_;

  OnCookieTimeoutCallback on_cookie_timeout_callback_;

  std::optional<OnCookieRetrievedCallback> on_cookie_retrieved_callback_;

  // To allow testing to continue without an oauth cookie.
  bool allow_empty_auth_code_for_testing_ = false;

  base::WeakPtrFactory<GaiaCookieRetriever> weak_factory_{this};
};

}  // namespace ash

#endif  // CHROME_BROWSER_UI_WEBUI_ASH_LOGIN_ONLINE_LOGIN_UTILS_H_
