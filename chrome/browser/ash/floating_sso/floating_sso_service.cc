// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/floating_sso/floating_sso_service.h"

#include <memory>

#include "ash/constants/ash_features.h"
#include "ash/constants/ash_pref_names.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/values.h"
#include "chrome/browser/ash/floating_sso/cookie_sync_conversions.h"
#include "chrome/browser/ash/floating_sso/floating_sso_sync_bridge.h"
#include "chrome/browser/ash/floating_workspace/floating_workspace_util.h"
#include "chrome/common/pref_names.h"
#include "components/google/core/common/google_util.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "components/sync/base/pref_names.h"
#include "components/url_matcher/url_util.h"
#include "net/cookies/cookie_change_dispatcher.h"
#include "net/cookies/cookie_util.h"

namespace ash::floating_sso {

namespace {

bool IsGoogleUrl(const GURL& url) {
  return google_util::IsGoogleDomainUrl(
             url, google_util::ALLOW_SUBDOMAIN,
             google_util::ALLOW_NON_STANDARD_PORTS) ||
         google_util::IsYoutubeDomainUrl(url, google_util::ALLOW_SUBDOMAIN,
                                         google_util::ALLOW_NON_STANDARD_PORTS);
}

}  // namespace

FloatingSsoService::FloatingSsoService(
    PrefService* prefs,
    std::unique_ptr<FloatingSsoSyncBridge> bridge,
    CookieManagerGetter cookie_manager_getter)
    : prefs_(prefs),
      cookie_manager_getter_(cookie_manager_getter),
      bridge_(std::move(bridge)),
      pref_change_registrar_(std::make_unique<PrefChangeRegistrar>()) {
  pref_change_registrar_->Init(prefs_);
  RegisterPolicyListeners();
  UpdateUrlMatchers();
  StartOrStop();
}

FloatingSsoService::~FloatingSsoService() = default;

void FloatingSsoService::Shutdown() {
  pref_change_registrar_.reset();
  prefs_ = nullptr;
}

void FloatingSsoService::RegisterPolicyListeners() {
  pref_change_registrar_->Add(
      ::prefs::kFloatingSsoEnabled,
      base::BindRepeating(&FloatingSsoService::StartOrStop,
                          base::Unretained(this)));
  pref_change_registrar_->Add(
      syncer::prefs::internal::kSyncKeepEverythingSynced,
      base::BindRepeating(&FloatingSsoService::StartOrStop,
                          base::Unretained(this)));
  pref_change_registrar_->Add(
      syncer::prefs::internal::kSyncCookies,
      base::BindRepeating(&FloatingSsoService::StartOrStop,
                          base::Unretained(this)));
  pref_change_registrar_->Add(
      syncer::prefs::internal::kSyncManaged,
      base::BindRepeating(&FloatingSsoService::StartOrStop,
                          base::Unretained(this)));
  // Policy updates will only affect future updates of cookies, this means that
  // cookies that already exist are not checked again to see if some of them are
  // no longer blocklisted.
  pref_change_registrar_->Add(
      ::prefs::kFloatingSsoDomainBlocklist,
      base::BindRepeating(&FloatingSsoService::UpdateUrlMatchers,
                          base::Unretained(this)));
  pref_change_registrar_->Add(
      ::prefs::kFloatingSsoDomainBlocklistExceptions,
      base::BindRepeating(&FloatingSsoService::UpdateUrlMatchers,
                          base::Unretained(this)));
}

void FloatingSsoService::UpdateUrlMatchers() {
  // Reset URL matchers every time the policies change.
  block_url_matcher_ = std::make_unique<url_matcher::URLMatcher>();
  except_url_matcher_ = std::make_unique<url_matcher::URLMatcher>();

  const base::Value::List& blocklist =
      prefs_->GetList(::prefs::kFloatingSsoDomainBlocklist);
  const base::Value::List& blocklist_exceptions =
      prefs_->GetList(::prefs::kFloatingSsoDomainBlocklistExceptions);

  if (!blocklist.empty()) {
    base::MatcherStringPattern::ID block_id = 0;
    url_matcher::util::AddFiltersWithLimit(
        block_url_matcher_.get(), /*allow=*/false, &block_id, blocklist);
  }

  if (!blocklist_exceptions.empty()) {
    base::MatcherStringPattern::ID except_id = 0;
    url_matcher::util::AddFiltersWithLimit(except_url_matcher_.get(),
                                           /*allow=*/true, &except_id,
                                           blocklist_exceptions);
  }
}

void FloatingSsoService::StartOrStop() {
  if (IsFloatingSsoEnabled()) {
    if (!scoped_observation_.IsObserving()) {
      scoped_observation_.Observe(bridge_.get());
    }
    MaybeStartListening();
  } else {
    scoped_observation_.Reset();
    StopListening();
  }
}

bool FloatingSsoService::IsFloatingSsoEnabled() {
  // Check FloatingSsoEnabled policy.
  if (!prefs_->GetBoolean(::prefs::kFloatingSsoEnabled)) {
    return false;
  }
  // Check SyncDisabled policy (it maps to kSyncManaged pref).
  if (prefs_->GetBoolean(syncer::prefs::internal::kSyncManaged)) {
    return false;
  }
  // Check that user either syncs everything or has selected cookies as one of
  // synced types.
  if (!prefs_->GetBoolean(syncer::prefs::internal::kSyncKeepEverythingSynced)) {
    return prefs_->GetBoolean(syncer::prefs::internal::kSyncCookies);
  }
  return true;
}

void FloatingSsoService::RunWhenCookiesAreReady(base::OnceClosure callback) {
  if (changes_in_progress_count_ == 0) {
    std::move(callback).Run();
  } else {
    on_no_changes_in_progress_callback_ = std::move(callback);
  }
}

void FloatingSsoService::RunWhenCookiesAreReadyOnFirstSync(
    base::OnceClosure callback) {
  // base::Unretained() is safe since `bridge_` is owned by `this`.
  bridge_->SetOnMergeFullSyncDataCallback(
      base::BindOnce(&FloatingSsoService::RunWhenCookiesAreReady,
                     base::Unretained(this), std::move(callback)));
}

void FloatingSsoService::MarkToNotOverride(const net::CanonicalCookie& cookie) {
  std::optional<std::string> storage_key = SerializedKey(cookie);
  if (storage_key) {
    bridge_->AddToLocallyPreferredCookies(storage_key.value());
  }
}

void FloatingSsoService::MaybeStartListening() {
  if (!receiver_.is_bound()) {
    BindToCookieManager();
  }
}

void FloatingSsoService::StopListening() {
  if (receiver_.is_bound()) {
    // In case cookie listening will resume in the same session, make sure the
    // accumulated cookie list will be fetched.
    fetch_accumulated_cookies_ = true;
    receiver_.reset();
  }
}

void FloatingSsoService::BindToCookieManager() {
  network::mojom::CookieManager* cookie_manager = cookie_manager_getter_.Run();
  if (!cookie_manager) {
    return;
  }
  cookie_manager->AddGlobalChangeListener(receiver_.BindNewPipeAndPassRemote());
  receiver_.set_disconnect_handler(base::BindOnce(
      &FloatingSsoService::OnConnectionError, base::Unretained(this)));

  if (fetch_accumulated_cookies_) {
    cookie_manager->GetAllCookies(base::BindOnce(
        &FloatingSsoService::OnCookiesLoaded, base::Unretained(this)));
  }
}

void FloatingSsoService::OnCookieChange(const net::CookieChangeInfo& change) {
  const net::CanonicalCookie& cookie = change.cookie;
  if (!ShouldSyncCookie(cookie)) {
    return;
  }

  switch (change.cause) {
    case net::CookieChangeCause::INSERTED:
    case net::CookieChangeCause::INSERTED_NO_CHANGE_OVERWRITE: {
      bridge_->AddOrUpdateCookie(cookie);
      break;
    }
    // All cases below correspond to deletion of a cookie. When intention is to
    // update the cookie (e.g. in the case of `CookieChangeCause::OVERWRITE`),
    // they will be immediately followed by a `CookieChangeCause::INSERTED`
    // change.
    case net::CookieChangeCause::EXPLICIT:
    case net::CookieChangeCause::UNKNOWN_DELETION:
    case net::CookieChangeCause::OVERWRITE:
    case net::CookieChangeCause::EXPIRED:
    case net::CookieChangeCause::EVICTED:
    case net::CookieChangeCause::EXPIRED_OVERWRITE:
      bridge_->DeleteCookie(cookie);
      break;
  }
}

void FloatingSsoService::OnCookiesAddedOrUpdatedRemotely(
    const std::vector<net::CanonicalCookie>& cookies) {
  network::mojom::CookieManager* cookie_manager = cookie_manager_getter_.Run();
  net::CookieOptions options;
  // Allow to alter http_only and SameSite cookies since we are restoring this
  // cookie from another Chrome session.
  options.set_include_httponly();
  options.set_same_site_cookie_context(
      net::CookieOptions::SameSiteCookieContext::MakeInclusive());
  changes_in_progress_count_ += cookies.size();
  for (const net::CanonicalCookie& cookie : cookies) {
    // Sync server might contain changes for cookies which should no longer be
    // synced due to a change of policies or a change in feature design and
    // implementation. In that case, ignore them on the client side and let
    // corresponding sync entities die on the server side based on TTL .
    if (!ShouldSyncCookie(cookie)) {
      --changes_in_progress_count_;
      continue;
    }
    cookie_manager->SetCanonicalCookie(
        cookie, net::cookie_util::SimulatedCookieSource(cookie, "https"),
        options,
        base::BindOnce(&FloatingSsoService::OnCookieSet,
                       base::Unretained(this)));
  }
}

void FloatingSsoService::OnCookiesRemovedRemotely(
    const std::vector<net::CanonicalCookie>& cookies) {
  network::mojom::CookieManager* cookie_manager = cookie_manager_getter_.Run();
  changes_in_progress_count_ += cookies.size();
  for (const net::CanonicalCookie& cookie : cookies) {
    // Sync server might contain changes for cookies which should no longer be
    // synced due to a change of policies or a change in feature design and
    // implementation. In that case, ignore them on the client side.
    if (!ShouldSyncCookie(cookie)) {
      --changes_in_progress_count_;
      continue;
    }

    cookie_manager->DeleteCanonicalCookie(
        cookie, base::BindOnce(&FloatingSsoService::OnCookieDeleted,
                               base::Unretained(this)));
  }
}

void FloatingSsoService::OnCookiesLoaded(const net::CookieList& cookies) {
  for (const net::CanonicalCookie& cookie : cookies) {
    if (!ShouldSyncCookie(cookie)) {
      continue;
    }
    bridge_->AddOrUpdateCookie(cookie);
  }
}

bool FloatingSsoService::ShouldSyncCookie(
    const net::CanonicalCookie& cookie) const {
  // We only sync cookies from HTTP headers because:
  // 1) this should be enough to transfer auth-related cookies
  // 2) JavaScript and/or extensions might update cookies much more frequently
  // and we want to limit the number of Sync requests
  if (cookie.SourceType() != net::CookieSourceType::kHTTP) {
    return false;
  }
  // Filter out session cookies (except when Floating Workspace is enabled).
  if (!cookie.IsPersistent() &&
      !ash::floating_workspace_util::IsFloatingWorkspaceV2Enabled()) {
    return false;
  }

  const GURL cookie_domain_url = net::cookie_util::CookieOriginToURL(
      cookie.Domain(), cookie.SecureAttribute());
  return ShouldSyncCookiesForUrl(cookie_domain_url);
}

bool FloatingSsoService::ShouldSyncCookiesForUrl(const GURL& url) const {
  // Filter out Google cookies.
  if (IsGoogleUrl(url)) {
    return false;
  }
  // Filter out policy-blocked URLs.
  if (!IsDomainAllowed(url)) {
    return false;
  }
  return true;
}

bool FloatingSsoService::IsDomainAllowed(const GURL& url) const {
  bool is_excepted = !except_url_matcher_->MatchURL(url).empty();

  // Exception list takes precedence.
  if (is_excepted) {
    return true;
  }

  // The domain is not blocked if it doesn't have matches in the blocklist.
  return block_url_matcher_->MatchURL(url).empty();
}

void FloatingSsoService::OnCookieSet(net::CookieAccessResult result) {
  DecrementChangesCountAndMaybeNotify();
}

void FloatingSsoService::OnCookieDeleted(bool success) {
  DecrementChangesCountAndMaybeNotify();
}

void FloatingSsoService::DecrementChangesCountAndMaybeNotify() {
  CHECK(changes_in_progress_count_ > 0);
  --changes_in_progress_count_;
  if (changes_in_progress_count_ == 0 && on_no_changes_in_progress_callback_) {
    std::move(on_no_changes_in_progress_callback_).Run();
  }
}

void FloatingSsoService::OnConnectionError() {
  // Don't fetch the accumulated cookies because we will try to reconnect right
  // away.
  fetch_accumulated_cookies_ = false;
  receiver_.reset();
  MaybeStartListening();
}

base::WeakPtr<syncer::DataTypeControllerDelegate>
FloatingSsoService::GetControllerDelegate() {
  return bridge_->change_processor()->GetControllerDelegate();
}

}  // namespace ash::floating_sso
