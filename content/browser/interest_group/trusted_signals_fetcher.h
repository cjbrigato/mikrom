// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_INTEREST_GROUP_TRUSTED_SIGNALS_FETCHER_H_
#define CONTENT_BROWSER_INTEREST_GROUP_TRUSTED_SIGNALS_FETCHER_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/types/expected.h"
#include "base/unguessable_token.h"
#include "base/values.h"
#include "content/browser/interest_group/data_decoder_manager.h"
#include "content/browser/interest_group/devtools_enums.h"
#include "content/common/content_export.h"
#include "content/public/browser/frame_tree_node_id.h"
#include "content/services/auction_worklet/public/mojom/trusted_signals_cache.mojom.h"
#include "net/http/http_response_headers.h"
#include "net/third_party/quiche/src/quiche/oblivious_http/buffers/oblivious_http_request.h"
#include "services/data_decoder/public/cpp/data_decoder.h"
#include "services/network/public/mojom/ip_address_space.mojom-forward.h"
#include "services/network/public/mojom/url_loader_factory.mojom-forward.h"
#include "services/network/public/mojom/url_response_head.mojom-forward.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace auction_worklet {
class AuctionDownloader;
}

namespace content {

struct BiddingAndAuctionServerKey;

// Single-use network fetcher for versions 2+ of the key-value server API.
// It takes a list compression groups and partitions, and asynchronously returns
// a set of responses, one per compression group. The responses are provided as
// still compressed compression group bodies, so the cache layer can store
// compressed responses and to minimize IPC size. The responses will be
// decompressed before use in the appropriate Javascript process.
//
// Bidding and scoring signals need different structs when sending requests, but
// they use the same response format, since it's only the compressed data itself
// that varies based on signals type.
//
// TODO(https://crbug.com/333445540): This is currently only an API, with no
// implementation. Need to actually implement the API.
class CONTENT_EXPORT TrustedSignalsFetcher {
 public:
  static constexpr std::string_view kRequestMediaType =
      "message/ad-auction-trusted-signals-request";
  static constexpr std::string_view kResponseMediaType =
      "message/ad-auction-trusted-signals-response";

  // All the data needed to request a particular bidding signals partition.
  struct CONTENT_EXPORT BiddingPartition {
    // Pointer arguments must remain valid until the BiddingPartition is
    // destroyed.
    BiddingPartition(int partition_id,
                     const std::set<std::string>* interest_group_names,
                     const std::set<std::string>* keys,
                     const base::Value::Dict* additional_params,
                     const std::string* buyer_tkv_signals);
    BiddingPartition(BiddingPartition&&);

    ~BiddingPartition();

    BiddingPartition& operator=(BiddingPartition&&);

    int partition_id;

    base::raw_ref<const std::set<std::string>> interest_group_names;
    base::raw_ref<const std::set<std::string>> keys;

    // At the moment, valid keys are "experimentGroupId", "slotSize", and
    // "allSlotsRequestedSizes". We could take them separately, but seems better
    // to take one field rather than several?
    base::raw_ref<const base::Value::Dict> additional_params;

    raw_ptr<const std::string> buyer_tkv_signals;
  };

  // All the data needed to request a particular scoring signals partition.
  struct CONTENT_EXPORT ScoringPartition {
    // Pointer arguments must remain valid until the ScoringPartition is
    // destroyed.
    ScoringPartition(int partition_id,
                     const GURL* render_url,
                     const std::set<GURL>* component_render_urls,
                     const base::Value::Dict* additional_params,
                     const std::string* seller_tkv_signals);
    ScoringPartition(ScoringPartition&&);

    ~ScoringPartition();

    ScoringPartition& operator=(ScoringPartition&&);

    int partition_id;

    // Currently, TrustedSignalsCacheImpl puts the values from each bid in its
    // own partition, so there will always be only one `render_url`.
    base::raw_ref<const GURL> render_url;

    base::raw_ref<const std::set<GURL>> component_render_urls;

    // At the moment, valid keys are "experimentGroupId", "slotSize", and
    // "allSlotsRequestedSizes". We could take them separately, but seems better
    // to take one field rather than several?
    base::raw_ref<const base::Value::Dict> additional_params;

    raw_ptr<const std::string> seller_tkv_signals;
  };

  // While buying and scoring signals partitions need different structs when
  // sending requests, the responses use the same format.

  // The received result for a particular compression group. Only returned on
  // success.
  struct CONTENT_EXPORT CompressionGroupResult {
    CompressionGroupResult();
    CompressionGroupResult(CompressionGroupResult&&);

    ~CompressionGroupResult();

    CompressionGroupResult& operator=(CompressionGroupResult&&);

    // The compression scheme used by `compression_group_data`, as indicated by
    // the server.
    auction_worklet::mojom::TrustedSignalsCompressionScheme compression_scheme;

    // The still-compressed data for the compression group.
    base::Value::BlobStorage compression_group_data;

    // Time until the response expires.
    base::TimeDelta ttl;
  };

  // A map of compression group ids to results, in the case of success.
  using CompressionGroupResultMap = std::map<int, CompressionGroupResult>;

  // The result of a fetch. Either the entire fetch succeeds or it fails with a
  // single error.
  using SignalsFetchResult =
      base::expected<CompressionGroupResultMap, std::string>;

  using Callback = base::OnceCallback<void(SignalsFetchResult)>;

  TrustedSignalsFetcher();

  // Virtual for tests.
  virtual ~TrustedSignalsFetcher();

  TrustedSignalsFetcher(const TrustedSignalsFetcher&) = delete;
  TrustedSignalsFetcher& operator=(const TrustedSignalsFetcher&) = delete;

  // `data_decoder_manager` must outlive the fetcher.
  //
  // `frame_tree_node_id` and `devtools_auction_ids` are used to log events for
  // devtools, if needed.
  //
  // `main_frame_origin` and `network_partition_nonce` are used to create an
  // IsolationInfo identifying the network partition to use.
  // `main_frame_origin`'s host is also sent as part of the encrypted request.
  //
  // `ip_address_space` is the IPAddressSpace of the frame that's running the
  // auction. It's used to create a ClientSecurityState that has a
  // PrivateNetworkRequestPolicy that will block the signals fetch if the
  // signals URL maps to an IP on a less public address space. The other members
  // of ClientSecurityState use default values.  Default values are safe since
  // these are credentialless requests. Any data taken from the frame would also
  // potentially be a leak.
  //
  // `script_origin` is the owner of the interest group the request is for. Used
  // as the initiator for CORS.
  //
  // `compression_groups` is a map of all partitions in the request, indexed by
  // compression group id. Virtual for tests.
  virtual void FetchBiddingSignals(
      DataDecoderManager& data_decoder_manager,
      network::mojom::URLLoaderFactory* url_loader_factory,
      FrameTreeNodeId frame_tree_node_id,
      base::flat_set<std::string> devtools_auction_ids,
      const url::Origin& main_frame_origin,
      network::mojom::IPAddressSpace ip_address_space,
      base::UnguessableToken network_partition_nonce,
      const url::Origin& script_origin,
      const GURL& trusted_bidding_signals_url,
      const BiddingAndAuctionServerKey& bidding_and_auction_key,
      const std::map<int, std::vector<BiddingPartition>>& compression_groups,
      Callback callback);

  // `data_decoder_manager` must outlive the fetcher.
  //
  // `frame_tree_node_id` and `devtools_auction_ids` are used to log events for
  // devtools, if needed.
  //
  // `main_frame_origin` and `network_partition_nonce` are used to create an
  // IsolationInfo identifying the network partition to use.
  // `main_frame_origin`'s host is also sent as part of the encrypted request.
  //
  // `ip_address_space` is the IPAddressSpace of the frame that's running the
  // auction. It's used to create a ClientSecurityState that has a
  // PrivateNetworkRequestPolicy that will block the signals fetch if the
  // signals URL maps to an IP on a less public address space. The other members
  // of ClientSecurityState use default values.  Default values are safe since
  // these are credentialless requests. Any data taken from the frame would also
  // potentially be a leak.
  //
  // `script_origin` is the seller for the auction. Used as the initiator for
  // CORS.
  //
  // `compression_groups` is a map of all partitions in the request, indexed by
  // compression group id. Virtual for tests.
  virtual void FetchScoringSignals(
      DataDecoderManager& data_decoder_manager,
      network::mojom::URLLoaderFactory* url_loader_factory,
      FrameTreeNodeId frame_tree_node_id,
      base::flat_set<std::string> devtools_auction_ids,
      const url::Origin& main_frame_origin,
      network::mojom::IPAddressSpace ip_address_space,
      base::UnguessableToken network_partition_nonce,
      const url::Origin& script_origin,
      const GURL& trusted_scoring_signals_url,
      const BiddingAndAuctionServerKey& bidding_and_auction_key,
      const std::map<int, std::vector<ScoringPartition>>& compression_groups,
      Callback callback);

 private:
  // Encrypts `plaintext_body` using `bidding_and_auction_key`, and then creates
  // a SimpleURLLoader and starts a request. Once the request body has been
  // created, everything else (including response body parsing) is identical for
  // bidding and scoring signals, as only the data inside compression groups is
  // different for bidding and scoring signals, and that layer is not parsed by
  // this class.
  void EncryptRequestBodyAndStart(
      DataDecoderManager& data_decoder_manager,
      network::mojom::URLLoaderFactory* url_loader_factory,
      InterestGroupAuctionFetchType fetch_type,
      FrameTreeNodeId frame_tree_node_id,
      base::flat_set<std::string> devtools_auction_ids,
      const url::Origin& main_frame_origin,
      network::mojom::IPAddressSpace ip_address_space,
      base::UnguessableToken network_partition_nonce,
      const url::Origin& script_origin,
      const GURL& trusted_signals_url,
      const BiddingAndAuctionServerKey& bidding_and_auction_key,
      std::string plaintext_request_body,
      Callback callback);

  void OnRequestComplete(std::unique_ptr<std::string> response_body,
                         scoped_refptr<net::HttpResponseHeaders> headers,
                         std::optional<std::string> error);

  void OnCborParsed(data_decoder::DataDecoder::ValueOrError value_or_error);

  // Attempts to parse the base::Value result from having the DataDecoder parse
  // the CBOR contents of the fetch.
  SignalsFetchResult ParseDataDecoderResult(
      data_decoder::DataDecoder::ValueOrError value_or_error);

  // Attempts to parse a single compression group object.
  // `compression_group_value` should be a value from the `compressionGroups`
  // array of the parsed CBOR value. On success, returns a
  // CompressionGroupResult and sets `compression_group_id` to the ID from the
  // passed in value. On failure, leaves `compression_group_id` alone, and
  // returns a string.
  base::expected<CompressionGroupResult, std::string> ParseCompressionGroup(
      base::Value compression_group_value,
      int& compression_group_id);

  // Returns a string error message, prefixing the passed in message with the
  // URL.
  std::string CreateError(const std::string& error_message);

  // The URL being fetched. Cached for using in error strings.
  GURL trusted_signals_url_;
  Callback callback_;
  std::unique_ptr<auction_worklet::AuctionDownloader> auction_downloader_;

  // Context needed to decrypt the response. Initialized while encrypting the
  // request body.
  std::unique_ptr<quiche::ObliviousHttpRequest::Context> ohttp_context_;

  // Used to parse the CBOR response. Created when fetch starts, to pre-warm the
  // decoder process.
  std::unique_ptr<DataDecoderManager::Handle> decoder_handle_;

  // Compression scheme used by all compression groups. Populated when reading
  // the response.
  auction_worklet::mojom::TrustedSignalsCompressionScheme compression_scheme_ =
      auction_worklet::mojom::TrustedSignalsCompressionScheme::kNone;

  base::WeakPtrFactory<TrustedSignalsFetcher> weak_ptr_factory_{this};
};

}  // namespace content

#endif  // CONTENT_BROWSER_INTEREST_GROUP_TRUSTED_SIGNALS_FETCHER_H_
