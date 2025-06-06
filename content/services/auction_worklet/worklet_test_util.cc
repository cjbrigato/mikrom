// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/services/auction_worklet/worklet_test_util.h"

#include <memory>
#include <optional>
#include <string>

#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/to_string.h"
#include "base/synchronization/waitable_event.h"
#include "content/public/test/shared_storage_test_utils.h"
#include "content/services/auction_worklet/auction_v8_helper.h"
#include "content/services/auction_worklet/public/cpp/auction_downloader.h"
#include "content/services/auction_worklet/public/cpp/creative_info.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/http/http_util.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/shared_storage.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/test/test_url_loader_factory.h"

namespace auction_worklet {

const char kJavascriptMimeType[] = "application/javascript";
const char kJsonMimeType[] = "application/json";
const char kWasmMimeType[] = "application/wasm";
const char kAdAuctionTrustedSignalsMimeType[] =
    "message/ad-auction-trusted-signals-response";

const char kAllowFledgeHeader[] = "Ad-Auction-Allowed: true";

void AddResponse(network::TestURLLoaderFactory* url_loader_factory,
                 const GURL& url,
                 std::optional<std::string> mime_type,
                 std::optional<std::string> charset,
                 const std::string content,
                 std::optional<std::string> headers,
                 net::HttpStatusCode http_status,
                 network::TestURLLoaderFactory::Redirects redirects) {
  auto head = network::mojom::URLResponseHead::New();
  if (mime_type) {
    head->mime_type = *mime_type;
  }
  if (charset) {
    head->charset = *charset;
  }

  std::string full_headers =
      base::StringPrintf("HTTP/1.1 %d %s\r\n\r\n",
                         static_cast<int>(http_status),
                         net::GetHttpReasonPhrase(http_status)) +
      headers.value_or(std::string());
  head->headers = net::HttpResponseHeaders::TryToCreate(full_headers);
  CHECK(head->headers);

  // WASM handling cares about content-type header, so add one if needed.
  // This doesn't try to escape, and purposefully passes any weird stuff passed
  // in for easier checking of it.
  if (mime_type) {
    std::string content_type_str = *mime_type;
    if (charset) {
      base::StrAppend(&content_type_str, {";charset=", *charset});
    }
    head->headers->SetHeader(net::HttpRequestHeaders::kContentType,
                             content_type_str);
  }

  url_loader_factory->AddResponse(url, std::move(head), content,
                                  network::URLLoaderCompletionStatus(),
                                  std::move(redirects));
}

void AddJavascriptResponse(
    network::TestURLLoaderFactory* url_loader_factory,
    const GURL& url,
    const std::string& content,
    base::optional_ref<const std::string> extra_headers) {
  std::string headers;
  if (!extra_headers.has_value()) {
    headers = kAllowFledgeHeader;
  } else {
    headers = base::StrCat(
        {kAllowFledgeHeader, "\r\n",
         base::TrimWhitespaceASCII(*extra_headers, base::TRIM_ALL)});
  }
  AddResponse(url_loader_factory, url, kJavascriptMimeType,
              /*charset=*/std::nullopt, content, headers);
}

void AddJsonResponse(network::TestURLLoaderFactory* url_loader_factory,
                     const GURL& url,
                     const std::string content) {
  AddResponse(url_loader_factory, url, kJsonMimeType, std::nullopt, content);
}

void AddVersionedJsonResponse(network::TestURLLoaderFactory* url_loader_factory,
                              const GURL& url,
                              const std::string content,
                              uint32_t data_version) {
  std::string headers = base::StringPrintf("%s\nData-Version: %u",
                                           kAllowFledgeHeader, data_version);
  AddResponse(url_loader_factory, url, kJsonMimeType, std::nullopt, content,
              headers);
}

void AddBidderJsonResponse(
    network::TestURLLoaderFactory* url_loader_factory,
    const GURL& url,
    const std::string content,
    std::optional<uint32_t> data_version,
    const std::optional<std::string>& format_version_string) {
  std::string headers = kAllowFledgeHeader;
  if (data_version) {
    headers.append(base::StringPrintf("\nData-Version: %u", *data_version));
  }
  if (format_version_string) {
    headers.append(
        base::StringPrintf("\nAd-Auction-Bidding-Signals-Format-Version:  %s",
                           format_version_string->c_str()));
  }
  AddResponse(url_loader_factory, url, kJsonMimeType, std::nullopt, content,
              headers);
}

base::WaitableEvent* WedgeV8Thread(AuctionV8Helper* v8_helper) {
  auto event = std::make_unique<base::WaitableEvent>();
  base::WaitableEvent* event_handle = event.get();
  v8_helper->v8_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](std::unique_ptr<base::WaitableEvent> event) { event->Wait(); },
          std::move(event)));
  return event_handle;
}

TestAuctionSharedStorageHost::Request::Request(
    network::mojom::SharedStorageModifierMethodWithOptionsPtr
        method_with_options,
    mojom::AuctionWorkletFunction source_auction_worklet_function)
    : method_with_options(std::move(method_with_options)),
      source_auction_worklet_function(source_auction_worklet_function) {}

TestAuctionSharedStorageHost::Request::~Request() = default;

TestAuctionSharedStorageHost::Request::Request(const Request& other)
    : method_with_options(other.method_with_options->Clone()),
      source_auction_worklet_function(other.source_auction_worklet_function) {}

TestAuctionSharedStorageHost::Request&
TestAuctionSharedStorageHost::Request::operator=(const Request& other) {
  if (this != &other) {
    method_with_options = other.method_with_options->Clone();
    source_auction_worklet_function = other.source_auction_worklet_function;
  }
  return *this;
}

TestAuctionSharedStorageHost::Request::Request(Request&& other) = default;
TestAuctionSharedStorageHost::Request&
TestAuctionSharedStorageHost::Request::operator=(Request&& other) = default;

bool TestAuctionSharedStorageHost::Request::operator==(
    const Request& rhs) const = default;

TestAuctionSharedStorageHost::BatchRequest::BatchRequest(
    std::vector<network::mojom::SharedStorageModifierMethodWithOptionsPtr>
        methods_with_options,
    const std::optional<std::string>& with_lock,
    mojom::AuctionWorkletFunction source_auction_worklet_function)
    : methods_with_options(std::move(methods_with_options)),
      with_lock(with_lock),
      source_auction_worklet_function(source_auction_worklet_function) {}

TestAuctionSharedStorageHost::BatchRequest::~BatchRequest() = default;

TestAuctionSharedStorageHost::BatchRequest::BatchRequest(
    const BatchRequest& other)
    : methods_with_options(
          content::CloneSharedStorageMethods(other.methods_with_options)),
      with_lock(other.with_lock),
      source_auction_worklet_function(other.source_auction_worklet_function) {}

TestAuctionSharedStorageHost::BatchRequest::BatchRequest(BatchRequest&& other) =
    default;
TestAuctionSharedStorageHost::BatchRequest&
TestAuctionSharedStorageHost::BatchRequest::operator=(BatchRequest&& other) =
    default;

bool TestAuctionSharedStorageHost::BatchRequest::operator==(
    const BatchRequest& rhs) const = default;

TestAuctionSharedStorageHost::TestAuctionSharedStorageHost() = default;

TestAuctionSharedStorageHost::~TestAuctionSharedStorageHost() = default;

void TestAuctionSharedStorageHost::SharedStorageUpdate(
    network::mojom::SharedStorageModifierMethodWithOptionsPtr
        method_with_options,
    auction_worklet::mojom::AuctionWorkletFunction
        source_auction_worklet_function) {
  observed_requests_.emplace_back(
      Request(std::move(method_with_options), source_auction_worklet_function));
}

void TestAuctionSharedStorageHost::SharedStorageBatchUpdate(
    std::vector<network::mojom::SharedStorageModifierMethodWithOptionsPtr>
        methods_with_options,
    const std::optional<std::string>& with_lock,
    auction_worklet::mojom::AuctionWorkletFunction
        source_auction_worklet_function) {
  observed_batch_requests_.emplace_back(
      BatchRequest(std::move(methods_with_options), with_lock,
                   source_auction_worklet_function));
}

void TestAuctionSharedStorageHost::ClearObservedRequests() {
  observed_requests_.clear();
}

TestAuctionNetworkEventsHandler::TestAuctionNetworkEventsHandler() = default;
TestAuctionNetworkEventsHandler::~TestAuctionNetworkEventsHandler() = default;

void TestAuctionNetworkEventsHandler::OnNetworkSendRequest(
    const ::network::ResourceRequest& request,
    ::base::TimeTicks timestamp) {
  std::string sent_url = "Sent URL: " + request.url.spec();
  observed_requests_.emplace_back(std::move(sent_url));
}

void TestAuctionNetworkEventsHandler::OnNetworkResponseReceived(
    const std::string& request_id,
    const std::string& loader_id,
    const ::GURL& request_url,
    ::network::mojom::URLResponseHeadPtr headers) {
  std::string received_url = "Received URL: " + request_url.spec();
  observed_requests_.emplace_back(std::move(received_url));
}

void TestAuctionNetworkEventsHandler::OnNetworkRequestComplete(
    const std::string& request_id,
    const ::network::URLLoaderCompletionStatus& status) {
  std::string completion_status =
      "Completion Status: " + net::ErrorToString(status.error_code);
  observed_requests_.emplace_back(std::move(completion_status));
}

void TestAuctionNetworkEventsHandler::Clone(
    mojo::PendingReceiver<auction_worklet::mojom::AuctionNetworkEventsHandler>
        receiver) {
  if (receiver.is_valid()) {
    auction_network_events_handlers_.Add(this, std::move(receiver));
  }
}

mojo::PendingRemote<auction_worklet::mojom::AuctionNetworkEventsHandler>
TestAuctionNetworkEventsHandler::CreateRemote() {
  mojo::PendingRemote<mojom::AuctionNetworkEventsHandler>
      auction_network_events_handler_remote;

  Clone(auction_network_events_handler_remote.InitWithNewPipeAndPassReceiver());

  return auction_network_events_handler_remote;
}

const std::vector<std::string>&
TestAuctionNetworkEventsHandler::GetObservedRequests() {
  return observed_requests_;
}

std::set<CreativeInfo> CreateCreativeInfoSet(
    const std::vector<std::string>& urls) {
  std::set<CreativeInfo> result;
  for (const auto& url : urls) {
    CreativeInfo entry;
    entry.ad_descriptor.url = GURL(url);
    result.insert(std::move(entry));
  }
  return result;
}

std::vector<mojom::CreativeInfoWithoutOwnerPtr>
CreateMojoCreativeInfoWithoutOwnerVector(const std::vector<std::string>& urls) {
  std::vector<mojom::CreativeInfoWithoutOwnerPtr> out;
  for (const auto& url : urls) {
    out.push_back(mojom::CreativeInfoWithoutOwner::New(
        blink::AdDescriptor(GURL(url)),
        /*creative_scanning_metadata=*/std::nullopt));
  }
  return out;
}

}  // namespace auction_worklet
