// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef COMPONENTS_SIGNIN_PUBLIC_BASE_LIST_ACCOUNTS_TEST_UTILS_H_
#define COMPONENTS_SIGNIN_PUBLIC_BASE_LIST_ACCOUNTS_TEST_UTILS_H_

#include <string>
#include <vector>

#include "google_apis/gaia/gaia_auth_test_util.h"

namespace network {
class TestURLLoaderFactory;
}  // namespace network

namespace signin {

// Make ListAccounts call return NotFound.
void SetListAccountsResponseHttpNotFound(
    network::TestURLLoaderFactory* test_url_loader_factory);

// Make ListAccounts call return an unexpected service response that leads to
// a one time retry request. It also sets the response for the retry request.
void SetListAccountsResponseWithUnexpectedServiceResponse(
    network::TestURLLoaderFactory* test_url_loader_factory);

// Make ListAccounts return a list of accounts based on the provided |params|.
void SetListAccountsResponseWithParams(
    const std::vector<gaia::CookieParams>& params,
    network::TestURLLoaderFactory* test_url_loader_factory);

// Helper methods, equivalent to calling
// SetListAccountsResponseWithParams().

// Make ListAccounts return no accounts.
void SetListAccountsResponseNoAccounts(
    network::TestURLLoaderFactory* test_url_loader_factory);

// Make ListAccounts return one account with the provided |email| and
// |gaia_id|.
void SetListAccountsResponseOneAccount(
    const std::string& email,
    const GaiaId& gaia_id,
    network::TestURLLoaderFactory* test_url_loader_factory);

// Make ListAccounts return one account based on the provided |params|.
void SetListAccountsResponseOneAccountWithParams(
    const gaia::CookieParams& params,
    network::TestURLLoaderFactory* test_url_loader_factory);

// Make ListAccounts return two accounts with the provided emails and gaia_ids.
void SetListAccountsResponseTwoAccounts(
    const std::string& email1,
    const GaiaId& gaia_id1,
    const std::string& email2,
    const GaiaId& gaia_id2,
    network::TestURLLoaderFactory* test_url_loader_factory);

}  // namespace signin

#endif  // COMPONENTS_SIGNIN_PUBLIC_BASE_LIST_ACCOUNTS_TEST_UTILS_H_
