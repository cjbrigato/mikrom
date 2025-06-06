// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/services/auction_worklet/public/cpp/auction_downloader.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "content/services/auction_worklet/public/cpp/auction_worklet_features.h"
#include "content/services/auction_worklet/public/mojom/in_progress_auction_download.mojom.h"
#include "content/services/auction_worklet/worklet_test_util.h"
#include "net/base/isolation_info.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"
#include "services/network/public/cpp/devtools_observer_util.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/resource_request_body.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/url_loader.mojom-forward.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom-forward.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace auction_worklet {
namespace {

const char kAsciiResponseBody[] = "ASCII response body.";
const char kUtf8ResponseBody[] = "\xc3\x9f\xc3\x9e";
const char kNonUtf8ResponseBody[] = "\xc3";

const char kAsciiCharset[] = "us-ascii";
const char kUtf8Charset[] = "utf-8";

const char kCachedTrustedBiddingSignalsAge[] =
    "Ads.InterestGroup.Auction.HttpCachedTrustedBiddingSignalsAge2";

const char kBiddingSignalsResponseDownloadTime[] =
    "Ads.InterestGroup.Auction.BiddingSignalsResponseDownloadTime";

const char kBiddingSignalsResponseDownloadTimePerIG[] =
    "Ads.InterestGroup.Auction.BiddingSignalsResponseDownloadTimePerIG";

const char kBiddingSignalsResponseDownloadTimeAfterFirstDownloadTimePerIG[] =
    "Ads.InterestGroup.Auction."
    "BiddingSignalsResponseDownloadTimeAfterOneDownloadTimePerIG";

// Creates a URLResponseHeadPtr that the AuctionDownloader will accept as a
// valid set of headers for a response.
network::mojom::URLResponseHeadPtr CreateResponseHead() {
  auto response_head = network::mojom::URLResponseHead::New();
  response_head->headers = net::HttpResponseHeaders::TryToCreate(
      "HTTP/1.1 200 OK\r\n"
      "Ad-Auction-Allowed: true\r\n");
  return response_head;
}

enum class ConstructorChoice {
  kStartLoadInWorklet,
  kStartLoadInBrowser,
  kAdoptInProgressLoad
};

// The bool parameter is true if the constructor that takes an initiator should
// be used.
class AuctionDownloaderTest
    : public testing::TestWithParam<
          std::tuple<AuctionDownloader::DownloadMode, ConstructorChoice>> {
 public:
  AuctionDownloaderTest() = default;
  ~AuctionDownloaderTest() override = default;

  AuctionDownloader::DownloadMode download_mode() const {
    return std::get<AuctionDownloader::DownloadMode>(GetParam());
  }

  ConstructorChoice constructor_choice() const {
    return std::get<ConstructorChoice>(GetParam());
  }

  // Returns the request initiator to use.
  // ConstructorChoice::kStartLoadInBrowser must be used.
  url::Origin RequestInitiator() const {
    CHECK_EQ(constructor_choice(), ConstructorChoice::kStartLoadInBrowser);
    return url::Origin::Create(GURL("https://initiator.test"));
  }

  // Returns the TrustedParams to use. ConstructorChoice::kStartLoadInBrowser
  // must be used.
  network::ResourceRequest::TrustedParams TrustedParams() const {
    CHECK_EQ(constructor_choice(), ConstructorChoice::kStartLoadInBrowser);
    network::ResourceRequest::TrustedParams trusted_params;
    trusted_params.isolation_info =
        net::IsolationInfo::CreateForInternalRequest(
            url::Origin::Create(GURL("https://isolation_info.test")));
    return trusted_params;
  }

  class TestDelegate : public AuctionDownloader::NetworkEventsDelegate {
   public:
    TestDelegate(network::URLLoaderCompletionStatus& completetion_status,
                 std::optional<GURL>& response_url,
                 std::optional<std::string>& request_id,
                 std::optional<GURL>& request_url,
                 std::optional<network::mojom::URLResponseHeadPtr>& head)
        : request_url_ref_(request_url),
          head_ref_(head),
          request_id_ref_(request_id),
          response_url_ref_(response_url),
          completetion_status_ref_(completetion_status) {}

    ~TestDelegate() override = default;

    void OnNetworkSendRequest(network::ResourceRequest& request) override {
      *request_url_ref_ = request.url;
      *request_id_ref_ = request.devtools_request_id;
    }

    void OnNetworkResponseReceived(
        const GURL& url,
        const network::mojom::URLResponseHead& head) override {
      *response_url_ref_ = url;
      *head_ref_ = head.Clone();
    }

    void OnNetworkRequestComplete(
        const network::URLLoaderCompletionStatus& status) override {
      *completetion_status_ref_ = status;
    }

   private:
    raw_ref<std::optional<GURL>> request_url_ref_;
    raw_ref<std::optional<network::mojom::URLResponseHeadPtr>> head_ref_;
    raw_ref<std::optional<std::string>> request_id_ref_;
    raw_ref<std::optional<GURL>> response_url_ref_;
    raw_ref<network::URLLoaderCompletionStatus> completetion_status_ref_;
  };

  std::unique_ptr<AuctionDownloader> MakeDownloader(
      std::optional<std::string> post_body = std::nullopt,
      std::optional<std::string> content_type = std::nullopt) {
    auto test_network_events_delegate = std::make_unique<TestDelegate>(
        observed_completion_status_, observed_response_url_,
        observed_request_id_, observed_request_url_, observed_response_head_);
    std::unique_ptr<AuctionDownloader> downloader;
    switch (constructor_choice()) {
      case ConstructorChoice::kAdoptInProgressLoad: {
        TestAuctionNetworkEventsHandler test_auction_network_events_handler;
        auto in_progress_load = AuctionDownloader::StartDownload(
            url_loader_factory_, url_, mime_type_,
            test_auction_network_events_handler, post_body, content_type);
        observed_request_url_ = in_progress_load->url;
        observed_request_id_ = in_progress_load->devtools_request_id;
        downloader = std::make_unique<AuctionDownloader>(
            &url_loader_factory_, std::move(in_progress_load), download_mode(),
            mime_type_, num_igs_for_trusted_bidding_signals_kvv1_,
            response_started_callback_,
            base::BindOnce(&AuctionDownloaderTest::DownloadCompleteCallback,
                           base::Unretained(this)),
            std::move(test_network_events_delegate));
        break;
      }
      case ConstructorChoice::kStartLoadInWorklet: {
        downloader = std::make_unique<AuctionDownloader>(
            &url_loader_factory_, url_, download_mode(), mime_type_,
            std::move(post_body), std::move(content_type),
            num_igs_for_trusted_bidding_signals_kvv1_,
            response_started_callback_,
            base::BindOnce(&AuctionDownloaderTest::DownloadCompleteCallback,
                           base::Unretained(this)),
            std::move(test_network_events_delegate));
        break;
      }
      case ConstructorChoice::kStartLoadInBrowser: {
        // This constructor doesn't take a ResponseStarted callback, or take
        // `num_igs_for_trusted_bidding_signals_kvv1_`.
        CHECK(!response_started_callback_);
        CHECK(!num_igs_for_trusted_bidding_signals_kvv1_);
        downloader = std::make_unique<AuctionDownloader>(
            &url_loader_factory_, url_, download_mode(), mime_type_,
            std::move(post_body), std::move(content_type), RequestInitiator(),
            TrustedParams(),
            base::BindOnce(&AuctionDownloaderTest::DownloadCompleteCallback,
                           base::Unretained(this)),
            std::move(test_network_events_delegate));
      }
    }
    return downloader;
  }

  // `trusted_params` may only be non-null if
  // ConstructorChoice::kStartLoadInBrowser is used, since only the constructor
  // that takes an initiator takes a TrustedParams argument.
  std::unique_ptr<std::string> RunRequest(
      std::optional<std::string> post_body = std::nullopt,
      std::optional<std::string> content_type = std::nullopt) {
    DCHECK(!run_loop_);

    // reset values
    observed_request_id_ = std::nullopt;
    observed_request_url_ = std::nullopt;
    observed_request_content_type_ = std::nullopt;
    observed_response_url_ = std::nullopt;
    observed_completion_status_ =
        network::URLLoaderCompletionStatus(net::Error());
    observed_response_head_ = std::nullopt;

    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [this](const network::ResourceRequest& request) {
          EXPECT_TRUE(request.devtools_request_id);
          EXPECT_EQ(request.credentials_mode,
                    network::mojom::CredentialsMode::kOmit);
          EXPECT_EQ(request.redirect_mode,
                    network::mojom::RedirectMode::kError);
          EXPECT_EQ(request.url, url_);
          EXPECT_EQ(
              AuctionDownloader::MimeTypeToStringForTesting(mime_type_),
              request.headers.GetHeader(net::HttpRequestHeaders::kAccept));

          if (constructor_choice() != ConstructorChoice::kStartLoadInBrowser) {
            EXPECT_FALSE(request.request_initiator);
            EXPECT_EQ(request.mode, network::mojom::RequestMode::kNoCors);
            EXPECT_FALSE(request.trusted_params);
          } else {
            EXPECT_EQ(request.request_initiator, RequestInitiator());
            EXPECT_EQ(request.mode, network::mojom::RequestMode::kCors);
            ASSERT_TRUE(request.trusted_params);
            EXPECT_TRUE(
                request.trusted_params->isolation_info.IsEqualForTesting(
                    TrustedParams().isolation_info));
          }

          observed_request_post_body_ = request.request_body;
          observed_request_content_type_ =
              request.headers.GetHeader(net::HttpRequestHeaders::kContentType);
          observed_request_method_ = request.method;
        }));

    std::unique_ptr<AuctionDownloader> downloader =
        MakeDownloader(post_body, content_type);

    // Populate `run_loop_` after starting the download, since API guarantees
    // callback will not be invoked synchronously.
    run_loop_ = std::make_unique<base::RunLoop>();
    run_loop_->Run();
    run_loop_.reset();
    return std::move(body_);
  }

  std::string EmptyIfSimulated(std::string in) {
    return download_mode() ==
                   AuctionDownloader::DownloadMode::kSimulatedDownload
               ? std::string()
               : in;
  }

  // Helper to avoid checking has_value all over the place.
  std::string last_error_msg() const {
    return error_.value_or("Not an error.");
  }

  void DownloadCompleteCallback(std::unique_ptr<std::string> body,
                                scoped_refptr<net::HttpResponseHeaders> headers,
                                std::optional<std::string> error) {
    DCHECK(!body_);
    DCHECK(run_loop_);
    body_ = std::move(body);
    headers_ = std::move(headers);
    error_ = std::move(error);
    EXPECT_EQ(error_.has_value(), !body_);
    run_loop_->Quit();
  }

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  GURL url_ = GURL("https://url.test/script.js");

  AuctionDownloader::MimeType mime_type_ =
      AuctionDownloader::MimeType::kJavascript;

  std::unique_ptr<base::RunLoop> run_loop_;
  std::unique_ptr<std::string> body_;
  scoped_refptr<net::HttpResponseHeaders> headers_;
  std::optional<std::string> error_;

  network::TestURLLoaderFactory url_loader_factory_;

  std::optional<GURL> observed_request_url_;
  std::optional<std::string> observed_request_id_;
  scoped_refptr<network::ResourceRequestBody> observed_request_post_body_;
  std::optional<std::string> observed_request_content_type_;
  std::optional<std::string> observed_request_method_;
  std::optional<GURL> observed_response_url_;
  std::optional<network::mojom::URLResponseHeadPtr> observed_response_head_;
  network::URLLoaderCompletionStatus observed_completion_status_;

  base::RepeatingCallback<void(const network::mojom::URLResponseHead&)>
      response_started_callback_;

  std::optional<size_t> num_igs_for_trusted_bidding_signals_kvv1_;
};

TEST_P(AuctionDownloaderTest, NetworkError) {
  network::URLLoaderCompletionStatus status;
  status.error_code = net::ERR_FAILED;
  url_loader_factory_.AddResponse(url_, /*head=*/nullptr, kAsciiResponseBody,
                                  status);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Failed to load https://url.test/script.js error = net::ERR_FAILED.",
      last_error_msg());
  EXPECT_EQ(observed_completion_status_.error_code, net::ERR_FAILED);
}

TEST_P(AuctionDownloaderTest, NetworkErrorTruncatesUrl) {
  network::URLLoaderCompletionStatus status;
  status.error_code = net::ERR_FAILED;
  std::string almost_too_long_url_base = "https://url.test/";
  almost_too_long_url_base += std::string(
      AuctionDownloader::kMaxErrorUrlLength - almost_too_long_url_base.size(),
      '1');
  GURL almost_too_long_url = GURL(almost_too_long_url_base);
  GURL too_long_url = GURL(almost_too_long_url_base + "2");

  url_ = almost_too_long_url;
  url_loader_factory_.AddResponse(url_, /*head=*/nullptr, kAsciiResponseBody,
                                  status);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(base::StringPrintf("Failed to load %s error = net::ERR_FAILED.",
                               almost_too_long_url.spec().c_str()),
            last_error_msg());
  EXPECT_EQ(observed_completion_status_.error_code, net::ERR_FAILED);

  url_ = too_long_url;
  url_loader_factory_.AddResponse(url_, /*head=*/nullptr, kAsciiResponseBody,
                                  status);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(base::StringPrintf(
                "Failed to load %s... error = net::ERR_FAILED.",
                too_long_url.spec()
                    .substr(0, AuctionDownloader::kMaxErrorUrlLength - 3)
                    .c_str()),
            last_error_msg());
  EXPECT_EQ(observed_completion_status_.error_code, net::ERR_FAILED);
}

// HTTP 404 responses are treated as failures.
TEST_P(AuctionDownloaderTest, HttpError) {
  // This is an unlikely response for an error case, but should fail if it ever
  // happens.
  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody, kAllowFledgeHeader, net::HTTP_NOT_FOUND);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Failed to load https://url.test/script.js HTTP status = 404 Not Found.",
      last_error_msg());
  EXPECT_EQ(observed_completion_status_.error_code,
            net::ERR_HTTP_RESPONSE_CODE_FAILURE);
}

TEST_P(AuctionDownloaderTest, Timeout) {
  // Set up and start the downloader inline, since can't use RunRequest() in
  // this test.
  run_loop_ = std::make_unique<base::RunLoop>();
  std::unique_ptr<AuctionDownloader> downloader = MakeDownloader();

  // Run until just before the timeout duration. The request should not time
  // out.
  constexpr base::TimeDelta kTinyTime = base::Milliseconds(1);
  task_environment_.FastForwardBy(AuctionDownloader::kRequestTimeout -
                                  kTinyTime);
  EXPECT_FALSE(run_loop_->AnyQuitCalled());

  // Wait until the timeout duration has passed. The request should have timed
  // out.
  task_environment_.FastForwardBy(kTinyTime);
  EXPECT_TRUE(run_loop_->AnyQuitCalled());
  EXPECT_EQ(
      "Failed to load https://url.test/script.js error = net::ERR_TIMED_OUT.",
      last_error_msg());
}

TEST_P(AuctionDownloaderTest, AllowAdAuction) {
  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody, "X-Allow-FLEDGE: true");
  EXPECT_TRUE(RunRequest());
  EXPECT_EQ(observed_request_url_, observed_response_url_);
  ASSERT_TRUE(observed_response_head_.has_value());
  const scoped_refptr<::net::HttpResponseHeaders> observed_header =
      observed_response_head_.value()->headers;
  EXPECT_TRUE(observed_header->GetNormalizedHeader("X-Allow-FLEDGE"));
  EXPECT_EQ(observed_completion_status_.error_code, net::OK);

  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody, "x-aLLow-fLeDgE: true");
  EXPECT_TRUE(RunRequest());

  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody, "aD-aUCtioN-alloWeD: true");
  EXPECT_TRUE(RunRequest());

  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody, "X-Allow-FLEDGE: false");
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to lack of "
      "Ad-Auction-Allowed: true (or the deprecated X-Allow-FLEDGE: true).",
      last_error_msg());
  EXPECT_EQ(observed_completion_status_.error_code, net::ERR_ABORTED);

  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody, "Ad-Auction-Allowed: true");
  EXPECT_TRUE(RunRequest());

  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody, "Ad-Auction-Allowed: false");
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to lack of "
      "Ad-Auction-Allowed: true (or the deprecated X-Allow-FLEDGE: true).",
      last_error_msg());

  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody,
              "Ad-Auction-Allowed: true\nX-Allow-FLEDGE: true");
  EXPECT_TRUE(RunRequest());

  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody,
              "Ad-Auction-Allowed: false\nX-Allow-FLEDGE: false");
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to lack of "
      "Ad-Auction-Allowed: true (or the deprecated X-Allow-FLEDGE: true).",
      last_error_msg());

  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody, "Ad-Auction-Allowed: sometimes");
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to lack of "
      "Ad-Auction-Allowed: true (or the deprecated X-Allow-FLEDGE: true).",
      last_error_msg());

  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody, "Ad-Auction-Allowed: ");
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to lack of "
      "Ad-Auction-Allowed: true (or the deprecated X-Allow-FLEDGE: true).",
      last_error_msg());

  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody, "X-Allow-Hats: true");
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to lack of "
      "Ad-Auction-Allowed: true (or the deprecated X-Allow-FLEDGE: true).",
      last_error_msg());

  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody, "");
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to lack of "
      "Ad-Auction-Allowed: true (or the deprecated X-Allow-FLEDGE: true).",
      last_error_msg());

  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody, std::nullopt);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to lack of "
      "Ad-Auction-Allowed: true (or the deprecated X-Allow-FLEDGE: true).",
      last_error_msg());
}

TEST_P(AuctionDownloaderTest, PassesHeaders) {
  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody, "Ad-Auction-Allowed: true");
  EXPECT_TRUE(RunRequest()) << last_error_msg();
  EXPECT_EQ(headers_->GetNormalizedHeader("Ad-Auction-Allowed"), "true");
  EXPECT_FALSE(headers_->GetNormalizedHeader("Data-Version"));

  mime_type_ = AuctionDownloader::MimeType::kJson;
  AddVersionedJsonResponse(&url_loader_factory_, url_, kAsciiResponseBody, 10u);
  EXPECT_TRUE(RunRequest()) << last_error_msg();
  EXPECT_EQ(headers_->GetNormalizedHeader("Ad-Auction-Allowed"), "true");
  EXPECT_EQ(headers_->GetNormalizedHeader("Data-Version"), "10");

  AddVersionedJsonResponse(&url_loader_factory_, url_, kAsciiResponseBody, 5u);
  EXPECT_TRUE(RunRequest()) << last_error_msg();
  EXPECT_EQ(headers_->GetNormalizedHeader("Ad-Auction-Allowed"), "true");
  EXPECT_EQ(headers_->GetNormalizedHeader("Data-Version"), "5");

  AddJsonResponse(&url_loader_factory_, url_, kAsciiResponseBody);
  EXPECT_TRUE(RunRequest()) << last_error_msg();
  EXPECT_EQ(headers_->GetNormalizedHeader("Ad-Auction-Allowed"), "true");
  EXPECT_FALSE(headers_->GetNormalizedHeader("Data-Version"));
}

TEST_P(AuctionDownloaderTest, ResponseStartedCallback) {
  // ResponseStartedCallback is only supported by the other constructors.
  if (constructor_choice() == ConstructorChoice::kStartLoadInBrowser) {
    return;
  }
  bool called = false;
  response_started_callback_ = base::BindLambdaForTesting(
      [&](const network::mojom::URLResponseHead& response_head) {
        EXPECT_FALSE(called);
        // If this callback is called, it's called before the result one.
        EXPECT_TRUE(run_loop_);
        called = true;
        ASSERT_TRUE(response_head.headers);
        EXPECT_EQ(response_head.headers->GetNormalizedHeader("Test-Header"),
                  "test-val");
      });

  // Since response doesn't have Ad-Auction-Allowed, this will fail and not
  // get the `response_started_callback_` invoked, either.
  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody, "Test-Header: test-val");
  EXPECT_FALSE(RunRequest());
  EXPECT_FALSE(called);
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to lack of "
      "Ad-Auction-Allowed: true (or the deprecated X-Allow-FLEDGE: true).",
      last_error_msg());

  // This will succeed and invoke the callback as well.
  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody,
              "Test-Header: test-val\r\nAd-Auction-Allowed: true");
  EXPECT_TRUE(RunRequest());
  EXPECT_TRUE(called);
}

// Redirect responses are treated as failures.
TEST_P(AuctionDownloaderTest, Redirect) {
  // None of these fields actually matter for this test, but a bit strange for
  // them not to be populated.
  net::RedirectInfo redirect_info;
  redirect_info.status_code = net::HTTP_MOVED_PERMANENTLY;
  redirect_info.new_url = url_;
  redirect_info.new_method = "GET";
  network::TestURLLoaderFactory::Redirects redirects;
  redirects.push_back(
      std::make_pair(redirect_info, network::mojom::URLResponseHead::New()));

  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody, kAllowFledgeHeader, net::HTTP_OK,
              std::move(redirects));
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ("Unexpected redirect on https://url.test/script.js.",
            last_error_msg());
  EXPECT_EQ(observed_completion_status_.error_code, net::ERR_ABORTED);
}

TEST_P(AuctionDownloaderTest, Success) {
  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody);
  std::unique_ptr<std::string> body = RunRequest();
  ASSERT_TRUE(body);
  EXPECT_EQ(EmptyIfSimulated(kAsciiResponseBody), *body);
}

TEST_P(AuctionDownloaderTest, SuccessWithPostBody) {
  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody);
  const std::string kPostBody = "TEST BODY";
  const std::string kContentType = "text/javascript";
  std::unique_ptr<std::string> body =
      RunRequest(std::move(kPostBody), std::move(kContentType));
  ASSERT_TRUE(body);
  ASSERT_TRUE(observed_request_post_body_);
  ASSERT_EQ(1u, observed_request_post_body_->elements()->size());
  const network::DataElement& elem =
      observed_request_post_body_->elements()->at(0);
  ASSERT_EQ(network::DataElement::Tag::kBytes, elem.type());
  const network::DataElementBytes& byte_elem =
      elem.As<network::DataElementBytes>();
  EXPECT_EQ(kPostBody, byte_elem.AsStringPiece());
  EXPECT_EQ(observed_request_method_, "POST");
  EXPECT_EQ(observed_request_content_type_, kContentType);
  EXPECT_EQ(EmptyIfSimulated(kAsciiResponseBody), *body);
}

// Test `AuctionDownloader::MimeType` values work as expected.
TEST_P(AuctionDownloaderTest, MimeType) {
  // Javascript request, JSON response type.
  AddResponse(&url_loader_factory_, url_, kJsonMimeType, kUtf8Charset,
              kAsciiResponseBody);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected MIME "
      "type.",
      last_error_msg());

  // Javascript request, no response type.
  AddResponse(&url_loader_factory_, url_, std::nullopt, kUtf8Charset,
              kAsciiResponseBody);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected MIME "
      "type.",
      last_error_msg());

  // Javascript request, empty response type.
  AddResponse(&url_loader_factory_, url_, "", kUtf8Charset, kAsciiResponseBody);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected MIME "
      "type.",
      last_error_msg());

  // Javascript request, unknown response type.
  AddResponse(&url_loader_factory_, url_, "blobfish", kUtf8Charset,
              kAsciiResponseBody);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected MIME "
      "type.",
      last_error_msg());

  // JSON request, Javascript response type.
  mime_type_ = AuctionDownloader::MimeType::kJson;
  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected MIME "
      "type.",
      last_error_msg());

  // JSON request, no response type.
  AddResponse(&url_loader_factory_, url_, std::nullopt, kUtf8Charset,
              kAsciiResponseBody);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected MIME "
      "type.",
      last_error_msg());

  // JSON request, empty response type.
  AddResponse(&url_loader_factory_, url_, "", kUtf8Charset, kAsciiResponseBody);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected MIME "
      "type.",
      last_error_msg());

  // JSON request, unknown response type.
  AddResponse(&url_loader_factory_, url_, "blobfish", kUtf8Charset,
              kAsciiResponseBody);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected MIME "
      "type.",
      last_error_msg());

  // JSON request, JSON response type.
  mime_type_ = AuctionDownloader::MimeType::kJson;
  AddResponse(&url_loader_factory_, url_, kJsonMimeType, kUtf8Charset,
              kAsciiResponseBody);
  std::unique_ptr<std::string> body = RunRequest();
  ASSERT_TRUE(body);
  EXPECT_EQ(EmptyIfSimulated(kAsciiResponseBody), *body);
}

TEST_P(AuctionDownloaderTest, MimeTypeWasm) {
  mime_type_ = AuctionDownloader::MimeType::kWebAssembly;

  // WASM request, Javascript response type.
  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected MIME "
      "type.",
      last_error_msg());

  // WASM request, no response type.
  AddResponse(&url_loader_factory_, url_, /*mime_type=*/std::nullopt,
              /*charset=*/std::nullopt, kAsciiResponseBody);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected MIME "
      "type.",
      last_error_msg());

  // WASM request, JSON response type.
  AddResponse(&url_loader_factory_, url_, kJsonMimeType, kUtf8Charset,
              kAsciiResponseBody);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected MIME "
      "type.",
      last_error_msg());

  // WASM request, WASM response type.
  AddResponse(&url_loader_factory_, url_, kWasmMimeType,
              /*charset=*/std::nullopt, kNonUtf8ResponseBody);
  std::unique_ptr<std::string> body = RunRequest();
  ASSERT_TRUE(body);
  EXPECT_EQ(EmptyIfSimulated(kNonUtf8ResponseBody), *body);

  // Mimetypes are case insensitive.
  AddResponse(&url_loader_factory_, url_, "Application/WasM",
              /*charset=*/std::nullopt, kNonUtf8ResponseBody);
  body = RunRequest();
  ASSERT_TRUE(body);
  EXPECT_EQ(EmptyIfSimulated(kNonUtf8ResponseBody), *body);

  // No charset should be used (it's a binary format, after all).
  AddResponse(&url_loader_factory_, url_, kWasmMimeType,
              /*charset=*/kUtf8Charset, kUtf8ResponseBody);
  body = RunRequest();
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected MIME "
      "type.",
      last_error_msg());

  // Even an empty parameter list is to be rejected.
  AddResponse(&url_loader_factory_, url_, "application/wasm;",
              /*charset=*/std::nullopt, kNonUtf8ResponseBody);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected MIME "
      "type.",
      last_error_msg());
}

TEST_P(AuctionDownloaderTest, MimeTypeTrustedSignals) {
  mime_type_ = AuctionDownloader::MimeType::kAdAuctionTrustedSignals;

  // AdAuctionTrustedSignals request, Javascript response type.
  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected MIME "
      "type.",
      last_error_msg());

  // AdAuctionTrustedSignals request, no response type.
  AddResponse(&url_loader_factory_, url_, /*mime_type=*/std::nullopt,
              /*charset=*/std::nullopt, kAsciiResponseBody);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected MIME "
      "type.",
      last_error_msg());

  // AdAuctionTrustedSignals request, JSON response type.
  AddResponse(&url_loader_factory_, url_, kJsonMimeType, kUtf8Charset,
              kAsciiResponseBody);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected MIME "
      "type.",
      last_error_msg());

  // AdAuctionTrustedSignals request, AdAuctionTrustedSignals response type.
  AddResponse(&url_loader_factory_, url_, kAdAuctionTrustedSignalsMimeType,
              /*charset=*/std::nullopt, kUtf8ResponseBody);
  std::unique_ptr<std::string> body = RunRequest();
  ASSERT_TRUE(body);
  EXPECT_EQ(EmptyIfSimulated(kUtf8ResponseBody), *body);
}

// Test all Javascript and JSON MIME type strings.
TEST_P(AuctionDownloaderTest, MimeTypeVariants) {
  // All supported Javascript MIME types, copied from blink's mime_util.cc.
  const char* kJavascriptMimeTypes[] = {
      "application/ecmascript",
      "application/javascript",
      "application/x-ecmascript",
      "application/x-javascript",
      "text/ecmascript",
      "text/javascript",
      "text/javascript1.0",
      "text/javascript1.1",
      "text/javascript1.2",
      "text/javascript1.3",
      "text/javascript1.4",
      "text/javascript1.5",
      "text/jscript",
      "text/livescript",
      "text/x-ecmascript",
      "text/x-javascript",
  };

  // Some MIME types (there are some wild cards in the matcher).
  const char* kJsonMimeTypes[] = {
      "application/json",      "text/json",
      "application/goat+json", "application/javascript+json",
      "application/+json",
  };

  for (const char* javascript_type : kJavascriptMimeTypes) {
    mime_type_ = AuctionDownloader::MimeType::kJavascript;
    AddResponse(&url_loader_factory_, url_, javascript_type, kUtf8Charset,
                kAsciiResponseBody);
    std::unique_ptr<std::string> body = RunRequest();
    ASSERT_TRUE(body);
    EXPECT_EQ(EmptyIfSimulated(kAsciiResponseBody), *body);

    mime_type_ = AuctionDownloader::MimeType::kJson;
    AddResponse(&url_loader_factory_, url_, javascript_type, kUtf8Charset,
                kAsciiResponseBody);
    EXPECT_FALSE(RunRequest());
    EXPECT_EQ(
        "Rejecting load of https://url.test/script.js due to unexpected MIME "
        "type.",
        last_error_msg());
  }

  for (const char* json_type : kJsonMimeTypes) {
    mime_type_ = AuctionDownloader::MimeType::kJavascript;
    AddResponse(&url_loader_factory_, url_, json_type, kUtf8Charset,
                kAsciiResponseBody);
    EXPECT_FALSE(RunRequest());
    EXPECT_EQ(
        "Rejecting load of https://url.test/script.js due to unexpected MIME "
        "type.",
        last_error_msg());

    mime_type_ = AuctionDownloader::MimeType::kJson;
    AddResponse(&url_loader_factory_, url_, json_type, kUtf8Charset,
                kAsciiResponseBody);
    std::unique_ptr<std::string> body = RunRequest();
    ASSERT_TRUE(body);
    EXPECT_EQ(EmptyIfSimulated(kAsciiResponseBody), *body);
  }
}

TEST_P(AuctionDownloaderTest, Charset) {
  mime_type_ = AuctionDownloader::MimeType::kJson;
  // Unknown/unsupported charsets should result in failure.
  AddResponse(&url_loader_factory_, url_, kJsonMimeType, "fred",
              kAsciiResponseBody);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected charset.",
      last_error_msg());

  AddResponse(&url_loader_factory_, url_, kJsonMimeType, "iso-8859-1",
              kAsciiResponseBody);
  EXPECT_FALSE(RunRequest());
  EXPECT_EQ(
      "Rejecting load of https://url.test/script.js due to unexpected charset.",
      last_error_msg());

  // ASCII charset should restrict response bodies to ASCII characters.
  // (Not relevant in kSimulatedDownload since that doesn't have a body).
  mime_type_ = AuctionDownloader::MimeType::kJavascript;
  if (download_mode() != AuctionDownloader::DownloadMode::kSimulatedDownload) {
    AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kAsciiCharset,
                kUtf8ResponseBody);
    EXPECT_FALSE(RunRequest());
    EXPECT_EQ(
        "Rejecting load of https://url.test/script.js due to unexpected "
        "charset.",
        last_error_msg());
  }

  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kAsciiCharset,
              kAsciiResponseBody);
  std::unique_ptr<std::string> body = RunRequest();
  ASSERT_TRUE(body);
  EXPECT_EQ(EmptyIfSimulated(kAsciiResponseBody), *body);

  // (Not relevant in kSimulatedDownload since that doesn't have a body).
  if (download_mode() != AuctionDownloader::DownloadMode::kSimulatedDownload) {
    AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kAsciiCharset,
                kUtf8ResponseBody);
    EXPECT_FALSE(RunRequest());
    EXPECT_EQ(
        "Rejecting load of https://url.test/script.js due to unexpected "
        "charset.",
        last_error_msg());

    AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kAsciiCharset,
                kNonUtf8ResponseBody);
    EXPECT_FALSE(RunRequest());
    EXPECT_EQ(
        "Rejecting load of https://url.test/script.js due to unexpected "
        "charset.",
        last_error_msg());
  }

  // UTF-8 charset should restrict response bodies to valid UTF-8 characters.
  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kAsciiResponseBody);
  body = RunRequest();
  ASSERT_TRUE(body);
  EXPECT_EQ(EmptyIfSimulated(kAsciiResponseBody), *body);
  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
              kUtf8ResponseBody);
  body = RunRequest();
  ASSERT_TRUE(body);
  EXPECT_EQ(EmptyIfSimulated(kUtf8ResponseBody), *body);

  // (Not relevant in kSimulatedDownload since that doesn't have a body).
  if (download_mode() != AuctionDownloader::DownloadMode::kSimulatedDownload) {
    AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, kUtf8Charset,
                kNonUtf8ResponseBody);
    EXPECT_FALSE(RunRequest());
    EXPECT_EQ(
        "Rejecting load of https://url.test/script.js due to unexpected "
        "charset.",
        last_error_msg());
  }

  // Null charset should act like UTF-8.
  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, std::nullopt,
              kAsciiResponseBody);
  body = RunRequest();
  ASSERT_TRUE(body);
  EXPECT_EQ(EmptyIfSimulated(kAsciiResponseBody), *body);
  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, std::nullopt,
              kUtf8ResponseBody);
  body = RunRequest();
  ASSERT_TRUE(body);
  EXPECT_EQ(EmptyIfSimulated(kUtf8ResponseBody), *body);

  // (Not relevant in kSimulatedDownload since that doesn't have a body).
  if (download_mode() != AuctionDownloader::DownloadMode::kSimulatedDownload) {
    AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, std::nullopt,
                kNonUtf8ResponseBody);
    EXPECT_FALSE(RunRequest());
    EXPECT_EQ(
        "Rejecting load of https://url.test/script.js due to unexpected "
        "charset.",
        last_error_msg());
  }

  // Empty charset should act like UTF-8.
  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, "",
              kAsciiResponseBody);
  body = RunRequest();
  ASSERT_TRUE(body);
  EXPECT_EQ(EmptyIfSimulated(kAsciiResponseBody), *body);
  AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, "",
              kUtf8ResponseBody);
  body = RunRequest();
  ASSERT_TRUE(body);
  EXPECT_EQ(EmptyIfSimulated(kUtf8ResponseBody), *body);
  // (Not relevant in kSimulatedDownload since that doesn't have a body).
  if (download_mode() != AuctionDownloader::DownloadMode::kSimulatedDownload) {
    AddResponse(&url_loader_factory_, url_, kJavascriptMimeType, "",
                kNonUtf8ResponseBody);
    EXPECT_FALSE(RunRequest());
    EXPECT_EQ(
        "Rejecting load of https://url.test/script.js due to unexpected "
        "charset.",
        last_error_msg());
  }
}

TEST_P(AuctionDownloaderTest, HttpCachedTrustedBiddingSignalsAge2_Cached) {
  // `num_igs_for_trusted_bidding_signals_kvv1_` is only supported by the
  // other constructors.
  if (constructor_choice() == ConstructorChoice::kStartLoadInBrowser) {
    return;
  }
  network::URLLoaderCompletionStatus status;
  num_igs_for_trusted_bidding_signals_kvv1_ = 1;

  base::HistogramTester histogram_tester;
  auto response_head = CreateResponseHead();
  response_head->was_fetched_via_cache = true;
  response_head->original_response_time = base::Time::Now() - base::Minutes(2);
  url_loader_factory_.AddResponse(url_, std::move(response_head),
                                  kAsciiResponseBody, status);
  std::unique_ptr<std::string> body = RunRequest();
  histogram_tester.ExpectUniqueSample(kCachedTrustedBiddingSignalsAge,
                                      base::Minutes(2).InMilliseconds(), 1);
  histogram_tester.ExpectTotalCount(kBiddingSignalsResponseDownloadTime, 1);
  histogram_tester.ExpectTotalCount(kBiddingSignalsResponseDownloadTimePerIG,
                                    1);
  histogram_tester.ExpectTotalCount(
      kBiddingSignalsResponseDownloadTimeAfterFirstDownloadTimePerIG, 1);
}

TEST_P(AuctionDownloaderTest, HttpCachedTrustedBiddingSignalsAge2_NotCached) {
  // `num_igs_for_trusted_bidding_signals_kvv1_` is only supported by the
  // other constructors.
  if (constructor_choice() == ConstructorChoice::kStartLoadInBrowser) {
    return;
  }
  network::URLLoaderCompletionStatus status;
  num_igs_for_trusted_bidding_signals_kvv1_ = 2;

  base::HistogramTester histogram_tester;
  auto response_head = CreateResponseHead();
  url_loader_factory_.AddResponse(url_, std::move(response_head),
                                  kAsciiResponseBody, status);
  std::unique_ptr<std::string> body = RunRequest();
  histogram_tester.ExpectTotalCount(kCachedTrustedBiddingSignalsAge, 0);
  histogram_tester.ExpectTotalCount(kBiddingSignalsResponseDownloadTime, 1);
  histogram_tester.ExpectTotalCount(kBiddingSignalsResponseDownloadTimePerIG,
                                    1);
  histogram_tester.ExpectTotalCount(
      kBiddingSignalsResponseDownloadTimeAfterFirstDownloadTimePerIG, 1);
}

TEST_P(AuctionDownloaderTest, HttpCachedTrustedBiddingSignalsAge2_NotKVV1) {
  network::URLLoaderCompletionStatus status;
  num_igs_for_trusted_bidding_signals_kvv1_ = std::nullopt;

  base::HistogramTester histogram_tester;
  auto response_head = CreateResponseHead();
  response_head->was_fetched_via_cache = true;
  response_head->original_response_time = base::Time::Now() - base::Minutes(2);
  url_loader_factory_.AddResponse(url_, std::move(response_head),
                                  kAsciiResponseBody, status);
  std::unique_ptr<std::string> body = RunRequest();
  histogram_tester.ExpectTotalCount(kCachedTrustedBiddingSignalsAge, 0);
  histogram_tester.ExpectTotalCount(kBiddingSignalsResponseDownloadTime, 0);
  histogram_tester.ExpectTotalCount(kBiddingSignalsResponseDownloadTimePerIG,
                                    0);
  histogram_tester.ExpectTotalCount(
      kBiddingSignalsResponseDownloadTimeAfterFirstDownloadTimePerIG, 0);
}

TEST_P(AuctionDownloaderTest, StaleWhileRevalidate) {
  // Stale-while-revalidate is only supported by the other constructors.
  if (constructor_choice() == ConstructorChoice::kStartLoadInBrowser) {
    return;
  }
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeature(
      features::kFledgeAuctionDownloaderStaleWhileRevalidate);
  network::URLLoaderCompletionStatus status;

  auto response_head = network::mojom::URLResponseHead::New();
  response_head->async_revalidation_requested = true;
  response_head->headers = net::HttpResponseHeaders::TryToCreate(
      "HTTP/1.1 200 OK\r\nAd-Auction-Allowed: true");

  url_loader_factory_.ClearResponses();
  run_loop_ = std::make_unique<base::RunLoop>();
  std::unique_ptr<AuctionDownloader> downloader = MakeDownloader();

  EXPECT_EQ(url_loader_factory_.total_requests(), 1u);
  EXPECT_EQ(url_loader_factory_.NumPending(), 1);
  network::ResourceRequest initial_request =
      url_loader_factory_.GetPendingRequest(0)->request;
  EXPECT_EQ(initial_request.url, url_);
  EXPECT_TRUE(net::LOAD_SUPPORT_ASYNC_REVALIDATION &
              initial_request.load_flags);

  EXPECT_TRUE(url_loader_factory_.SimulateResponseForPendingRequest(
      url_, status, std::move(response_head), kAsciiResponseBody));

  // There should be another request to revalidate. It should not have the
  // async revalidaiton load flag. Other than that, it should have the same
  // fields as the previous request.
  EXPECT_EQ(url_loader_factory_.total_requests(), 2u);
  EXPECT_EQ(url_loader_factory_.NumPending(), 1);
  network::ResourceRequest revalidation_request =
      url_loader_factory_.GetPendingRequest(0)->request;
  EXPECT_FALSE(net::LOAD_SUPPORT_ASYNC_REVALIDATION &
               revalidation_request.load_flags);
  EXPECT_EQ(revalidation_request.url, url_);
  revalidation_request.load_flags = initial_request.load_flags;
  EXPECT_TRUE(revalidation_request.EqualsForTesting(initial_request));
}

TEST_P(AuctionDownloaderTest, DoNotSupportRevalidateOnPostRequest) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeature(
      features::kFledgeAuctionDownloaderStaleWhileRevalidate);
  network::URLLoaderCompletionStatus status;

  auto response_head = network::mojom::URLResponseHead::New();
  response_head->headers = net::HttpResponseHeaders::TryToCreate(
      "HTTP/1.1 200 OK\r\nAd-Auction-Allowed: true");

  url_loader_factory_.ClearResponses();
  run_loop_ = std::make_unique<base::RunLoop>();
  std::unique_ptr<AuctionDownloader> downloader =
      MakeDownloader("post_body", "text/javascript");

  // The LOAD_SUPPORT_ASYNC_REVALIDATION flag was not used.
  EXPECT_EQ(url_loader_factory_.total_requests(), 1u);
  EXPECT_EQ(url_loader_factory_.NumPending(), 1);
  EXPECT_FALSE(net::LOAD_SUPPORT_ASYNC_REVALIDATION &
               url_loader_factory_.GetPendingRequest(0)->request.load_flags);
}

TEST_P(AuctionDownloaderTest, DoNotSupportStaleWhileRevalidateWhenDisabled) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndDisableFeature(
      features::kFledgeAuctionDownloaderStaleWhileRevalidate);
  network::URLLoaderCompletionStatus status;

  auto response_head = network::mojom::URLResponseHead::New();
  response_head->headers = net::HttpResponseHeaders::TryToCreate(
      "HTTP/1.1 200 OK\r\nAd-Auction-Allowed: true");

  url_loader_factory_.ClearResponses();
  run_loop_ = std::make_unique<base::RunLoop>();
  std::unique_ptr<AuctionDownloader> downloader = MakeDownloader();

  // The LOAD_SUPPORT_ASYNC_REVALIDATION flag was not used.
  EXPECT_EQ(url_loader_factory_.total_requests(), 1u);
  EXPECT_EQ(url_loader_factory_.NumPending(), 1);
  EXPECT_FALSE(net::LOAD_SUPPORT_ASYNC_REVALIDATION &
               url_loader_factory_.GetPendingRequest(0)->request.load_flags);
}

INSTANTIATE_TEST_SUITE_P(
    /* no label */,
    AuctionDownloaderTest,
    testing::Values(
        std::make_tuple(AuctionDownloader::DownloadMode::kActualDownload,
                        ConstructorChoice::kAdoptInProgressLoad),
        std::make_tuple(AuctionDownloader::DownloadMode::kActualDownload,
                        ConstructorChoice::kStartLoadInWorklet),
        std::make_tuple(AuctionDownloader::DownloadMode::kActualDownload,
                        ConstructorChoice::kStartLoadInBrowser),
        std::make_tuple(AuctionDownloader::DownloadMode::kSimulatedDownload,
                        ConstructorChoice::kStartLoadInWorklet)));

}  // namespace
}  // namespace auction_worklet
