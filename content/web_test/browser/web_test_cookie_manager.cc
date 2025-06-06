// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/web_test/browser/web_test_cookie_manager.h"

#include "content/public/browser/storage_partition.h"
#include "net/base/schemeful_site.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_partition_key.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "url/gurl.h"

namespace content {

WebTestCookieManager::WebTestCookieManager(
    network::mojom::CookieManager* const cookie_manager,
    const GURL& url,
    const net::IsolationInfo& isolation_info)
    : cookie_manager_(cookie_manager),
      url_(url),
      isolation_info_(isolation_info) {
  DCHECK(url_->is_valid());
}

std::optional<net::CookiePartitionKey>
WebTestCookieManager::GetCookiePartitionKey() const {
  return net::CookiePartitionKey::FromNetworkIsolationKey(
      isolation_info_->network_isolation_key(),
      isolation_info_->site_for_cookies(), net::SchemefulSite(*url_),
      /*main_frame_navigation=*/false);
}

void WebTestCookieManager::DeleteAllCookies(
    blink::test::mojom::CookieManagerAutomation::DeleteAllCookiesCallback
        callback) {
  network::mojom::CookieDeletionFilterPtr deletion_filter =
      network::mojom::CookieDeletionFilter::New();
  deletion_filter->url = *url_;
  cookie_manager_->DeleteCookies(
      std::move(deletion_filter),
      base::BindOnce(
          [](blink::test::mojom::CookieManagerAutomation::
                 DeleteAllCookiesCallback callback,
             uint32_t) {
            // There is no ability to detect rejection here.
            std::move(callback).Run();
          },
          std::move(callback)));
}

void WebTestCookieManager::GetAllCookies(
    blink::test::mojom::CookieManagerAutomation::GetAllCookiesCallback
        callback) {
  cookie_manager_->GetCookieList(
      *url_, net::CookieOptions::MakeAllInclusive(),
      net::CookiePartitionKeyCollection(GetCookiePartitionKey()),
      base::BindOnce(
          [](blink::test::mojom::CookieManagerAutomation::GetAllCookiesCallback
                 callback,
             const net::CookieAccessResultList& cookies,
             const net::CookieAccessResultList&) {
            std::move(callback).Run(std::move(cookies));
          },
          std::move(callback)));
}

void WebTestCookieManager::GetNamedCookie(
    const std::string& name,
    blink::test::mojom::CookieManagerAutomation::GetNamedCookieCallback
        callback) {
  cookie_manager_->GetCookieList(
      *url_, net::CookieOptions::MakeAllInclusive(),
      net::CookiePartitionKeyCollection(GetCookiePartitionKey()),
      base::BindOnce(
          [](const std::string& name,
             blink::test::mojom::CookieManagerAutomation::GetNamedCookieCallback
                 callback,
             const net::CookieAccessResultList& cookies,
             const net::CookieAccessResultList&) {
            for (const auto& cookie : cookies) {
              if (cookie.cookie.Name() == name) {
                std::move(callback).Run(std::move(cookie));
                return;
              }
            }
            std::move(callback).Run(std::nullopt);
          },
          name, std::move(callback)));
}

}  // namespace content
