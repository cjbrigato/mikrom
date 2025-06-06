// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/webid/idp_network_request_manager.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "base/memory/ref_counted_memory.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/values.h"
#include "content/browser/webid/test/mock_permission_delegate.h"
#include "content/common/features.h"
#include "content/public/browser/identity_request_dialog_controller.h"
#include "content/public/browser/manifest_icon_downloader.h"
#include "content/public/common/content_features.h"
#include "content/public/test/browser_task_environment.h"
#include "net/base/isolation_info.h"
#include "net/base/load_flags.h"
#include "net/base/network_isolation_key.h"
#include "net/base/network_isolation_partition.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "services/data_decoder/public/cpp/test_support/in_process_data_decoder.h"
#include "services/network/public/cpp/cors/cors_error_status.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/client_security_state.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/mojom/webid/federated_auth_request.mojom.h"
#include "third_party/re2/src/re2/re2.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using IdpClientMetadata = content::IdpNetworkRequestManager::ClientMetadata;
using TokenResult = content::IdpNetworkRequestManager::TokenResult;
using Endpoints = content::IdpNetworkRequestManager::Endpoints;
using FetchStatus = content::IdpNetworkRequestManager::FetchStatus;
using ParseStatus = content::IdpNetworkRequestManager::ParseStatus;
using AccountsRequestCallback =
    content::IdpNetworkRequestManager::AccountsRequestCallback;
using LoginState = content::IdentityRequestAccount::LoginState;
using AccountsResponseInvalidReason =
    content::IdpNetworkRequestManager::AccountsResponseInvalidReason;
using ErrorDialogType = content::IdpNetworkRequestManager::FedCmErrorDialogType;
using ErrorUrlType = content::IdpNetworkRequestManager::FedCmErrorUrlType;
using TokenResponseType =
    content::IdpNetworkRequestManager::FedCmTokenResponseType;

namespace content {

namespace {

// Values for testing. Real minimum and ideal sizes are different.
constexpr int kTestBrandIconMinimumSize = 16;
constexpr int kTestBrandIconIdealSize = 32;

constexpr char kTestIdpUrl[] = "https://idp.test";
constexpr char kTestRpUrl[] = "https://rp.test";
constexpr char kTestWellKnownUrl[] =
    "https://idp.test/.well-known/web-identity";
constexpr char kTestConfigUrl[] = "https://idp.test/fedcm.json";
constexpr char kTestAccountsEndpoint[] = "https://idp.test/accounts_endpoint";
constexpr char kTestTokenEndpoint[] = "https://idp.test/token_endpoint";
constexpr char kTestClientMetadataEndpoint[] =
    "https://idp.test/client_metadata_endpoint";
constexpr char kTestDisconnectEndpoint[] =
    "https://idp.test/revocation_endpoint";
constexpr char kTestLocalHostTokenEndpoint[] =
    "http://localhost/token_endpoint";

constexpr char kSingleAccountEndpointValidJson[] = R"({
  "accounts" : [
    {
      "id" : "1234",
      "email": "ken@idp.test",
      "name": "Ken R. Example",
      "given_name": "Ken",
      "picture": "https://idp.test/profile/1"
    }
  ]
  })";

// Replaces the first line with the passed-in JSON key in `input` with
// `new_line`.
std::string ReplaceFirstLineWithKeyFromJson(const std::string& key,
                                            const std::string& new_line,
                                            const std::string& input,
                                            bool replace_all) {
  std::string pattern = R"(^[ ]*")" + key + R"(".*$)";
  std::string result = input;
  RE2::Options options;
  options.set_posix_syntax(true);
  options.set_one_line(false);
  RE2 re2(pattern, options);
  if (replace_all)
    RE2::GlobalReplace(&result, re2, new_line);
  else
    RE2::Replace(&result, re2, new_line);
  return result;
}

// Removes all lines with the passed-in JSON key in `input`.
std::string RemoveAllLinesWithKeyFromJson(const std::string& key,
                                          const std::string& input) {
  return ReplaceFirstLineWithKeyFromJson(key, "", input, /*replace_all=*/true);
}

url::Origin GetOriginHeader(const network::ResourceRequest& request) {
  return url::Origin::Create(
      GURL(request.headers.GetHeader(net::HttpRequestHeaders::kOrigin)
               .value_or(std::string())));
}

class IdpNetworkRequestManagerTest : public ::testing::Test {
 public:
  std::unique_ptr<IdpNetworkRequestManager> CreateTestManager(
      const char* top_level_origin = nullptr) {
    test_permission_delegate_ =
        std::make_unique<NiceMock<MockPermissionDelegate>>();
    url::Origin top_level_origin_obj;
    if (top_level_origin) {
      top_level_origin_obj = url::Origin::Create(GURL(top_level_origin));
    }
    return std::make_unique<IdpNetworkRequestManager>(
        url::Origin::Create(GURL(kTestRpUrl)), top_level_origin_obj,
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &test_url_loader_factory_),
        test_permission_delegate_.get(),
        network::mojom::ClientSecurityState::New(), content::FrameTreeNodeId());
  }

  void AddResponse(const GURL& url,
                   net::HttpStatusCode http_status,
                   const std::string& mime_type,
                   const std::string& content,
                   bool cors_error = false) {
    auto head = network::mojom::URLResponseHead::New();
    std::string raw_header = "HTTP/1.1 " + base::NumberToString(http_status) +
                             " " + net::GetHttpReasonPhrase(http_status) +
                             "\n"
                             "Content-type: " +
                             mime_type + "\n\n";
    head->headers = net::HttpResponseHeaders::TryToCreate(raw_header);
    test_url_loader_factory().AddResponse(
        url, std::move(head), content,
        cors_error
            ? network::URLLoaderCompletionStatus(network::CorsErrorStatus(
                  network::mojom::CorsError::kMissingAllowOriginHeader))
            : network::URLLoaderCompletionStatus());
  }

  std::tuple<FetchStatus, IdpNetworkRequestManager::WellKnown>
  SendWellKnownRequestAndWaitForResponse(
      const char* test_data,
      net::HttpStatusCode http_status = net::HTTP_OK,
      const std::string& mime_type = "application/json") {
    GURL well_known_url(kTestWellKnownUrl);
    AddResponse(well_known_url, http_status, mime_type, test_data);

    base::RunLoop run_loop;
    FetchStatus parsed_fetch_status;
    IdpNetworkRequestManager::WellKnown parsed_wellknow;
    auto callback = base::BindLambdaForTesting(
        [&](FetchStatus fetch_status,
            const IdpNetworkRequestManager::WellKnown& well_known) {
          parsed_fetch_status = fetch_status;
          parsed_wellknow = well_known;
          run_loop.Quit();
        });

    std::unique_ptr<IdpNetworkRequestManager> manager = CreateTestManager();
    manager->FetchWellKnown(GURL(kTestIdpUrl), std::move(callback));
    run_loop.Run();

    return {parsed_fetch_status, parsed_wellknow};
  }

  std::tuple<FetchStatus, IdentityProviderMetadata>
  SendConfigRequestAndWaitForResponse(
      const char* test_data,
      net::HttpStatusCode http_status = net::HTTP_OK,
      const std::string& mime_type = "application/json",
      blink::mojom::RpMode rp_mode = blink::mojom::RpMode::kPassive) {
    GURL config_url(kTestConfigUrl);
    AddResponse(config_url, http_status, mime_type, test_data);

    base::RunLoop run_loop;
    FetchStatus parsed_fetch_status;
    IdentityProviderMetadata parsed_idp_metadata;
    auto callback = base::BindLambdaForTesting(
        [&](FetchStatus fetch_status, Endpoints endpoints,
            IdentityProviderMetadata idp_metadata) {
          parsed_fetch_status = fetch_status;
          parsed_idp_metadata = std::move(idp_metadata);
          run_loop.Quit();
        });

    std::unique_ptr<IdpNetworkRequestManager> manager = CreateTestManager();
    manager->FetchConfig(GURL(kTestConfigUrl), rp_mode, kTestBrandIconIdealSize,
                         kTestBrandIconMinimumSize, std::move(callback));
    run_loop.Run();

    return {parsed_fetch_status, parsed_idp_metadata};
  }

  std::tuple<FetchStatus, std::vector<IdentityRequestAccountPtr>>
  SendAccountsRequestWithStoredAccounts(base::Value::List test_accounts,
                                        const char* client_id = "") {
    GURL accounts_endpoint(kTestAccountsEndpoint);
    url::Origin idp_origin = url::Origin::Create(accounts_endpoint);

    base::RunLoop run_loop;
    FetchStatus parsed_accounts_response;
    std::vector<IdentityRequestAccountPtr> parsed_accounts;
    auto callback = base::BindLambdaForTesting(
        [&](FetchStatus response,
            std::vector<IdentityRequestAccountPtr> accounts) {
          parsed_accounts_response = response;
          parsed_accounts = accounts;
          run_loop.Quit();
        });

    std::unique_ptr<IdpNetworkRequestManager> manager = CreateTestManager();

    EXPECT_CALL(*test_permission_delegate_, GetAccounts(_))
        .WillOnce(Return(test_accounts.Clone()));
    manager->SendAccountsRequest(idp_origin, GURL(), client_id,
                                 std::move(callback));
    run_loop.Run();

    return {parsed_accounts_response, parsed_accounts};
  }

  std::tuple<FetchStatus, std::vector<IdentityRequestAccountPtr>>
  SendAccountsRequestAndWaitForResponse(
      const std::string& test_accounts,
      const char* client_id = "",
      net::HttpStatusCode response_code = net::HTTP_OK,
      const std::string& mime_type = "application/json") {
    GURL accounts_endpoint(kTestAccountsEndpoint);
    AddResponse(accounts_endpoint, response_code, mime_type, test_accounts);

    base::RunLoop run_loop;
    FetchStatus parsed_accounts_response;
    std::vector<IdentityRequestAccountPtr> parsed_accounts;
    auto callback = base::BindLambdaForTesting(
        [&](FetchStatus response,
            std::vector<IdentityRequestAccountPtr> accounts) {
          parsed_accounts_response = response;
          parsed_accounts = accounts;
          run_loop.Quit();
        });

    std::unique_ptr<IdpNetworkRequestManager> manager = CreateTestManager();
    manager->SendAccountsRequest(url::Origin::Create(accounts_endpoint),
                                 accounts_endpoint, client_id,
                                 std::move(callback));
    run_loop.Run();

    return {parsed_accounts_response, parsed_accounts};
  }

  IdpNetworkRequestManager::RecordErrorMetricsCallback
  CreateErrorMetricsCallback(base::RunLoop& run_loop) {
    return base::BindLambdaForTesting(
        [&](TokenResponseType token_response_type,
            std::optional<ErrorDialogType> error_dialog_type,
            std::optional<ErrorUrlType> error_url_type) {
          token_response_type_ = token_response_type;
          error_dialog_type_ = error_dialog_type;
          error_url_type_ = error_url_type;
          run_loop.Quit();
        });
  }

  std::tuple<FetchStatus, TokenResult> SendTokenRequestAndWaitForResponse(
      const char* account,
      const char* request,
      net::HttpStatusCode http_status = net::HTTP_OK,
      const std::string& mime_type = "application/json",
      const char* response = R"({"token": "token"})",
      bool idp_blindness = false,
      const char* token_endpoint_str = kTestTokenEndpoint,
      bool cors_error = false) {
    GURL token_endpoint{token_endpoint_str};
    AddResponse(token_endpoint, http_status, mime_type, response, cors_error);

    FetchStatus fetch_status;
    TokenResult token_result;
    base::RunLoop run_loop;
    auto callback =
        base::BindLambdaForTesting([&](FetchStatus status, TokenResult result) {
          fetch_status = status;
          token_result = result;
          run_loop.Quit();
        });

    std::unique_ptr<IdpNetworkRequestManager> manager = CreateTestManager();
    manager->SendTokenRequest(token_endpoint, account, request, idp_blindness,
                              std::move(callback), base::DoNothing(),
                              CreateErrorMetricsCallback(run_loop));
    run_loop.Run();
    return {fetch_status, token_result};
  }

  IdpClientMetadata SendClientMetadataRequestAndWaitForResponse(
      const char* client_id,
      const std::string& response = R"({})",
      const char* top_level_origin = nullptr) {
    GURL client_id_endpoint(kTestClientMetadataEndpoint);
    std::string url_string =
        client_id_endpoint.spec() + "?client_id=" + client_id;
    if (top_level_origin) {
      url_string += std::string("&top_frame_origin=") + top_level_origin;
    }
    AddResponse(GURL(url_string), net::HTTP_OK, "application/json", response);

    IdpClientMetadata data;
    base::RunLoop run_loop;
    auto callback = base::BindLambdaForTesting(
        [&](FetchStatus status, IdpClientMetadata metadata) {
          data = metadata;
          run_loop.Quit();
        });

    std::unique_ptr<IdpNetworkRequestManager> manager =
        CreateTestManager(top_level_origin);
    manager->FetchClientMetadata(
        client_id_endpoint, client_id, kTestBrandIconIdealSize,
        kTestBrandIconMinimumSize, std::move(callback));
    run_loop.Run();
    return data;
  }

  network::TestURLLoaderFactory& test_url_loader_factory() {
    return test_url_loader_factory_;
  }

  base::HistogramTester* histogram_tester() { return &histogram_tester_; }
  TokenResponseType token_response_type() { return token_response_type_; }
  std::optional<ErrorDialogType> error_dialog_type() {
    return error_dialog_type_;
  }
  std::optional<ErrorUrlType> error_url_type() { return error_url_type_; }

 private:
  content::BrowserTaskEnvironment task_environment_{
      content::BrowserTaskEnvironment::MainThreadType::UI};
  network::TestURLLoaderFactory test_url_loader_factory_;
  data_decoder::test::InProcessDataDecoder in_process_data_decoder;
  base::HistogramTester histogram_tester_;
  TokenResponseType token_response_type_;
  std::optional<ErrorDialogType> error_dialog_type_;
  std::optional<ErrorUrlType> error_url_type_;
  std::unique_ptr<NiceMock<MockPermissionDelegate>> test_permission_delegate_;
};

TEST_F(IdpNetworkRequestManagerTest, ParseAccountEmpty) {
  const auto* test_empty_account_json = R"({
  "accounts" : []
  })";

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestAndWaitForResponse(test_empty_account_json);

  EXPECT_EQ(ParseStatus::kEmptyListError, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  EXPECT_TRUE(accounts.empty());

  histogram_tester()->ExpectUniqueSample(
      "Blink.FedCm.Status.AccountsResponseInvalidReason",
      AccountsResponseInvalidReason::kAccountListIsEmpty, 1);
}

TEST_F(IdpNetworkRequestManagerTest, ParseAccountSingle) {
  const auto* test_single_account_json = kSingleAccountEndpointValidJson;

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestAndWaitForResponse(test_single_account_json);

  EXPECT_EQ(ParseStatus::kSuccess, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  EXPECT_EQ(1UL, accounts.size());
  EXPECT_EQ("1234", accounts[0]->id);
}

TEST_F(IdpNetworkRequestManagerTest, ParseAccountMultiple) {
  const auto* test_accounts_json = R"({
  "accounts" : [
    {
      "id" : "1234",
      "email": "ken@idp.test",
      "name": "Ken R. Example",
      "given_name": "Ken",
      "picture": "https://idp.test/profile/1"
    },
    {
      "id" : "5678",
      "email": "sam@idp.test",
      "name": "Sam G. Test",
      "given_name": "Sam",
      "picture": "https://idp.test/profile/2"
    }
  ]
  })";
  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestAndWaitForResponse(test_accounts_json);

  EXPECT_EQ(ParseStatus::kSuccess, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  EXPECT_EQ(2UL, accounts.size());
  EXPECT_EQ("1234", accounts[0]->id);
  EXPECT_EQ("5678", accounts[1]->id);
}

TEST_F(IdpNetworkRequestManagerTest, ParseAccountOptionalFields) {
  // given_name and picture fields are optional
  const auto* test_accounts_json = R"({
  "accounts" : [
    {
      "id" : "1234",
      "email": "ken@idp.test",
      "name": "Ken R. Example"
    }
  ]
  })";

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestAndWaitForResponse(test_accounts_json);

  EXPECT_EQ(ParseStatus::kSuccess, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  EXPECT_EQ("1234", accounts[0]->id);
}

TEST_F(IdpNetworkRequestManagerTest, ParseAccountRequiredFields) {
  {
    base::HistogramTester histogram_tester;
    std::string test_account_missing_account_id_json =
        RemoveAllLinesWithKeyFromJson("id", kSingleAccountEndpointValidJson);
    FetchStatus accounts_response;
    std::vector<IdentityRequestAccountPtr> accounts;
    std::tie(accounts_response, accounts) =
        SendAccountsRequestAndWaitForResponse(
            test_account_missing_account_id_json);

    EXPECT_EQ(ParseStatus::kInvalidResponseError,
              accounts_response.parse_status);
    EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
    EXPECT_TRUE(accounts.empty());
    histogram_tester.ExpectUniqueSample(
        "Blink.FedCm.Status.AccountsResponseInvalidReason",
        AccountsResponseInvalidReason::kAccountMissesRequiredField, 1);
  }
  {
    base::HistogramTester histogram_tester;
    std::string test_account_missing_email_json =
        RemoveAllLinesWithKeyFromJson("email", kSingleAccountEndpointValidJson);
    FetchStatus accounts_response;
    std::vector<IdentityRequestAccountPtr> accounts;
    std::tie(accounts_response, accounts) =
        SendAccountsRequestAndWaitForResponse(test_account_missing_email_json);

    EXPECT_EQ(ParseStatus::kInvalidResponseError,
              accounts_response.parse_status);
    EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
    EXPECT_TRUE(accounts.empty());
    histogram_tester.ExpectUniqueSample(
        "Blink.FedCm.Status.AccountsResponseInvalidReason",
        AccountsResponseInvalidReason::kAccountMissesRequiredField, 1);
  }
  {
    base::HistogramTester histogram_tester;
    std::string test_account_missing_name_json =
        RemoveAllLinesWithKeyFromJson("name", kSingleAccountEndpointValidJson);
    FetchStatus accounts_response;
    std::vector<IdentityRequestAccountPtr> accounts;
    std::tie(accounts_response, accounts) =
        SendAccountsRequestAndWaitForResponse(test_account_missing_name_json);

    EXPECT_EQ(ParseStatus::kInvalidResponseError,
              accounts_response.parse_status);
    EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
    EXPECT_TRUE(accounts.empty());
    histogram_tester.ExpectUniqueSample(
        "Blink.FedCm.Status.AccountsResponseInvalidReason",
        AccountsResponseInvalidReason::kAccountMissesRequiredField, 1);
  }
}

TEST_F(IdpNetworkRequestManagerTest, ParseAccountRequiredFieldNonEmpty) {
  {
    base::HistogramTester histogram_tester;
    const auto* test_accounts_json = R"({
    "accounts" : [
      {
        "id" : "1234",
        "email": "test@email.example",
        "name": "    "
      }
    ]
    })";

    FetchStatus accounts_response;
    std::vector<IdentityRequestAccountPtr> accounts;
    std::tie(accounts_response, accounts) =
        SendAccountsRequestAndWaitForResponse(test_accounts_json);

    EXPECT_EQ(ParseStatus::kInvalidResponseError,
              accounts_response.parse_status);
    EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
    EXPECT_TRUE(accounts.empty());
    histogram_tester.ExpectUniqueSample(
        "Blink.FedCm.Status.AccountsResponseInvalidReason",
        AccountsResponseInvalidReason::kAccountMissesRequiredField, 1);
  }
  {
    base::HistogramTester histogram_tester;
    const auto* test_accounts_json = R"({
    "accounts" : [
      {
        "id" : "1234",
        "email": "",
        "name": "Test User"
      }
    ]
    })";

    FetchStatus accounts_response;
    std::vector<IdentityRequestAccountPtr> accounts;
    std::tie(accounts_response, accounts) =
        SendAccountsRequestAndWaitForResponse(test_accounts_json);

    EXPECT_EQ(ParseStatus::kInvalidResponseError,
              accounts_response.parse_status);
    EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
    EXPECT_TRUE(accounts.empty());
    histogram_tester.ExpectUniqueSample(
        "Blink.FedCm.Status.AccountsResponseInvalidReason",
        AccountsResponseInvalidReason::kAccountMissesRequiredField, 1);
  }
}

// Test that parsing accounts fails if two accounts have the same account id.
TEST_F(IdpNetworkRequestManagerTest, ParseAccountDuplicateIds) {
  const auto* accounts_json = R"({
  "accounts" : [
    {
      "id" : "1234",
      "email": "ken@idp.test",
      "name": "Ken R. Example"
    },
    {
      "id" : "1234",
      "email": "ken@idp.test",
      "name": "Ken R. Example"
    }
  ]
  })";

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestAndWaitForResponse(accounts_json);

  EXPECT_EQ(ParseStatus::kInvalidResponseError, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  EXPECT_TRUE(accounts.empty());
  histogram_tester()->ExpectUniqueSample(
      "Blink.FedCm.Status.AccountsResponseInvalidReason",
      AccountsResponseInvalidReason::kAccountsShareSameId, 1);

  // Test that JSON is valid with exception of duplicate id.
  std::string accounts_json_different_account_ids =
      ReplaceFirstLineWithKeyFromJson("id", R"("id": "5678",)", accounts_json,
                                      /*replace_all=*/false);

  std::tie(accounts_response, accounts) = SendAccountsRequestAndWaitForResponse(
      accounts_json_different_account_ids);
  EXPECT_EQ(ParseStatus::kSuccess, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
}

TEST_F(IdpNetworkRequestManagerTest, ParseAccountPictureUrl) {
  const auto* test_accounts_json = R"({
  "accounts" : [
    {
      "id" : "1234",
      "email": "ken@idp.test",
      "name": "Ken R. Example",
      "picture": "https://idp.test/profile/1234"
    },
    {
      "id" : "567",
      "email": "sam@idp.test",
      "name": "Sam R. Example",
      "picture": "invalid_url"
    }
  ]
  })";

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestAndWaitForResponse(test_accounts_json);

  EXPECT_EQ(ParseStatus::kSuccess, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  EXPECT_TRUE(accounts[0]->picture.is_valid());
  EXPECT_EQ(GURL("https://idp.test/profile/1234"), accounts[0]->picture);
  EXPECT_FALSE(accounts[1]->picture.is_valid());
}

TEST_F(IdpNetworkRequestManagerTest, ParseAccountUnicode) {
  auto TestAccountWithKeyValue = [](const std::string& key,
                                    const std::string& value) {
    static constexpr char kJson[] = R"({
      "accounts" : [
        {
          "id" : "1234",
          "email": "ken@idp.test",
          "%s": "%s"
        }
      ]
    })";
    return base::StringPrintf(kJson, key.c_str(), value.c_str());
  };

  std::array<std::string, 3> test_values{"ascii", "🦖", "مجید"};

  for (auto& test_value : test_values) {
    const auto& accounts_json = TestAccountWithKeyValue("name", test_value);

    FetchStatus accounts_response;
    std::vector<IdentityRequestAccountPtr> accounts;
    std::tie(accounts_response, accounts) =
        SendAccountsRequestAndWaitForResponse(accounts_json.c_str());

    EXPECT_EQ(1UL, accounts.size());
    EXPECT_EQ(test_value, accounts[0]->display_name);
  }
}

TEST_F(IdpNetworkRequestManagerTest, ParseAccountInvalid) {
  const auto* test_invalid_account_json = "{}";

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestAndWaitForResponse(test_invalid_account_json);

  EXPECT_EQ(ParseStatus::kInvalidResponseError, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  EXPECT_TRUE(accounts.empty());
  histogram_tester()->ExpectUniqueSample(
      "Blink.FedCm.Status.AccountsResponseInvalidReason",
      AccountsResponseInvalidReason::kNoAccountsKey, 1);
}

TEST_F(IdpNetworkRequestManagerTest, ParseAccountMalformed) {
  const auto* test_invalid_account_json = "malformed_json";

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestAndWaitForResponse(test_invalid_account_json);

  EXPECT_EQ(ParseStatus::kInvalidResponseError, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  EXPECT_TRUE(accounts.empty());
  histogram_tester()->ExpectUniqueSample(
      "Blink.FedCm.Status.AccountsResponseInvalidReason",
      AccountsResponseInvalidReason::kResponseIsNotJsonOrDict, 1);
}

TEST_F(IdpNetworkRequestManagerTest, ParseAccountLabelsOldSyntax) {
  base::test::ScopedFeatureList list;
  list.InitAndDisableFeature(features::kFedCmUseOtherAccountAndLabelsNewSyntax);

  // New syntax should be ignored with the flag disabled.
  const auto* test_accounts_json = R"({
  "accounts" : [
    {
      "id": "1234",
      "email": "ken@idp.test",
      "name": "Ken R. Example",
      "label_hints": ["x1", 42, "x2"],
      "labels": ["l1", 42, "l2"]
    }
  ]
  })";

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestAndWaitForResponse(test_accounts_json);

  EXPECT_EQ(ParseStatus::kSuccess, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  EXPECT_EQ("1234", accounts[0]->id);
  // The integer in the second position should be ignored.
  ASSERT_EQ(2u, accounts[0]->labels.size());
  EXPECT_EQ("l1", accounts[0]->labels[0]);
  EXPECT_EQ("l2", accounts[0]->labels[1]);
}

TEST_F(IdpNetworkRequestManagerTest, ParseAccountLabelsOldAndNewSyntax) {
  base::test::ScopedFeatureList list;
  list.InitAndEnableFeature(features::kFedCmUseOtherAccountAndLabelsNewSyntax);

  // label_hints should take precedence.
  const auto* test_accounts_json = R"({
  "accounts" : [
    {
      "id": "1234",
      "email": "ken@idp.test",
      "name": "Ken R. Example",
      "label_hints": ["l1", 42, "l2"],
      "labels": ["x1", 42, "x2"]
    }
  ]
  })";

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestAndWaitForResponse(test_accounts_json);

  ASSERT_EQ(ParseStatus::kSuccess, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  EXPECT_EQ("1234", accounts[0]->id);
  // The integer in the second position should be ignored.
  ASSERT_EQ(2u, accounts[0]->labels.size());
  EXPECT_EQ("l1", accounts[0]->labels[0]);
  EXPECT_EQ("l2", accounts[0]->labels[1]);
}

// TODO(crbug.com/404568028): Delete when
// kFedCmUseOtherAccountAndLabelsNewSyntax is removed.
TEST_F(IdpNetworkRequestManagerTest, DoNotParseAccountLabelsOldSyntaxWithFlag) {
  base::test::ScopedFeatureList list;
  list.InitAndEnableFeature(features::kFedCmUseOtherAccountAndLabelsNewSyntax);

  // With new syntax enabled, old syntax should be ignored.
  const auto* test_accounts_json = R"({
  "accounts" : [
    {
      "id": "1234",
      "email": "ken@idp.test",
      "name": "Ken R. Example",
      "labels": ["x1", 42, "x2"]
    }
  ]
  })";

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestAndWaitForResponse(test_accounts_json);

  ASSERT_EQ(ParseStatus::kSuccess, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  EXPECT_EQ("1234", accounts[0]->id);
  ASSERT_EQ(0u, accounts[0]->labels.size());
}

TEST_F(IdpNetworkRequestManagerTest, ParseAccountLabelHints) {
  base::test::ScopedFeatureList list;
  list.InitAndEnableFeature(features::kFedCmUseOtherAccountAndLabelsNewSyntax);

  const auto* test_accounts_json = R"({
  "accounts" : [
    {
      "id": "1234",
      "email": "ken@idp.test",
      "name": "Ken R. Example",
      "label_hints": ["l1", 42, "l2"]
    }
  ]
  })";

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestAndWaitForResponse(test_accounts_json);

  EXPECT_EQ(ParseStatus::kSuccess, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  EXPECT_EQ("1234", accounts[0]->id);
  // The integer in the second position should be ignored.
  ASSERT_EQ(2u, accounts[0]->labels.size());
  EXPECT_EQ("l1", accounts[0]->labels[0]);
  EXPECT_EQ("l2", accounts[0]->labels[1]);
}

TEST_F(IdpNetworkRequestManagerTest, ComputeWellKnownUrl) {
  EXPECT_EQ("https://localhost:8000/.well-known/web-identity",
            IdpNetworkRequestManager::ComputeWellKnownUrl(
                GURL("https://localhost:8000/test/"))
                ->spec());

  EXPECT_EQ("https://google.com/.well-known/web-identity",
            IdpNetworkRequestManager::ComputeWellKnownUrl(
                GURL("https://www.google.com:8000/test/"))
                ->spec());

  EXPECT_EQ(std::nullopt, IdpNetworkRequestManager::ComputeWellKnownUrl(
                              GURL("https://192.101.0.1/test/")));
}

TEST_F(IdpNetworkRequestManagerTest, ParseUsername) {
  base::test::ScopedFeatureList list;
  list.InitAndEnableFeature(features::kFedCmAlternativeIdentifiers);

  const auto* test_accounts_json = R"({
  "accounts" : [
    {
      "id": "1234",
      "email": "ken@idp.test",
      "username": "ken"
    }
  ]
  })";

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestAndWaitForResponse(test_accounts_json);

  ASSERT_EQ(ParseStatus::kSuccess, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  EXPECT_EQ("1234", accounts[0]->id);
  EXPECT_EQ("ken@idp.test", accounts[0]->email);
  EXPECT_EQ("ken@idp.test", accounts[0]->display_identifier);
  EXPECT_EQ("ken", accounts[0]->display_name);
}

TEST_F(IdpNetworkRequestManagerTest, ParsePhoneNumber) {
  base::test::ScopedFeatureList list;
  list.InitAndEnableFeature(features::kFedCmAlternativeIdentifiers);

  const auto* test_accounts_json = R"({
  "accounts" : [
    {
      "id": "1234",
      "tel": "111-111-1111"
    }
  ]
  })";

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestAndWaitForResponse(test_accounts_json);

  ASSERT_EQ(ParseStatus::kSuccess, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  EXPECT_EQ("1234", accounts[0]->id);
  EXPECT_EQ("", accounts[0]->email);
  EXPECT_EQ("", accounts[0]->display_identifier);
  EXPECT_EQ("111-111-1111", accounts[0]->display_name);
}

TEST_F(IdpNetworkRequestManagerTest, ParseAccountSingleLightweightFedcm) {
  base::test::ScopedFeatureList list;
  list.InitAndEnableFeature(features::kFedCmLightweightMode);

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) = SendAccountsRequestWithStoredAccounts(
      base::Value::List().Append(base::Value::Dict()
                                     .Set("id", "1234")
                                     .Set("email", "ken@idp.test")
                                     .Set("name", "Ken R. Example")));

  EXPECT_EQ(ParseStatus::kSuccess, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  EXPECT_EQ(1UL, accounts.size());

  EXPECT_EQ("1234", accounts[0]->id);
  EXPECT_EQ("ken@idp.test", accounts[0]->email);
  EXPECT_EQ("Ken R. Example", accounts[0]->name);
}

TEST_F(IdpNetworkRequestManagerTest, ParseAccountEmptyLightweightFedcm) {
  base::test::ScopedFeatureList list;
  list.InitAndEnableFeature(features::kFedCmLightweightMode);

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestWithStoredAccounts(base::Value::List());

  EXPECT_EQ(ParseStatus::kEmptyListError, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  EXPECT_EQ(0UL, accounts.size());
}

TEST_F(IdpNetworkRequestManagerTest, CacheProfilePictures) {
  std::unique_ptr<IdpNetworkRequestManager> manager = CreateTestManager();
  url::Origin idp_origin = url::Origin::Create(GURL(kTestIdpUrl));
  GURL picture_url("https://idp.cdn.test/profile/1234");

  manager->CacheAccountPictures(idp_origin, {picture_url});
  EXPECT_EQ(1, test_url_loader_factory().NumPending());
  network::TestURLLoaderFactory::PendingRequest* pending_request =
      test_url_loader_factory().GetPendingRequest(0);
  ASSERT_TRUE(pending_request);
  network::ResourceRequest resource_request = pending_request->request;
  EXPECT_FALSE(resource_request.load_flags & net::LOAD_ONLY_FROM_CACHE);
  net::IsolationInfo expected_isolation_info = net::IsolationInfo::Create(
      net::IsolationInfo::RequestType::kOther,
      /*top_frame_origin=*/idp_origin,
      /*frame_origin=*/url::Origin::Create(GURL("https://idp.cdn.test/")),
      net::SiteForCookies(),
      /*nonce=*/std::nullopt,
      net::NetworkIsolationPartition::kFedCmUncredentialedRequests);
  EXPECT_TRUE(expected_isolation_info.IsEqualForTesting(
      resource_request.trusted_params->isolation_info));
}

TEST_F(IdpNetworkRequestManagerTest, DownloadAndDecodeCachedImage) {
  std::unique_ptr<IdpNetworkRequestManager> manager = CreateTestManager();
  url::Origin idp_origin = url::Origin::Create(GURL(kTestIdpUrl));
  GURL picture_url("https://idp.cdn.test/profile/1234");

  manager->DownloadAndDecodeCachedImage(
      idp_origin, picture_url, base::DoNothingAs<void(const gfx::Image&)>());

  EXPECT_EQ(1, test_url_loader_factory().NumPending());
  network::TestURLLoaderFactory::PendingRequest* pending_request =
      test_url_loader_factory().GetPendingRequest(0);
  ASSERT_TRUE(pending_request);
  network::ResourceRequest resource_request = pending_request->request;
  EXPECT_TRUE(resource_request.load_flags & net::LOAD_ONLY_FROM_CACHE);
  net::IsolationInfo expected_isolation_info = net::IsolationInfo::Create(
      net::IsolationInfo::RequestType::kOther,
      /*top_frame_origin=*/idp_origin,
      /*frame_origin=*/url::Origin::Create(GURL("https://idp.cdn.test/")),
      net::SiteForCookies(),
      /*nonce=*/std::nullopt,
      net::NetworkIsolationPartition::kFedCmUncredentialedRequests);
  EXPECT_TRUE(expected_isolation_info.IsEqualForTesting(
      resource_request.trusted_params->isolation_info));
}
// Test that IdpNetworkRequestManager::FetchWellKnown() fails when the
// identity provider domain is empty.
TEST_F(IdpNetworkRequestManagerTest, FetchWellKnownIllegalDomainFails) {
  GURL illegal_idp_url("https://192.101.0.1/test/");

  network::TestURLLoaderFactory test_url_loader_factory;
  std::unique_ptr<MockPermissionDelegate> test_permission_delegate_ =
      std::make_unique<MockPermissionDelegate>();

  auto network_manager = std::make_unique<IdpNetworkRequestManager>(
      url::Origin::Create(GURL(kTestRpUrl)),
      /*rp_embedding_origin=*/url::Origin(),
      base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
          &test_url_loader_factory),
      test_permission_delegate_.get(),
      network::mojom::ClientSecurityState::New(), content::FrameTreeNodeId());

  base::RunLoop run_loop;
  auto callback = base::BindLambdaForTesting(
      [&](FetchStatus fetch_status,
          const IdpNetworkRequestManager::WellKnown& well_known) {
        EXPECT_EQ(ParseStatus::kHttpNotFoundError, fetch_status.parse_status);
        // We receive OK here because
        // IdpNetworkRequestManager::ComputeWellKnownUrl() fails.
        EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
        run_loop.Quit();
      });
  network_manager->FetchWellKnown(illegal_idp_url, std::move(callback));
  run_loop.Run();

  // Well-known download should not have been attempted.
  EXPECT_EQ(0, test_url_loader_factory.NumPending());
}

TEST_F(IdpNetworkRequestManagerTest, ParseWellKnown) {
  FetchStatus fetch_status;
  IdpNetworkRequestManager::WellKnown well_known;

  std::tie(fetch_status, well_known) =
      SendWellKnownRequestAndWaitForResponse(R"({
  "provider_urls": ["https://idp.test/fedcm.json"]
  })");
  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(std::set<GURL>{GURL("https://idp.test/fedcm.json")},
            well_known.provider_urls);

  std::tie(fetch_status, well_known) =
      SendWellKnownRequestAndWaitForResponse(R"({
  "provider_urls": ["https://idp.test/path/fedcm.json"]
  })");
  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(std::set<GURL>{GURL("https://idp.test/path/fedcm.json")},
            well_known.provider_urls);

  // Value not a list
  std::tie(fetch_status, well_known) =
      SendWellKnownRequestAndWaitForResponse(R"({
  "provider_urls": "https://idp.test/fedcm.json"
  })");
  EXPECT_EQ(ParseStatus::kInvalidResponseError, fetch_status.parse_status);

  // Toplevel not a dictionary
  std::tie(fetch_status, well_known) =
      SendWellKnownRequestAndWaitForResponse(R"(
  ["https://idp.test/fedcm.json"]
  )");
  EXPECT_EQ(ParseStatus::kInvalidResponseError, fetch_status.parse_status);

  // Incorrect key
  std::tie(fetch_status, well_known) =
      SendWellKnownRequestAndWaitForResponse(R"({
  "providers": ["https://idp.test/fedcm.json"]
  })");
  EXPECT_EQ(ParseStatus::kInvalidResponseError, fetch_status.parse_status);

  // Array entry not a string
  std::tie(fetch_status, well_known) =
      SendWellKnownRequestAndWaitForResponse(R"({
  "provider_urls": [1]
  })");
  EXPECT_EQ(ParseStatus::kInvalidResponseError, fetch_status.parse_status);

  // Relative URLs
  std::tie(fetch_status, well_known) =
      SendWellKnownRequestAndWaitForResponse(R"({
  "provider_urls": ["/fedcm.json"]
  })");
  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(std::set<GURL>{GURL("https://idp.test/fedcm.json")},
            well_known.provider_urls);

  std::tie(fetch_status, well_known) =
      SendWellKnownRequestAndWaitForResponse(R"({
  "provider_urls": ["fedcm.json"]
  })");
  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(std::set<GURL>{GURL("https://idp.test/.well-known/fedcm.json")},
            well_known.provider_urls);

  // Empty well known list
  std::tie(fetch_status, well_known) =
      SendWellKnownRequestAndWaitForResponse(R"({
  "provider_urls": []
  })");
  EXPECT_EQ(ParseStatus::kEmptyListError, fetch_status.parse_status);

  // well-known file having valid account endpoints,
  // login url and provider_urls
  std::tie(fetch_status, well_known) =
      SendWellKnownRequestAndWaitForResponse(R"({
  "accounts_endpoint": "/accounts.php",
  "login_url": "/login",
  "provider_urls": ["https://idp.test/path/fedcm.json"]
  })");
  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(GURL("https://idp.test/accounts.php"), well_known.accounts);
  EXPECT_EQ(GURL("https://idp.test/login"), well_known.login_url);
  EXPECT_EQ(std::set<GURL>{GURL("https://idp.test/path/fedcm.json")},
            well_known.provider_urls);

  // well-known file having empty provider_urls and
  // valid account endpoints and login url
  std::tie(fetch_status, well_known) =
      SendWellKnownRequestAndWaitForResponse(R"({
  "accounts_endpoint": "/accounts.php",
  "login_url": "/login"
  })");
  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(GURL("https://idp.test/accounts.php"), well_known.accounts);
  EXPECT_EQ(GURL("https://idp.test/login"), well_known.login_url);
  EXPECT_TRUE(well_known.provider_urls.empty());

  // well-known file having empty account endpoints and valid
  // login url and provider_urls
  std::tie(fetch_status, well_known) =
      SendWellKnownRequestAndWaitForResponse(R"({
  "accounts_endpoint": "",
  "login_url": "/login",
  "provider_urls": ["https://idp.test/path/fedcm.json"]
  })");
  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_TRUE(well_known.accounts.is_empty());
  EXPECT_EQ(GURL("https://idp.test/login"), well_known.login_url);
  EXPECT_EQ(std::set<GURL>{GURL("https://idp.test/path/fedcm.json")},
            well_known.provider_urls);

  // well-known file having empty login url and valid
  // account endpoints and provider_urls
  std::tie(fetch_status, well_known) =
      SendWellKnownRequestAndWaitForResponse(R"({
  "accounts_endpoint": "/accounts.php",
  "login_url": "",
  "provider_urls": ["https://idp.test/path/fedcm.json"]
  })");
  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(GURL("https://idp.test/accounts.php"), well_known.accounts);
  EXPECT_TRUE(well_known.login_url.is_empty());
  EXPECT_EQ(std::set<GURL>{GURL("https://idp.test/path/fedcm.json")},
            well_known.provider_urls);

  // well-known file having valid provider urls with empty
  // login url and account endpoints
  std::tie(fetch_status, well_known) =
      SendWellKnownRequestAndWaitForResponse(R"({
  "accounts_endpoint": "",
  "login_url": "",
  "provider_urls": ["https://idp.test/path/fedcm.json"]
  })");
  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_TRUE(well_known.accounts.is_empty());
  EXPECT_TRUE(well_known.login_url.is_empty());
  EXPECT_EQ(std::set<GURL>{GURL("https://idp.test/path/fedcm.json")},
            well_known.provider_urls);
}

// Test that the "alpha" value in the "branding" JSON is ignored.
TEST_F(IdpNetworkRequestManagerTest, ParseConfigBrandingRemoveAlpha) {
  const char test_json[] = R"({
  "branding" : {
    "background_color": "#20202020"
  }
  })";

  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) =
      SendConfigRequestAndWaitForResponse(test_json);

  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ(SkColorSetARGB(0xff, 0x20, 0x20, 0x20),
            idp_metadata.brand_background_color);
}

TEST_F(IdpNetworkRequestManagerTest, ParseConfigBrandingInvalidColor) {
  const char test_json[] = R"({
  "branding" : {
    "background_color": "fake_color"
  }
  })";

  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) =
      SendConfigRequestAndWaitForResponse(test_json);

  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ(std::nullopt, idp_metadata.brand_background_color);
}

TEST_F(IdpNetworkRequestManagerTest,
       ParseConfigWithInsufficientContrastTextColor) {
  const char test_json[] = R"({
  "branding" : {
    "background_color": "#000000",
    "color": "#010101"
  }
  })";

  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) =
      SendConfigRequestAndWaitForResponse(test_json);

  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ(SkColorSetRGB(0, 0, 0), idp_metadata.brand_background_color);
  EXPECT_EQ(SkColorSetRGB(1, 1, 1), idp_metadata.brand_text_color);
}

TEST_F(IdpNetworkRequestManagerTest,
       ParseConfigBrandingWithTextColorAndNoBackgroundColor) {
  const char test_json[] = R"({
  "branding" : {
    "color": "blue"
  }
  })";

  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) =
      SendConfigRequestAndWaitForResponse(test_json);

  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ(std::nullopt, idp_metadata.brand_background_color);
  EXPECT_EQ(SkColorSetRGB(0, 0, 255), idp_metadata.brand_text_color);
}

TEST_F(IdpNetworkRequestManagerTest, ParseConfigBrandingSelectBestSize) {
  const char test_json[] = R"({
  "branding" : {
    "icons": [
      {
        "url": "https://example.com/10.png",
        "size": 10
      },
      {
        "url": "https://example.com/16.png",
        "size": 16
      },
      {
        "url": "https://example.com/31.png",
        "size": 31
      },
      {
        "url": "https://example.com/32.png",
        "size": 32
      },
      {
        "url": "https://example.com/33.png",
        "size": 33
      }
    ]
  }
  })";

  ASSERT_EQ(32, kTestBrandIconIdealSize);

  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) =
      SendConfigRequestAndWaitForResponse(test_json);

  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ("https://example.com/32.png", idp_metadata.brand_icon_url.spec());
}

// Test that the icon is rejected if there is an explicit brand icon size in the
// config and it is smaller than the `idp_brand_icon_minimum_size` parameter
// passed to IdpNetworkRequestManager::FetchConfig().
TEST_F(IdpNetworkRequestManagerTest, ParseConfigBrandingMinSize) {
  ASSERT_EQ(16, kTestBrandIconMinimumSize);

  {
    const char test_json[] = R"({
    "branding" : {
      "icons": [
        {
          "url": "https://example.com/15.png",
          "size": 15
        }
      ]
    }
    })";

    FetchStatus fetch_status;
    IdentityProviderMetadata idp_metadata;
    std::tie(fetch_status, idp_metadata) =
        SendConfigRequestAndWaitForResponse(test_json);

    EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
    EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
    EXPECT_EQ(GURL(), idp_metadata.brand_icon_url);
  }

  {
    const char test_json[] = R"({
    "branding" : {
      "icons": [
        {
          "url": "https://example.com/16.png",
          "size": 16
        }
      ]
    }
    })";

    FetchStatus fetch_status;
    IdentityProviderMetadata idp_metadata;
    std::tie(fetch_status, idp_metadata) =
        SendConfigRequestAndWaitForResponse(test_json);

    EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
    EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
    EXPECT_EQ("https://example.com/16.png", idp_metadata.brand_icon_url.spec());
  }
}

// Tests various scenarios on resolving branding icon's url for given config
// url.
TEST_F(IdpNetworkRequestManagerTest, ParseConfigBrandingIconReltivePath) {
  // branding icon url domain matches with config url domain
  {
    const char test_json[] = R"({
    "branding" : {
      "icons": [
        {
          "url": "/16.png",
          "size": 16
        }
      ]
    }
    })";

    FetchStatus fetch_status;
    IdentityProviderMetadata idp_metadata;
    std::tie(fetch_status, idp_metadata) =
        SendConfigRequestAndWaitForResponse(test_json);

    EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
    EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
    EXPECT_EQ("https://idp.test/16.png", idp_metadata.brand_icon_url);
  }

  // branding icon url domain doesnt match with config url domain
  {
    const char test_json[] = R"({
    "branding" : {
      "icons": [
        {
          "url": "https://example.com/16.png",
          "size": 16
        }
      ]
    }
    })";

    FetchStatus fetch_status;
    IdentityProviderMetadata idp_metadata;
    std::tie(fetch_status, idp_metadata) =
        SendConfigRequestAndWaitForResponse(test_json);

    EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
    EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
    EXPECT_EQ("https://example.com/16.png", idp_metadata.brand_icon_url);
  }
}

TEST_F(IdpNetworkRequestManagerTest,
       ParseConfigSupportsOtherAccountActiveMode) {
  base::test::ScopedFeatureList list;
  list.InitAndDisableFeature(features::kFedCmUseOtherAccountAndLabelsNewSyntax);

  const char test_json[] = R"({
  "modes": {
    "active": {
      "supports_use_other_account": true
    }
  }
  })";

  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) = SendConfigRequestAndWaitForResponse(
      test_json, net::HTTP_OK, "application/json",
      blink::mojom::RpMode::kActive);

  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ(true, idp_metadata.supports_add_account);
}

TEST_F(IdpNetworkRequestManagerTest,
       ParseConfigSupportsOtherAccountPassiveMode) {
  base::test::ScopedFeatureList list;
  list.InitAndDisableFeature(features::kFedCmUseOtherAccountAndLabelsNewSyntax);

  // The toplevel field should be ignored with the flag disabled.
  const char test_json[] = R"({
  "supports_use_other_account": false,
  "modes": {
    "passive": {
      "supports_use_other_account": true
    }
  }
  })";

  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) = SendConfigRequestAndWaitForResponse(
      test_json, net::HTTP_OK, "application/json",
      blink::mojom::RpMode::kPassive);

  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ(true, idp_metadata.supports_add_account);
}

TEST_F(IdpNetworkRequestManagerTest,
       ParseConfigSupportsOtherAccountOldAndNewSyntax) {
  base::test::ScopedFeatureList list;
  list.InitAndEnableFeature(features::kFedCmUseOtherAccountAndLabelsNewSyntax);

  // The toplevel field should take precedence.
  const char test_json[] = R"({
  "supports_use_other_account": true,
  "modes": {
    "passive": {
      "supports_use_other_account": false
    }
  }
  })";

  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) = SendConfigRequestAndWaitForResponse(
      test_json, net::HTTP_OK, "application/json",
      blink::mojom::RpMode::kPassive);

  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ(true, idp_metadata.supports_add_account);
}

// TODO(crbug.com/404568028): Delete when
// kFedCmUseOtherAccountAndLabelsNewSyntax is removed.
TEST_F(IdpNetworkRequestManagerTest,
       DoNotParseConfigSupportsOtherAccountOldSyntaxWithFlag) {
  base::test::ScopedFeatureList list;
  list.InitAndEnableFeature(features::kFedCmUseOtherAccountAndLabelsNewSyntax);

  // Old syntax should be ignored if new syntax is enabled.
  const char test_json[] = R"({
  "modes": {
    "passive": {
      "supports_use_other_account": true
    }
  }
  })";

  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) = SendConfigRequestAndWaitForResponse(
      test_json, net::HTTP_OK, "application/json",
      blink::mojom::RpMode::kPassive);

  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ(false, idp_metadata.supports_add_account);
}

TEST_F(IdpNetworkRequestManagerTest, ParseConfigSupportsOtherAccountNewSyntax) {
  base::test::ScopedFeatureList list;
  list.InitAndEnableFeature(features::kFedCmUseOtherAccountAndLabelsNewSyntax);

  const char test_json[] = R"({
  "supports_use_other_account": true
  })";

  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) = SendConfigRequestAndWaitForResponse(
      test_json, net::HTTP_OK, "application/json",
      blink::mojom::RpMode::kPassive);

  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ(true, idp_metadata.supports_add_account);
}

TEST_F(IdpNetworkRequestManagerTest,
       ParseConfigSupportsOtherAccountDifferentMode) {
  const char test_json[] = R"({
  "modes": {
    "active": {
      "supports_use_other_account": true
    }
  }
  })";

  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) = SendConfigRequestAndWaitForResponse(
      test_json, net::HTTP_OK, "application/json",
      blink::mojom::RpMode::kPassive);

  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ(false, idp_metadata.supports_add_account);
}

TEST_F(IdpNetworkRequestManagerTest, ParseConfigSupportsOtherAccountBothModes) {
  const char test_json[] = R"({
  "modes": {
    "active": {
      "supports_use_other_account": false
    },
    "passive": {
      "supports_use_other_account": true
    }
  }
  })";

  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) = SendConfigRequestAndWaitForResponse(
      test_json, net::HTTP_OK, "application/json",
      blink::mojom::RpMode::kActive);

  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ(false, idp_metadata.supports_add_account);
}

TEST_F(IdpNetworkRequestManagerTest,
       ParseConfigSupportsUseOtherAccountMissing) {
  const char test_json[] = R"({
  })";

  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) =
      SendConfigRequestAndWaitForResponse(test_json);

  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ(false, idp_metadata.supports_add_account);
}

TEST_F(IdpNetworkRequestManagerTest, ParseConfigRequestedLabelOldSyntax) {
  base::test::ScopedFeatureList list;
  list.InitAndDisableFeature(features::kFedCmUseOtherAccountAndLabelsNewSyntax);

  // New syntax should be ignored with flag disabled.
  const char test_json[] = R"({
    "account_label": "l1",
    "accounts": {
      "include": "l1"
    }
  })";

  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) =
      SendConfigRequestAndWaitForResponse(test_json);

  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ("l1", idp_metadata.requested_label);
}

TEST_F(IdpNetworkRequestManagerTest, ParseConfigRequestedLabelOldAndNewSyntax) {
  base::test::ScopedFeatureList list;
  list.InitAndEnableFeature(features::kFedCmUseOtherAccountAndLabelsNewSyntax);

  // New syntax should take precedence over old syntax.
  const char test_json[] = R"({
    "account_label": "l1",
    "accounts": {
      "include": "l5"
    }
  })";

  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) =
      SendConfigRequestAndWaitForResponse(test_json);

  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ("l1", idp_metadata.requested_label);
}

// TODO(crbug.com/404568028): Delete when
// kFedCmUseOtherAccountAndLabelsNewSyntax is removed.
TEST_F(IdpNetworkRequestManagerTest,
       DoNotParseConfigRequestedLabelOldSyntaxWithFlag) {
  base::test::ScopedFeatureList list;
  list.InitAndEnableFeature(features::kFedCmUseOtherAccountAndLabelsNewSyntax);

  // Old syntax should be ignored if new syntax is enabled.
  const char test_json[] = R"({
    "accounts": {
      "include": "l5"
    }
  })";

  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) =
      SendConfigRequestAndWaitForResponse(test_json);

  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ("", idp_metadata.requested_label);
}

TEST_F(IdpNetworkRequestManagerTest, ParseConfigRequestedLabel) {
  base::test::ScopedFeatureList list;
  list.InitAndEnableFeature(features::kFedCmUseOtherAccountAndLabelsNewSyntax);

  const char test_json[] = R"({
    "account_label": "l1"
  })";

  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) =
      SendConfigRequestAndWaitForResponse(test_json);

  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ("l1", idp_metadata.requested_label);
}

// Tests that we send the correct origin for account requests.
TEST_F(IdpNetworkRequestManagerTest, AccountRequestOrigin) {
  bool called = false;
  auto interceptor =
      base::BindLambdaForTesting([&](const network::ResourceRequest& request) {
        called = true;
        EXPECT_EQ(GURL(kTestAccountsEndpoint), request.url);
        EXPECT_EQ(request.request_body, nullptr);
        EXPECT_FALSE(request.referrer.is_valid());
        EXPECT_FALSE(
            request.headers.HasHeader(net::HttpRequestHeaders::kOrigin));
      });
  test_url_loader_factory().SetInterceptor(interceptor);

  const char test_accounts_json[] = R"({
  "accounts" : [
    {
      "id" : "1234",
      "email": "ken@idp.test",
      "name": "Ken R. Example"
    }
  ]
  })";

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestAndWaitForResponse(test_accounts_json);

  ASSERT_TRUE(called);
  EXPECT_EQ(ParseStatus::kSuccess, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
}

// Verifies that we correctly check the signed-in status.
TEST_F(IdpNetworkRequestManagerTest, AccountSignedInStatus) {
  bool called = false;
  auto interceptor =
      base::BindLambdaForTesting([&](const network::ResourceRequest& request) {
        called = true;
        EXPECT_EQ(GURL(kTestAccountsEndpoint), request.url);
        EXPECT_EQ(request.request_body, nullptr);
        EXPECT_FALSE(request.referrer.is_valid());
        EXPECT_FALSE(
            request.headers.HasHeader(net::HttpRequestHeaders::kOrigin));
      });
  test_url_loader_factory().SetInterceptor(interceptor);

  const char test_accounts_json[] = R"({
  "accounts" : [
    {
      "id" : "1",
      "email": "ken@idp.test",
      "name": "Ken R. Example",
      "approved_clients": ["xxx"]
    },
    {
      "id" : "2",
      "email": "jim@idp.test",
      "name": "Jim R. Example",
      "approved_clients": []
    },
    {
      "id" : "3",
      "email": "rashida@idp.test",
      "name": "Rashida R. Example",
      "approved_clients": ["yyy"]
    },
    {
      "id" : "4",
      "email": "wei@idp.test",
      "name": "Wei R. Example"
    },
    {
      "id" : "5",
      "email": "hans@idp.test",
      "name": "Hans R. Example",
      "approved_clients": ["xxx", "yyy"]
    }
  ]
  })";

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestAndWaitForResponse(test_accounts_json, "xxx");

  EXPECT_TRUE(called);
  EXPECT_EQ(ParseStatus::kSuccess, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  ASSERT_EQ(5ul, accounts.size());
  ASSERT_TRUE(accounts[0]->login_state.has_value());
  EXPECT_EQ(LoginState::kSignIn, *accounts[0]->login_state);
  ASSERT_TRUE(accounts[1]->login_state.has_value());
  EXPECT_EQ(LoginState::kSignUp, *accounts[1]->login_state);
  ASSERT_TRUE(accounts[2]->login_state.has_value());
  EXPECT_EQ(LoginState::kSignUp, *accounts[2]->login_state);
  EXPECT_FALSE(accounts[3]->login_state.has_value());
  ASSERT_TRUE(accounts[4]->login_state.has_value());
  EXPECT_EQ(LoginState::kSignIn, *accounts[4]->login_state);
}

// Tests the token request implementation.
TEST_F(IdpNetworkRequestManagerTest, IdAssertionRequest) {
  bool called = false;
  auto interceptor =
      base::BindLambdaForTesting([&](const network::ResourceRequest& request) {
        called = true;
        EXPECT_EQ(GURL(kTestTokenEndpoint), request.url);
        EXPECT_FALSE(request.referrer.is_valid());
        EXPECT_EQ(url::Origin::Create(GURL(kTestRpUrl)),
                  GetOriginHeader(request));

        // Check that the request body is correct (should be "request")
        ASSERT_NE(request.request_body, nullptr);
        ASSERT_EQ(1ul, request.request_body->elements()->size());
        const network::DataElement& elem =
            request.request_body->elements()->at(0);
        ASSERT_EQ(network::DataElement::Tag::kBytes, elem.type());
        const network::DataElementBytes& byte_elem =
            elem.As<network::DataElementBytes>();
        EXPECT_EQ("request", byte_elem.AsStringPiece());
      });
  test_url_loader_factory().SetInterceptor(interceptor);
  FetchStatus fetch_status;
  TokenResult token_result;
  std::tie(fetch_status, token_result) =
      SendTokenRequestAndWaitForResponse("account", "request");
  ASSERT_TRUE(called);
  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  ASSERT_EQ("token", token_result.token);
  ASSERT_EQ(false, fetch_status.cors_error);
}

// Tests the ID assertion request implementation when CORS is enforced on the
// endpoint.
TEST_F(IdpNetworkRequestManagerTest, IdAssertionRequestWithCORS) {
  bool called = false;
  auto interceptor =
      base::BindLambdaForTesting([&](const network::ResourceRequest& request) {
        called = true;
        EXPECT_EQ(GURL(kTestTokenEndpoint), request.url);
        EXPECT_FALSE(request.referrer.is_valid());
        url::Origin rpOrigin = url::Origin::Create(GURL(kTestRpUrl));
        EXPECT_EQ(GetOriginHeader(request), rpOrigin);

        // Check that the request body is correct (should be "request")
        ASSERT_NE(request.request_body, nullptr);
        ASSERT_EQ(1ul, request.request_body->elements()->size());
        const network::DataElement& elem =
            request.request_body->elements()->at(0);
        ASSERT_EQ(network::DataElement::Tag::kBytes, elem.type());
        const network::DataElementBytes& byte_elem =
            elem.As<network::DataElementBytes>();
        EXPECT_EQ("request", byte_elem.AsStringPiece());
        ASSERT_EQ(request.mode, network::mojom::RequestMode::kCors);
        ASSERT_EQ(request.request_initiator, rpOrigin);
      });
  test_url_loader_factory().SetInterceptor(interceptor);
  FetchStatus fetch_status;
  TokenResult token_result;
  std::tie(fetch_status, token_result) =
      SendTokenRequestAndWaitForResponse("account", "request");
  ASSERT_TRUE(called);
  EXPECT_EQ(ParseStatus::kSuccess, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  ASSERT_EQ("token", token_result.token);
  ASSERT_EQ(false, fetch_status.cors_error);
}

// Tests the ID assertion request implementation when CORS is enforced on the
// endpoint and server responds with CORS Error
TEST_F(IdpNetworkRequestManagerTest, IdAssertionRequestWithCORSError) {
  FetchStatus fetch_status;
  TokenResult token_result;
  std::tie(fetch_status, token_result) = SendTokenRequestAndWaitForResponse(
      "account", "request", net::HTTP_FORBIDDEN, "application/json",
      R"({"token": ""})", false, kTestTokenEndpoint, true);

  EXPECT_EQ(ParseStatus::kNoResponseError, fetch_status.parse_status);
  EXPECT_EQ(net::ERR_FAILED, fetch_status.response_code);
  ASSERT_EQ("", token_result.token);
  ASSERT_EQ(true, fetch_status.cors_error);
}

// Tests the client metadata implementation.
TEST_F(IdpNetworkRequestManagerTest, ClientMetadata) {
  bool called = false;
  auto interceptor =
      base::BindLambdaForTesting([&](const network::ResourceRequest& request) {
        called = true;
        std::string url_string =
            std::string(kTestClientMetadataEndpoint) + "?client_id=xxx";
        EXPECT_EQ(GURL(url_string), request.url);
        EXPECT_EQ(request.request_body, nullptr);
        EXPECT_FALSE(request.referrer.is_valid());
        EXPECT_EQ(url::Origin::Create(GURL(kTestRpUrl)),
                  GetOriginHeader(request));
      });
  test_url_loader_factory().SetInterceptor(interceptor);
  IdpClientMetadata data = SendClientMetadataRequestAndWaitForResponse("xxx");
  ASSERT_TRUE(called);
  ASSERT_EQ(GURL(), data.privacy_policy_url);
  ASSERT_EQ(GURL(), data.terms_of_service_url);
  ASSERT_EQ(GURL(), data.brand_icon_url);
  ASSERT_FALSE(data.client_matches_top_frame_origin.has_value());
}

// Tests the "matches top frame" boolean.
TEST_F(IdpNetworkRequestManagerTest, ClientMatchesTopFrameOrigin) {
  base::test::ScopedFeatureList list;
  list.InitAndEnableFeature(features::kFedCmIframeOrigin);

  IdpClientMetadata data = SendClientMetadataRequestAndWaitForResponse(
      "clientid", R"({"client_matches_top_frame_origin": false})",
      "https://toplevel.example");
  ASSERT_TRUE(data.client_matches_top_frame_origin.has_value());
  EXPECT_FALSE(*data.client_matches_top_frame_origin);
}

// Tests that we correctly records metrics regarding approved_clients.
TEST_F(IdpNetworkRequestManagerTest, RecordApprovedClientsMetrics) {
  base::HistogramTester histogram_tester;
  bool called = false;
  auto interceptor =
      base::BindLambdaForTesting([&](const network::ResourceRequest& request) {
        called = true;
        EXPECT_EQ(GURL(kTestAccountsEndpoint), request.url);
        EXPECT_EQ(request.request_body, nullptr);
        EXPECT_FALSE(request.referrer.is_valid());
        EXPECT_FALSE(
            request.headers.HasHeader(net::HttpRequestHeaders::kOrigin));
      });
  test_url_loader_factory().SetInterceptor(interceptor);

  const char test_accounts_json[] = R"({
  "accounts" : [
    {
      "id" : "1",
      "email": "ken@idp.test",
      "name": "Ken R. Example",
      "approved_clients": []
    },
    {
      "id" : "2",
      "email": "jim@idp.test",
      "name": "Jim R. Example",
      "approved_clients": ["xxx"]
    },
    {
      "id" : "3",
      "email": "rashida@idp.test",
      "name": "Rashida R. Example",
      "approved_clients": ["xxx", "yyy"]
    },
    {
      "id" : "4",
      "email": "wei@idp.test",
      "name": "Wei R. Example"
    }
   ]
  })";

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) =
      SendAccountsRequestAndWaitForResponse(test_accounts_json, "xxx");

  EXPECT_TRUE(called);
  EXPECT_EQ(ParseStatus::kSuccess, accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  ASSERT_EQ(4ul, accounts.size());

  histogram_tester.ExpectTotalCount("Blink.FedCm.ApprovedClientsExistence", 4);
  histogram_tester.ExpectBucketCount("Blink.FedCm.ApprovedClientsExistence", 1,
                                     3);
  histogram_tester.ExpectBucketCount("Blink.FedCm.ApprovedClientsExistence", 0,
                                     1);

  histogram_tester.ExpectTotalCount("Blink.FedCm.ApprovedClientsSize", 3);
  histogram_tester.ExpectBucketCount("Blink.FedCm.ApprovedClientsSize", 0, 1);
  histogram_tester.ExpectBucketCount("Blink.FedCm.ApprovedClientsSize", 1, 1);
  histogram_tester.ExpectBucketCount("Blink.FedCm.ApprovedClientsSize", 2, 1);
}

// Test that the callback is not called after IdpNetworkRequestManager is
// destroyed.
TEST_F(IdpNetworkRequestManagerTest, DontCallCallbackAfterManagerDeletion) {
  const char test_accounts_json[] = R"({
  "accounts" : [
    {
      "id" : "1",
      "email": "ken@idp.test",
      "name": "Ken R. Example",
      "approved_clients": []
    }
   ]
  })";

  GURL accounts_endpoint(kTestAccountsEndpoint);
  AddResponse(accounts_endpoint, net::HTTP_OK, "application/json",
              test_accounts_json);

  bool callback_called = false;
  auto callback = base::BindLambdaForTesting(
      [&callback_called](FetchStatus response,
                         std::vector<IdentityRequestAccountPtr> accounts) {
        callback_called = true;
      });

  {
    std::unique_ptr<IdpNetworkRequestManager> manager = CreateTestManager();
    manager->SendAccountsRequest(url::Origin::Create(accounts_endpoint),
                                 accounts_endpoint, /*client_id=*/"",
                                 std::move(callback));
    // Destroy `manager`.
  }
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(callback_called);
}

TEST_F(IdpNetworkRequestManagerTest, ErrorFetchingWellKnown) {
  FetchStatus fetch_status;
  IdpNetworkRequestManager::WellKnown wellknown;
  std::tie(fetch_status, wellknown) =
      SendWellKnownRequestAndWaitForResponse(R"({
  "provider_urls": ["https://idp.test/fedcm.json"]
  })",
                                             net::HTTP_REQUEST_TIMEOUT);
  EXPECT_EQ(ParseStatus::kNoResponseError, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_REQUEST_TIMEOUT, fetch_status.response_code);
  EXPECT_EQ(std::set<GURL>{}, wellknown.provider_urls);
}

TEST_F(IdpNetworkRequestManagerTest, ErrorFetchingConfig) {
  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) =
      SendConfigRequestAndWaitForResponse(R"({
  "branding" : {
    "color": "blue"
  }
  })",
                                          net::HTTP_NOT_FOUND);
  EXPECT_EQ(ParseStatus::kHttpNotFoundError, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_NOT_FOUND, fetch_status.response_code);
}

TEST_F(IdpNetworkRequestManagerTest, ErrorFetchingAccounts) {
  FetchStatus fetch_status;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(fetch_status, accounts) =
      SendAccountsRequestAndWaitForResponse(R"({
  "accounts" : []
  })",
                                            "", net::HTTP_BAD_REQUEST);
  EXPECT_EQ(ParseStatus::kNoResponseError, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_BAD_REQUEST, fetch_status.response_code);
}

TEST_F(IdpNetworkRequestManagerTest, FetchClientMetadataValidUrls) {
  // Both HTTPS and HTTP URLs are allowed.
  {
    const std::string privacy_policy_url = "https://privacy.policy";
    const std::string terms_of_service_url = "http://terms.of.service";
    const std::string brand_icon_url = "http://rp.brand.icon";

    IdpClientMetadata data = SendClientMetadataRequestAndWaitForResponse(
        /*client_id=*/"123", R"({"privacy_policy_url": ")" +
                                 privacy_policy_url +
                                 R"(", "terms_of_service_url": ")" +
                                 terms_of_service_url +
                                 R"(", "icons": [
      {
        "url":  ")" + brand_icon_url +
                                 R"(",
        "size": 40
      }
    ]})");
    ASSERT_EQ(GURL(privacy_policy_url), data.privacy_policy_url);
    ASSERT_EQ(GURL(terms_of_service_url), data.terms_of_service_url);
    ASSERT_EQ(GURL(brand_icon_url), data.brand_icon_url);
  }

  // local host URL is allowed.
  {
    const std::string privacy_policy_url = "http://localhost";
    const std::string terms_of_service_url = "http://127.0.0.1";
    const std::string brand_icon_url = "http://localhost";

    IdpClientMetadata data = SendClientMetadataRequestAndWaitForResponse(
        /*client_id=*/"123", R"({"privacy_policy_url": ")" +
                                 privacy_policy_url +
                                 R"(", "terms_of_service_url": ")" +
                                 terms_of_service_url +
                                 R"(", "icons": [
      {
        "url":  ")" + brand_icon_url +
                                 R"(",
        "size": 40
      }
    ]})");
    ASSERT_EQ(GURL(privacy_policy_url), data.privacy_policy_url);
    ASSERT_EQ(GURL(terms_of_service_url), data.terms_of_service_url);
    ASSERT_EQ(GURL(brand_icon_url), data.brand_icon_url);
  }
}

TEST_F(IdpNetworkRequestManagerTest, FetchClientMetadataInvalidUrls) {
  // Non-HTTP(S) URLs should not be allowed.
  const std::string privacy_policy_url = "chrome://settings";
  const std::string terms_of_service_url = "file:///Users/you/file.html";
  const std::string brand_icon_url = "about:blank";

  IdpClientMetadata data = SendClientMetadataRequestAndWaitForResponse(
      /*client_id=*/"123", R"({"privacy_policy_url": ")" + privacy_policy_url +
                               R"(", "terms_of_service_url": ")" +
                               terms_of_service_url +
                               R"(", "icons": [
      {
        "url":  ")" + brand_icon_url +
                               R"(",
        "size": 40
      }
    ]})");
  ASSERT_EQ(GURL(), data.privacy_policy_url);
  ASSERT_EQ(GURL(), data.terms_of_service_url);
  ASSERT_EQ(GURL(), data.brand_icon_url);
}

TEST_F(IdpNetworkRequestManagerTest, WellKnownWrongMimeType) {
  FetchStatus fetch_status;
  IdpNetworkRequestManager::WellKnown wellknown;
  std::tie(fetch_status, wellknown) =
      SendWellKnownRequestAndWaitForResponse(R"({
  "provider_urls": ["https://idp.test/fedcm.json"]
  })",
                                             net::HTTP_OK, "text/html");
  EXPECT_EQ(ParseStatus::kInvalidContentTypeError, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
  EXPECT_EQ(std::set<GURL>{}, wellknown.provider_urls);
}

TEST_F(IdpNetworkRequestManagerTest, ConfigWrongMimeType) {
  FetchStatus fetch_status;
  IdentityProviderMetadata idp_metadata;
  std::tie(fetch_status, idp_metadata) = SendConfigRequestAndWaitForResponse(
      R"({"branding" : { "color": "blue" } })", net::HTTP_OK, "text/html");
  EXPECT_EQ(ParseStatus::kInvalidContentTypeError, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
}

TEST_F(IdpNetworkRequestManagerTest, AccountsWrongMimeType) {
  const auto* test_single_account_json = kSingleAccountEndpointValidJson;

  FetchStatus accounts_response;
  std::vector<IdentityRequestAccountPtr> accounts;
  std::tie(accounts_response, accounts) = SendAccountsRequestAndWaitForResponse(
      test_single_account_json, /*client_id=*/"", net::HTTP_OK, "text/html");

  EXPECT_EQ(ParseStatus::kInvalidContentTypeError,
            accounts_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, accounts_response.response_code);
  EXPECT_TRUE(accounts.empty());
}

TEST_F(IdpNetworkRequestManagerTest, IdAssertionWrongMimeType) {
  FetchStatus fetch_status;
  TokenResult token_result;
  std::tie(fetch_status, token_result) = SendTokenRequestAndWaitForResponse(
      "account", "request", net::HTTP_OK, "text/html");
  EXPECT_EQ("", token_result.token);
  EXPECT_EQ(ParseStatus::kInvalidContentTypeError, fetch_status.parse_status);
  EXPECT_EQ(net::HTTP_OK, fetch_status.response_code);
}

TEST_F(IdpNetworkRequestManagerTest, FetchingTokenLeadsToAContinuationUrl) {
  net::HttpStatusCode http_status = net::HTTP_OK;
  const std::string& mime_type = "application/json";

  const char response[] =
      R"({"continue_on": "https://idp.test/an-absolute-url-for-continuation"})";
  GURL token_endpoint(kTestTokenEndpoint);
  AddResponse(token_endpoint, http_status, mime_type, response);

  base::RunLoop run_loop;
  auto callback = base::BindLambdaForTesting(
      [&](FetchStatus status, TokenResult result) {});

  auto on_continue = base::BindLambdaForTesting([&](FetchStatus status,
                                                    const GURL& url) {
    // Checks that we got a continuation url event back.
    EXPECT_EQ("https://idp.test/an-absolute-url-for-continuation", url.spec());
    run_loop.Quit();
  });

  std::unique_ptr<IdpNetworkRequestManager> manager = CreateTestManager();
  manager->SendTokenRequest(token_endpoint, "account", "request", false,
                            std::move(callback), std::move(on_continue),
                            CreateErrorMetricsCallback(run_loop));
  run_loop.Run();
  EXPECT_EQ(TokenResponseType::
                kTokenNotReceivedAndErrorNotReceivedAndContinueOnReceived,
            token_response_type());
}

//+    kTokenReceivedAndErrorReceivedAndContinueOnReceived = 5,

TEST_F(IdpNetworkRequestManagerTest, ContinueOnWithToken) {
  net::HttpStatusCode http_status = net::HTTP_OK;
  const std::string& mime_type = "application/json";

  const char response[] = R"({"continue_on": "/", "token": "a_token"})";
  GURL token_endpoint(kTestTokenEndpoint);
  AddResponse(token_endpoint, http_status, mime_type, response);

  base::RunLoop run_loop;
  std::unique_ptr<IdpNetworkRequestManager> manager = CreateTestManager();
  manager->SendTokenRequest(token_endpoint, "account", "request", false,
                            base::DoNothing(), base::DoNothing(),
                            CreateErrorMetricsCallback(run_loop));
  run_loop.Run();
  EXPECT_EQ(
      TokenResponseType::kTokenReceivedAndErrorNotReceivedAndContinueOnReceived,
      token_response_type());
}

TEST_F(IdpNetworkRequestManagerTest, ContinueOnWithErrorAndToken) {
  net::HttpStatusCode http_status = net::HTTP_OK;
  const std::string& mime_type = "application/json";

  const char response[] =
      R"({"continue_on": "/", "token": "a_token", "error": {"code": "foo"}})";
  GURL token_endpoint(kTestTokenEndpoint);
  AddResponse(token_endpoint, http_status, mime_type, response);

  base::RunLoop run_loop;
  std::unique_ptr<IdpNetworkRequestManager> manager = CreateTestManager();
  manager->SendTokenRequest(token_endpoint, "account", "request", false,
                            base::DoNothing(), base::DoNothing(),
                            CreateErrorMetricsCallback(run_loop));
  run_loop.Run();
  EXPECT_EQ(
      TokenResponseType::kTokenReceivedAndErrorReceivedAndContinueOnReceived,
      token_response_type());
}

TEST_F(IdpNetworkRequestManagerTest, ContinueOnWithError) {
  net::HttpStatusCode http_status = net::HTTP_OK;
  const std::string& mime_type = "application/json";

  const char response[] = R"({"continue_on": "/", "error": {"code": "foo"}})";
  GURL token_endpoint(kTestTokenEndpoint);
  AddResponse(token_endpoint, http_status, mime_type, response);

  base::RunLoop run_loop;
  std::unique_ptr<IdpNetworkRequestManager> manager = CreateTestManager();
  manager->SendTokenRequest(token_endpoint, "account", "request", false,
                            base::DoNothing(), base::DoNothing(),
                            CreateErrorMetricsCallback(run_loop));
  run_loop.Run();
  EXPECT_EQ(
      TokenResponseType::kTokenNotReceivedAndErrorReceivedAndContinueOnReceived,
      token_response_type());
}

TEST_F(IdpNetworkRequestManagerTest, ContinueOnCanBeRelativeUrl) {
  net::HttpStatusCode http_status = net::HTTP_OK;
  const std::string& mime_type = "application/json";

  const char response[] =
      R"({"continue_on": "/a-relative-url-for-continuation"})";
  GURL token_endpoint(kTestTokenEndpoint);
  AddResponse(token_endpoint, http_status, mime_type, response);

  base::RunLoop run_loop;
  auto callback = base::BindLambdaForTesting(
      [&](FetchStatus status, TokenResult result) {});

  auto on_continue = base::BindLambdaForTesting([&](FetchStatus status,
                                                    const GURL& url) {
    // Checks that we got a continuation url event back.
    EXPECT_EQ("https://idp.test/a-relative-url-for-continuation", url.spec());
    run_loop.Quit();
  });

  std::unique_ptr<IdpNetworkRequestManager> manager = CreateTestManager();
  manager->SendTokenRequest(token_endpoint, "account", "request", false,
                            std::move(callback), std::move(on_continue),
                            base::DoNothing());
  run_loop.Run();
}

TEST_F(IdpNetworkRequestManagerTest, IdAssertionRequestErrorWithProperField) {
  FetchStatus fetch_status;
  TokenResult token_result;
  std::tie(fetch_status, token_result) = SendTokenRequestAndWaitForResponse(
      "account", "request", net::HTTP_OK, "application/json", R"({
        "error": {
          "code": "invalid_request",
          "url": "https://idp.test/error"
        }
      })");

  EXPECT_TRUE(token_result.error);
  EXPECT_EQ("invalid_request", token_result.error->code);
  EXPECT_EQ("https://idp.test/error", token_result.error->url);
  EXPECT_EQ(TokenResponseType::
                kTokenNotReceivedAndErrorReceivedAndContinueOnNotReceived,
            token_response_type());
  EXPECT_TRUE(error_dialog_type());
  EXPECT_EQ(ErrorDialogType::kInvalidRequestWithUrl, *error_dialog_type());
  EXPECT_TRUE(error_url_type());
  EXPECT_EQ(ErrorUrlType::kSameOrigin, *error_url_type());
}

TEST_F(IdpNetworkRequestManagerTest, IdAssertionRequestErrorWithRelativePath) {
  FetchStatus fetch_status;
  TokenResult token_result;
  std::tie(fetch_status, token_result) = SendTokenRequestAndWaitForResponse(
      "account", "request", net::HTTP_OK, "application/json", R"({
        "error": {
          "code": "invalid_request",
          "url": "/error"
        }
      })");

  EXPECT_TRUE(token_result.error);
  EXPECT_EQ("invalid_request", token_result.error->code);
  EXPECT_EQ("https://idp.test/error", token_result.error->url);
  EXPECT_EQ(TokenResponseType::
                kTokenNotReceivedAndErrorReceivedAndContinueOnNotReceived,
            token_response_type());
  EXPECT_TRUE(error_dialog_type());
  EXPECT_EQ(ErrorDialogType::kInvalidRequestWithUrl, *error_dialog_type());
  EXPECT_TRUE(error_url_type());
  EXPECT_EQ(ErrorUrlType::kSameOrigin, *error_url_type());
}

TEST_F(IdpNetworkRequestManagerTest, IdAssertionRequestErrorWithCrossSiteUrl) {
  FetchStatus fetch_status;
  TokenResult token_result;
  std::tie(fetch_status, token_result) = SendTokenRequestAndWaitForResponse(
      "account", "request", net::HTTP_OK, "application/json", R"({
        "error": {
          "code": "invalid_request",
          "url": "https://cross-site-idp.test/error"
        }
      })");

  EXPECT_TRUE(token_result.error);
  EXPECT_EQ("invalid_request", token_result.error->code);
  EXPECT_EQ(GURL(), token_result.error->url);
  EXPECT_EQ(TokenResponseType::
                kTokenNotReceivedAndErrorReceivedAndContinueOnNotReceived,
            token_response_type());
  EXPECT_TRUE(error_dialog_type());
  EXPECT_EQ(ErrorDialogType::kInvalidRequestWithoutUrl, *error_dialog_type());
  EXPECT_TRUE(error_url_type());
  EXPECT_EQ(ErrorUrlType::kCrossSite, *error_url_type());
}

TEST_F(IdpNetworkRequestManagerTest,
       IdAssertionRequestErrorWithSameSiteCrossOriginUrl) {
  FetchStatus fetch_status;
  TokenResult token_result;
  std::tie(fetch_status, token_result) = SendTokenRequestAndWaitForResponse(
      "account", "request", net::HTTP_OK, "application/json", R"({
        "error": {
          "code": "invalid_request",
          "url": "https://cross-origin.idp.test/error"
        }
      })");

  EXPECT_TRUE(token_result.error);
  EXPECT_EQ("invalid_request", token_result.error->code);
  EXPECT_EQ("https://cross-origin.idp.test/error", token_result.error->url);
  EXPECT_EQ(TokenResponseType::
                kTokenNotReceivedAndErrorReceivedAndContinueOnNotReceived,
            token_response_type());
  EXPECT_TRUE(error_dialog_type());
  EXPECT_EQ(ErrorDialogType::kInvalidRequestWithUrl, *error_dialog_type());
  EXPECT_TRUE(error_url_type());
  EXPECT_EQ(ErrorUrlType::kCrossOriginSameSite, *error_url_type());
}

TEST_F(IdpNetworkRequestManagerTest,
       IdAssertionRequestErrorWithUntrustworthyUrl) {
  FetchStatus fetch_status;
  TokenResult token_result;
  std::tie(fetch_status, token_result) = SendTokenRequestAndWaitForResponse(
      "account", "request", net::HTTP_OK, "application/json", R"({
        "error": {
          "code": "invalid_request",
          "url": "http://idp.test/error"
        }
      })");

  EXPECT_TRUE(token_result.error);
  EXPECT_EQ("invalid_request", token_result.error->code);
  EXPECT_EQ(GURL(), token_result.error->url);
  EXPECT_EQ(TokenResponseType::
                kTokenNotReceivedAndErrorReceivedAndContinueOnNotReceived,
            token_response_type());
  EXPECT_TRUE(error_dialog_type());
  EXPECT_EQ(ErrorDialogType::kInvalidRequestWithoutUrl, *error_dialog_type());
  ASSERT_TRUE(error_url_type());
  EXPECT_EQ(ErrorUrlType::kCrossSite, *error_url_type());
}

TEST_F(IdpNetworkRequestManagerTest, IdAssertionRequestErrorWithEmptyUrl) {
  FetchStatus fetch_status;
  TokenResult token_result;
  std::tie(fetch_status, token_result) = SendTokenRequestAndWaitForResponse(
      "account", "request", net::HTTP_OK, "application/json", R"({
        "error": {
          "code": "invalid_request",
          "url": ""
        }
      })");

  EXPECT_TRUE(token_result.error);
  EXPECT_EQ("invalid_request", token_result.error->code);
  EXPECT_EQ(GURL(), token_result.error->url);
  EXPECT_EQ(TokenResponseType::
                kTokenNotReceivedAndErrorReceivedAndContinueOnNotReceived,
            token_response_type());
  EXPECT_TRUE(error_dialog_type());
  EXPECT_EQ(ErrorDialogType::kInvalidRequestWithoutUrl, *error_dialog_type());
  EXPECT_FALSE(error_url_type());
}

TEST_F(IdpNetworkRequestManagerTest, IdAssertionRequestErrorWithLocalHostUrl) {
  // allow localhost for error url
  {
    FetchStatus fetch_status;
    TokenResult token_result;
    std::tie(fetch_status, token_result) = SendTokenRequestAndWaitForResponse(
        "account", "request", net::HTTP_OK, "application/json", R"({
        "error": {
          "url": "http://localhost/error"
        }
      })",
        false, kTestLocalHostTokenEndpoint);

    EXPECT_TRUE(token_result.error);
    EXPECT_EQ("", token_result.error->code);
    EXPECT_EQ(GURL("http://localhost/error"), token_result.error->url);
    EXPECT_EQ(TokenResponseType::
                  kTokenNotReceivedAndErrorReceivedAndContinueOnNotReceived,
              token_response_type());
    EXPECT_TRUE(error_dialog_type());
    EXPECT_EQ(ErrorDialogType::kGenericEmptyWithUrl, *error_dialog_type());
    ASSERT_TRUE(error_url_type());
    EXPECT_EQ(ErrorUrlType::kSameOrigin, *error_url_type());
  }

  {
    FetchStatus fetch_status;
    TokenResult token_result;
    std::tie(fetch_status, token_result) = SendTokenRequestAndWaitForResponse(
        "account", "request", net::HTTP_OK, "application/json", R"({
        "error": {
          "code": "invalid_request",
          "url": "http://localhost/error"
        }
      })",
        false, kTestLocalHostTokenEndpoint);

    EXPECT_TRUE(token_result.error);
    EXPECT_EQ("invalid_request", token_result.error->code);
    EXPECT_EQ(GURL("http://localhost/error"), token_result.error->url);
    EXPECT_EQ(TokenResponseType::
                  kTokenNotReceivedAndErrorReceivedAndContinueOnNotReceived,
              token_response_type());
    EXPECT_TRUE(error_dialog_type());
    EXPECT_EQ(ErrorDialogType::kInvalidRequestWithUrl, *error_dialog_type());
    ASSERT_TRUE(error_url_type());
    EXPECT_EQ(ErrorUrlType::kSameOrigin, *error_url_type());
  }
}

TEST_F(IdpNetworkRequestManagerTest, IdAssertionResponse200NonParsable) {
  FetchStatus fetch_status;
  TokenResult token_result;
  std::tie(fetch_status, token_result) = SendTokenRequestAndWaitForResponse(
      "account", "request", net::HTTP_OK, "application/json", R"({}})");

  EXPECT_TRUE(token_result.error);
  EXPECT_EQ("", token_result.error->code);
  EXPECT_EQ(GURL(), token_result.error->url);
  EXPECT_EQ(TokenResponseType::
                kTokenNotReceivedAndErrorNotReceivedAndContinueOnNotReceived,
            token_response_type());
  EXPECT_TRUE(error_dialog_type());
  EXPECT_EQ(ErrorDialogType::kGenericEmptyWithoutUrl, *error_dialog_type());
  EXPECT_FALSE(error_url_type());
}

TEST_F(IdpNetworkRequestManagerTest, IdAssertionResponse500NonParsable) {
  FetchStatus fetch_status;
  TokenResult token_result;
  std::tie(fetch_status, token_result) = SendTokenRequestAndWaitForResponse(
      "account", "request", net::HTTP_INTERNAL_SERVER_ERROR, "application/json",
      R"({}})");

  EXPECT_TRUE(token_result.error);
  EXPECT_EQ("server_error", token_result.error->code);
  EXPECT_EQ(GURL(), token_result.error->url);
  EXPECT_EQ(TokenResponseType::
                kTokenNotReceivedAndErrorNotReceivedAndContinueOnNotReceived,
            token_response_type());
  EXPECT_TRUE(error_dialog_type());
  EXPECT_EQ(ErrorDialogType::kServerErrorWithoutUrl, *error_dialog_type());
  EXPECT_FALSE(error_url_type());
}

TEST_F(IdpNetworkRequestManagerTest, IdAssertionResponse503NonParsable) {
  FetchStatus fetch_status;
  TokenResult token_result;
  std::tie(fetch_status, token_result) = SendTokenRequestAndWaitForResponse(
      "account", "request", net::HTTP_SERVICE_UNAVAILABLE, "application/json",
      R"({}})");

  EXPECT_TRUE(token_result.error);
  EXPECT_EQ("temporarily_unavailable", token_result.error->code);
  EXPECT_EQ(GURL(), token_result.error->url);
  EXPECT_EQ(TokenResponseType::
                kTokenNotReceivedAndErrorNotReceivedAndContinueOnNotReceived,
            token_response_type());
  EXPECT_TRUE(error_dialog_type());
  EXPECT_EQ(ErrorDialogType::kTemporarilyUnavailableWithoutUrl,
            *error_dialog_type());
  EXPECT_FALSE(error_url_type());
}

TEST_F(IdpNetworkRequestManagerTest, IdAssertionResponseWithErrorAndHttpError) {
  FetchStatus fetch_status;
  TokenResult token_result;
  std::tie(fetch_status, token_result) = SendTokenRequestAndWaitForResponse(
      "account", "request", net::HTTP_SERVICE_UNAVAILABLE, "application/json",
      R"({
        "error": {
          "code": "temporarily_unavailable",
          "url": "https://idp.test/error"
        }
      })");

  EXPECT_TRUE(token_result.error);
  EXPECT_EQ("temporarily_unavailable", token_result.error->code);
  EXPECT_EQ("https://idp.test/error", token_result.error->url);
  EXPECT_EQ(TokenResponseType::
                kTokenNotReceivedAndErrorReceivedAndContinueOnNotReceived,
            token_response_type());
  EXPECT_TRUE(error_dialog_type());
  EXPECT_EQ(ErrorDialogType::kTemporarilyUnavailableWithUrl,
            *error_dialog_type());
  EXPECT_TRUE(error_url_type());
  EXPECT_EQ(ErrorUrlType::kSameOrigin, *error_url_type());
}

TEST_F(IdpNetworkRequestManagerTest, IdAssertionResponseWithTokenAndHttpError) {
  FetchStatus fetch_status;
  TokenResult token_result;

  std::tie(fetch_status, token_result) = SendTokenRequestAndWaitForResponse(
      "account", "request", net::HTTP_FORBIDDEN);

  EXPECT_EQ("", token_result.token);
  EXPECT_TRUE(token_result.error);
  EXPECT_EQ("", token_result.error->code);
  EXPECT_EQ("", token_result.error->url);
  EXPECT_EQ(net::HTTP_FORBIDDEN, fetch_status.response_code);
  EXPECT_EQ(ParseStatus::kInvalidResponseError, fetch_status.parse_status);
  EXPECT_EQ(TokenResponseType::
                kTokenNotReceivedAndErrorNotReceivedAndContinueOnNotReceived,
            token_response_type());
  EXPECT_TRUE(error_dialog_type());
  EXPECT_EQ(ErrorDialogType::kGenericEmptyWithoutUrl, *error_dialog_type());
  EXPECT_FALSE(error_url_type());
}

TEST_F(IdpNetworkRequestManagerTest, DisconnectRequest) {
  bool called = false;
  auto interceptor =
      base::BindLambdaForTesting([&](const network::ResourceRequest& request) {
        called = true;
        EXPECT_EQ(GURL(kTestDisconnectEndpoint), request.url);
        EXPECT_FALSE(request.referrer.is_valid());
        url::Origin rpOrigin = url::Origin::Create(GURL(kTestRpUrl));
        EXPECT_EQ(GetOriginHeader(request), rpOrigin);

        // Check that the request body is correct.
        ASSERT_NE(request.request_body, nullptr);
        ASSERT_EQ(1ul, request.request_body->elements()->size());
        const network::DataElement& elem =
            request.request_body->elements()->at(0);
        ASSERT_EQ(network::DataElement::Tag::kBytes, elem.type());
        const network::DataElementBytes& byte_elem =
            elem.As<network::DataElementBytes>();
        EXPECT_EQ("client_id=clientId&account_hint=hint",
                  byte_elem.AsStringPiece());
        ASSERT_EQ(request.mode, network::mojom::RequestMode::kCors);
        ASSERT_EQ(request.request_initiator, rpOrigin);
      });
  test_url_loader_factory().SetInterceptor(interceptor);

  const char test_disconnect_json[] = R"({
  "account_id" : "accountId"
  })";

  GURL disconnect_endpoint(kTestDisconnectEndpoint);
  AddResponse(disconnect_endpoint, net::HTTP_OK, "application/json",
              test_disconnect_json);

  base::RunLoop run_loop;
  FetchStatus disconnect_response;
  std::optional<std::string> disconnect_account_id;
  auto callback = base::BindLambdaForTesting(
      [&](FetchStatus response, const std::string& account_id) {
        disconnect_response = response;
        disconnect_account_id = account_id;
        run_loop.Quit();
      });

  std::unique_ptr<IdpNetworkRequestManager> manager = CreateTestManager();
  manager->SendDisconnectRequest(disconnect_endpoint, "hint", "clientId",
                                 std::move(callback));
  run_loop.Run();

  EXPECT_TRUE(called);
  EXPECT_EQ(ParseStatus::kSuccess, disconnect_response.parse_status);
  EXPECT_EQ(net::HTTP_OK, disconnect_response.response_code);
  ASSERT_TRUE(disconnect_account_id.has_value());
  EXPECT_EQ(*disconnect_account_id, "accountId");
}

}  // namespace

}  // namespace content
