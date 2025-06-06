// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SERVICES_AUCTION_WORKLET_WORKLET_TEST_UTIL_H_
#define CONTENT_SERVICES_AUCTION_WORKLET_WORKLET_TEST_UTIL_H_

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/types/optional_ref.h"
#include "content/services/auction_worklet/public/cpp/creative_info.h"
#include "content/services/auction_worklet/public/mojom/auction_network_events_handler.mojom.h"
#include "content/services/auction_worklet/public/mojom/auction_shared_storage_host.mojom.h"
#include "content/services/auction_worklet/public/mojom/seller_worklet.mojom.h"
#include "content/services/auction_worklet/trusted_signals.h"
#include "net/http/http_status_code.h"
#include "services/network/public/mojom/shared_storage.mojom-forward.h"
#include "services/network/test/test_url_loader_factory.h"
#include "url/gurl.h"

namespace base {
class WaitableEvent;
}  // namespace base

namespace auction_worklet {

class AuctionV8Helper;

// The official Javascript, JSON, and WASM MIME types. For JS and JSON there are
// also other supported MIME types.
extern const char kJavascriptMimeType[];
extern const char kJsonMimeType[];
extern const char kWasmMimeType[];
extern const char kAdAuctionTrustedSignalsMimeType[];

// "X-Allow-Fledge: true" header.
extern const char kAllowFledgeHeader[];

// Enqueues a response to `url_loader_factory` using the specified values.
//
// `headers` contains the HTTP header lines (no status line + header lines) used
// to create the HttpResponseHeaders value. If nullopt, HttpResponseHeaders is
// null, and `http_status` is ignored.
void AddResponse(network::TestURLLoaderFactory* url_loader_factory,
                 const GURL& url,
                 std::optional<std::string> mime_type,
                 std::optional<std::string> charset,
                 const std::string content,
                 std::optional<std::string> headers = kAllowFledgeHeader,
                 net::HttpStatusCode http_status = net::HTTP_OK,
                 network::TestURLLoaderFactory::Redirects redirects =
                     network::TestURLLoaderFactory::Redirects());

// Convenience methods to invoke AddResponse() with the specified MIME type and
// no charset.
void AddJavascriptResponse(
    network::TestURLLoaderFactory* url_loader_factory,
    const GURL& url,
    const std::string& content,
    base::optional_ref<const std::string> extra_headers = std::nullopt);
void AddJsonResponse(network::TestURLLoaderFactory* url_loader_factory,
                     const GURL& url,
                     const std::string content);
void AddVersionedJsonResponse(network::TestURLLoaderFactory* url_loader_factory,
                              const GURL& url,
                              const std::string content,
                              uint32_t data_version);

// Adds a bidder worklet JSON response, optionally with data version and format
// version headers. Defaults to including a header indicating format version 2.
void AddBidderJsonResponse(
    network::TestURLLoaderFactory* url_loader_factory,
    const GURL& url,
    const std::string content,
    std::optional<uint32_t> data_version = std::nullopt,
    const std::optional<std::string>& format_version_string = "2");

// Adds a task to `v8_helper->v8_runner()` that blocks until the return value
// is signaled. The returned event will be deleted afterwards.
base::WaitableEvent* WedgeV8Thread(AuctionV8Helper* v8_helper);

// Receives shared storage mojom messages.
class TestAuctionSharedStorageHost : public mojom::AuctionSharedStorageHost {
 public:
  struct Request {
    Request(network::mojom::SharedStorageModifierMethodWithOptionsPtr
                method_with_options,
            mojom::AuctionWorkletFunction source_auction_worklet_function);
    ~Request();

    Request(const Request& other);
    Request& operator=(const Request& other);
    Request(Request&& other);
    Request& operator=(Request&& other);

    network::mojom::SharedStorageModifierMethodWithOptionsPtr
        method_with_options;
    mojom::AuctionWorkletFunction source_auction_worklet_function;

    bool operator==(const Request& rhs) const;
  };

  struct BatchRequest {
    BatchRequest(
        std::vector<network::mojom::SharedStorageModifierMethodWithOptionsPtr>
            methods_with_options,
        const std::optional<std::string>& with_lock,
        mojom::AuctionWorkletFunction source_auction_worklet_function);
    ~BatchRequest();

    BatchRequest(const BatchRequest& other);
    BatchRequest& operator=(const BatchRequest& other) = delete;
    BatchRequest(BatchRequest&& other);
    BatchRequest& operator=(BatchRequest&& other);

    std::vector<network::mojom::SharedStorageModifierMethodWithOptionsPtr>
        methods_with_options;
    std::optional<std::string> with_lock;
    mojom::AuctionWorkletFunction source_auction_worklet_function;

    bool operator==(const BatchRequest& rhs) const;
  };

  TestAuctionSharedStorageHost();

  ~TestAuctionSharedStorageHost() override;

  // mojom::AuctionSharedStorageHost:
  void SharedStorageUpdate(
      network::mojom::SharedStorageModifierMethodWithOptionsPtr
          method_with_options,
      auction_worklet::mojom::AuctionWorkletFunction
          source_auction_worklet_function) override;
  void SharedStorageBatchUpdate(
      std::vector<network::mojom::SharedStorageModifierMethodWithOptionsPtr>
          methods_with_options,
      const std::optional<std::string>& with_lock,
      auction_worklet::mojom::AuctionWorkletFunction
          source_auction_worklet_function) override;

  const std::vector<Request>& observed_requests() const {
    return observed_requests_;
  }

  const std::vector<BatchRequest>& observed_batch_requests() const {
    return observed_batch_requests_;
  }

  void ClearObservedRequests();

 private:
  std::vector<Request> observed_requests_;
  std::vector<BatchRequest> observed_batch_requests_;
};

class TestAuctionNetworkEventsHandler
    : public mojom::AuctionNetworkEventsHandler {
 public:
  TestAuctionNetworkEventsHandler();
  ~TestAuctionNetworkEventsHandler() override;

  void OnNetworkSendRequest(const ::network::ResourceRequest& request,
                            ::base::TimeTicks timestamp) override;

  void OnNetworkResponseReceived(
      const std::string& request_id,
      const std::string& loader_id,
      const ::GURL& request_url,
      ::network::mojom::URLResponseHeadPtr headers) override;

  void OnNetworkRequestComplete(
      const std::string& request_id,
      const ::network::URLLoaderCompletionStatus& status) override;

  void Clone(
      ::mojo::PendingReceiver<AuctionNetworkEventsHandler> receiver) override;

  mojo::PendingRemote<AuctionNetworkEventsHandler> CreateRemote();

  const std::vector<std::string>& GetObservedRequests();

 private:
  std::vector<std::string> observed_requests_;
  mojo::ReceiverSet<auction_worklet::mojom::AuctionNetworkEventsHandler>
      auction_network_events_handlers_;
};

// A helper to make testing common cases in trusted seller signals easier, by
// construsting the larger type it needs from simpler string input.
std::set<CreativeInfo> CreateCreativeInfoSet(
    const std::vector<std::string>& urls);
std::vector<mojom::CreativeInfoWithoutOwnerPtr>
CreateMojoCreativeInfoWithoutOwnerVector(const std::vector<std::string>& urls);

}  // namespace auction_worklet

#endif  // CONTENT_SERVICES_AUCTION_WORKLET_WORKLET_TEST_UTIL_H_
