// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_INTEREST_GROUP_TRUSTED_SIGNALS_CACHE_IMPL_H_
#define CONTENT_BROWSER_INTEREST_GROUP_TRUSTED_SIGNALS_CACHE_IMPL_H_

#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "base/containers/lru_cache.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/types/optional_ref.h"
#include "base/unguessable_token.h"
#include "base/values.h"
#include "content/browser/interest_group/trusted_signals_fetcher.h"
#include "content/common/content_export.h"
#include "content/public/browser/frame_tree_node_id.h"
#include "content/services/auction_worklet/public/mojom/trusted_signals_cache.mojom.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/mojom/ip_address_space.mojom-forward.h"
#include "third_party/blink/public/mojom/interest_group/interest_group_types.mojom-forward.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace content {

struct BiddingAndAuctionServerKey;
class DataDecoderManager;

// Handles caching (not yet implemented) and dispatching of trusted bidding and
// scoring signals requests. Only handles requests to Trusted Execution
// Environments (TEEs), i.e., versions 2+ of the protocol, so does not handle
// legacy bring-your-own-server (BYOS) requests. The browser process makes a
// request and gets a Handle and a partition ID, which can then be used to fetch
// the response through the Mojo auction_worklet::mojom::TrustedSignalsCache
// API provided by the Cache. The Handle and partition ID are provided
// immediately on invocation, but the network request may not be sent out
// immediately.
//
// The values it vends are guaranteed to remain valid at least until the Handle
// they were returned with is destroyed. Having the cache in the browser process
// allows requests to be sent while the Javascript process is still starting up,
// and allows the cache to live beyond the shutdown of the often short-live
// Javascript processes.
//
// Internally, it uses 4 maps:
//
// * `fetches_`, a multimap of pending/live Fetches, with FetchKeys consisting
// of what must be the same to share a fetch. On fetch completion, ownership of
// the response is passed to the corresponding CompressionGroupData(s) and the
// Fetch is deleted. See FetchKey for more details on why this is a multimap
// rather than a map.
//
// * `compression_group_data_map_`, a map of UnguessableTokens
// (`compression_group_tokens`) to CompressionGroupData, which contain the still
// compressed response for a single partition group within a fetch. A
// CompressionGroupData may have one or more partitions, each of which
// corresponds to a single [Bidding|Scoring]CacheEntry. The lifetime of
// CompressionGroupData is scoped to the Handle objects returned by the Cache.
//
// * `bidding_cache_entries_`, a map of BiddingCacheEntries, with
// BiddingCacheKeys consisting of what must be the same to share a Fetch, a
// compression group, and partition within the group. Fields that can be merged
// between requests to share a partitiong (e.g., trusted signals keys) are part
// of entry itself, not the key. This is a map, not a multimap, so if a
// BiddingCacheEntry cannot be reused (with or without modification) to suit the
// needs of an incoming request, the BiddingCacheEntry is deleted, and removed
// from its CompressionGroupData. Destroying a BiddingCacheEntry in this way
// will not destroy the CompressionGroupData, or the CompressionGroupData's
// fetch, if it has one.
//
// Fetches and CacheEntries have pointers to the corresponding
// CompressionGroupData, while the CompressionGroupData owns the corresponding
// values in the other two maps. Deleting a CompressionGroupData removes the
// corresponding values in the two maps. One CompressionGroupData may own
// multiple CacheEntries, but will only own one live/pending Fetch. Ownership of
// a Fetch may be shared by multiple CompressionGroupData objects with matching
// FetchKeys.
//
// Each handed out Handle object will keep its corresponding
// CompressionGroupData alive until the handle is destroyed.
//
// TODO(https://crbug.com/333445540): May need some sort of rate limit and size
// cap. Currently, this class creates an arbitrary number of downloads, and
// potentially stores an unlimited amount of data in browser process memory.
class CONTENT_EXPORT TrustedSignalsCacheImpl
    : public auction_worklet::mojom::TrustedSignalsCache {
 public:
  enum class SignalsType {
    kBidding,
    kScoring,
  };

  // Callback to retrieve a BiddingAndAuctionServerKey for a given coordinator.
  // The `callback` parameter may be invoked synchronously or asynchronously,
  // and may fail.
  using GetCoordinatorKeyCallback = base::RepeatingCallback<void(
      const url::Origin& scope_origin,
      const std::optional<url::Origin>& coordinator,
      base::OnceCallback<void(
          base::expected<BiddingAndAuctionServerKey, std::string>)> callback)>;

  // The cached compression group of a trusted signals response, or an error
  // message. May be for bidding signals or scoring signals, but not both.
  // CompressionGroupData are indexed by UnguessableTokens which can be used to
  // retrieve them over the auction_worklet::mojom::TrustedSignalsCache Mojo
  // interface.
  //
  // Public so that Handle can depend on it.
  //
  // CompressionGroupData objects are created when RequestTrusted*Signals() is
  // called and can't reuse an existing one, at which point a new or existing
  // Fetch in `fetch_map_` is also associated with the CompressionGroupData.
  // Each CompressionGroupData owns all CacheEntries that refer to it, and the
  // compression group of the associated fetch as well.  No two
  // CompressionGroupData objects represent the same compression group from a
  // single Fetch.
  //
  // CompressionGroupData objects are refcounted, and when the last reference is
  // released, all associated CacheEntries are destroyed, and the compression
  // group of the associated fetch (if the fetche associated with the
  // CompressionGroupData has not yet completed) is destroyed as well.
  class CompressionGroupData;

  // As long as a Handle is alive, any Mojo
  // auction_worklet::mojom::TrustedSignalsCache created by invoking
  // CreateMojoPipe() can retrieve the response associated with the
  // corresponding signals response ID, which will not change for the lifetime
  // of the handle. The ID can be used to request a response from the cache at
  // any point in time, but the fetch may be made asynchronously, so there's no
  // guarantee of a timely response.
  //
  // Any pending or future requests through a handed out
  // auction_worklet::mojom::TrustedSignalsCache pipe for the
  // `compression_group_token` associated with a destroyed Handle will be sent
  // an error message.
  //
  // All outstanding Handles must be destroyed before the underlying
  // CompressionGroupData may be destroyed.
  //
  // Handles must be destroyed before the TrustedSignalsCacheImpl that created
  // them.
  class CONTENT_EXPORT Handle {
   public:
    // Takes ownership of a reference to CompressionGroupData.
    Handle(TrustedSignalsCacheImpl* trusted_signals_cache,
           scoped_refptr<CompressionGroupData> compression_group_data);
    Handle(Handle&) = delete;

    ~Handle();

    Handle& operator=(Handle&) = delete;

    // The token that needs to be passed to GetTrustedSignals() to retrieve the
    // response through the auction_worklet::mojom::TrustedSignalsCache API.
    // Will not change for the lifetime of the handle.
    base::UnguessableToken compression_group_token() const;

    // Attempts to start the network fetch, if it hasn't started already. Not
    // guaranteed to immediately start the fetch, as it may currently be
    // retrieving the coordinator key. If this isn't called within
    // `kAutoStartDelay` of a fetch being created, it will automatically be
    // invoked for the fetch. Note that since fetches may be reused, it's
    // possible for a fetch of any age to be assigned to a new Handle, and for
    // another Handle to start the fetch assigned to a Handle.
    //
    // Handles that share a `compression_group_token` always share a Fetch,
    // though other Handles may share the fetch as well.
    void StartFetch();

   protected:
    const raw_ptr<TrustedSignalsCacheImpl> trusted_signals_cache_;

    // The underlying CompressionGroupData. Only released on destruction.
    scoped_refptr<CompressionGroupData> compression_group_data_;
  };

  // The maximum size of the cache, in bytes. There is currently no limit on the
  // size taken up by entries that are actively in use (i.e., have outstanding
  // Handles), but entries that are not in used can never drive up the
  // approximated cache size above this value.
  static constexpr size_t kMaxCacheSizeBytes = 10 * 1024 * 1024;

  static constexpr size_t kNonceCacheSize = 50;

  // If data for a compression group has no outstanding Handle for this amount
  // of time, it will be removed to save memory. Entries are guaranteed to be
  // destroyed between `kMinUnusedCleanupTime` and `kMaxUnusedCleanupTime` times
  // - the logic records when the minimum time is when a compression group is
  // added to the LruList, but the shared-across-groups cleanup timer is set to
  // run after the expiration time of the next group to expire plus the
  // `kCleanupInterval` has passed, and then cleans up all entries that are past
  // the `kMinUnusedCleanupTime`. This avoids potentially running a bunch of
  // very short timers when there are a lot of entries that expire at about the
  // same time. Unclear if the extra complexity is worth it.
  //
  // None of this logic currently takes into account when a compression group's
  // TTL from the server has been reached.
  static constexpr base::TimeDelta kMinUnusedCleanupTime = base::Minutes(10);
  static constexpr base::TimeDelta kCleanupInterval = base::Seconds(30);
  static constexpr base::TimeDelta kMaxUnusedCleanupTime =
      kMinUnusedCleanupTime + kCleanupInterval;

  // If StartFetch() isn't called on any handle for a request that has been
  // around this long, automatically call SetFetchCanStart() for the fetch.
  static constexpr base::TimeDelta kAutoStartDelay = base::Milliseconds(10);

  TrustedSignalsCacheImpl(
      DataDecoderManager* data_decoder_manager,
      GetCoordinatorKeyCallback get_coordinator_key_callback);
  ~TrustedSignalsCacheImpl() override;

  TrustedSignalsCacheImpl(const TrustedSignalsCacheImpl&) = delete;
  TrustedSignalsCacheImpl& operator=(const TrustedSignalsCacheImpl&) = delete;

  // Creates a TrustedSignalsCache Remote to be sent to a Protected Audiences
  // JavaScript process. It may only be used for `signals_type` fetches for
  // `script_origin`, where `origin` is origin of the script that will receive
  // those signals (i.e., the seller origin or InterestGroup owner).
  mojo::PendingRemote<auction_worklet::mojom::TrustedSignalsCache> CreateRemote(
      SignalsType signals_type,
      const url::Origin& script_origin);

  // Requests bidding signals for the specified interest group. Return value is
  // a Handle which must be kept alive until the response to the request is no
  // longer needed, and which provides a key to identify the response. Also
  // returns `partition_id`, which identifies the partition within the
  // compression group identified by Handle::compression_group_token() that will
  // have the relevant response.
  //
  // Never starts a network fetch synchronously. Bidder signals are requested
  // over the network after a post task.
  //
  // `rfh_token` is needed to treat the request as if it came from a specific
  // frame.
  //
  // `url_loader_factory` is use to fetch the request, and should also be tied
  // to the source frame, to ensure that the network request goes through any
  // extensions.
  std::unique_ptr<Handle> RequestTrustedBiddingSignals(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      FrameTreeNodeId frame_tree_node_id,
      const std::string& devtools_auction_id,
      const url::Origin& main_frame_origin,
      network::mojom::IPAddressSpace ip_address_space,
      const url::Origin& interest_group_owner,
      const std::string& interest_group_name,
      blink::mojom::InterestGroup_ExecutionMode execution_mode,
      const url::Origin& joining_origin,
      const GURL& trusted_signals_url,
      const url::Origin& coordinator,
      base::optional_ref<const std::vector<std::string>>
          trusted_bidding_signals_keys,
      base::Value::Dict additional_params,
      base::optional_ref<const std::string> buyer_tkv_signals,
      int& partition_id);

  // Requests scoring signals. Return value is a Handle which must be kept alive
  // until the response to the request is no longer needed, and which provides a
  // key to identify the response. Also returns `partition_id`, which identifies
  // the partition within the compression group identified by
  // Handle::compression_group_token() that will have the relevant response.
  //
  // `interest_group_owner` and `joining_origin` are never sent over the wire,
  // but are instead both used to determine if different compression groups
  // should be used.
  //
  // Never starts a network fetch synchronously. Scoring signals are requested
  // over the network after a post task.
  //
  // `rfh_token` is needed to treat the request as if it came from a specific
  // frame.
  //
  // `url_loader_factory` is use to fetch the request, and should also be tied
  // to the source frame, to ensure that the network request goes through any
  // extensions.
  std::unique_ptr<TrustedSignalsCacheImpl::Handle> RequestTrustedScoringSignals(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      FrameTreeNodeId frame_tree_node_id,
      const std::string& devtools_auction_id,
      const url::Origin& main_frame_origin,
      network::mojom::IPAddressSpace ip_address_space,
      const url::Origin& seller,
      const GURL& trusted_signals_url,
      const url::Origin& coordinator,
      const url::Origin& interest_group_owner,
      const url::Origin& joining_origin,
      const GURL& render_url,
      const std::vector<GURL>& component_render_urls,
      base::Value::Dict additional_params,
      base::optional_ref<const std::string> seller_tkv_signals,
      int& partition_id);

  // TrustedSignalsFetcher implementation:
  void GetTrustedSignals(
      const base::UnguessableToken& compression_group_token,
      mojo::PendingRemote<auction_worklet::mojom::TrustedSignalsCacheClient>
          client) override;

  // Returns the estimated size of all CompressionGroupData currently stored in
  // the cache.
  size_t size_for_testing() const { return size_; }

  // Returns the number of compression groups currently in the cache. Includes
  // groups whose data has yet to be fetched, groups with live Handles, and
  // groups being kept alive by `lru_list_`.
  size_t num_groups_for_testing() const {
    return compression_group_data_map_.size();
  }

 private:
  // List of most recently used CompressionGroupData objects that aren't
  // associated without any Handle, to keep alive data that's not currently in
  // used. "Recency" here is defined by defined by how recently the last
  // remaining Handle using them was destroyed. CompressionGroupData with live
  // Handles are not included in this list.
  //
  // Entries are added to the back on Handle destruction if there are no other
  // outstanding Handles (among other requirements), and removed from the front
  // when the size limit is exceeded. Entries are also removed when a Handle is
  // created for a CompressionGroupData. Other than this removal behavior, the
  // list is used in a FIFO manner.
  using LruList = std::list<scoped_refptr<CompressionGroupData>>;

  // Each receiver pipe in `receiver_set_` is restricted to only receive
  // scoring/bidding signals for the specific script origin identified by this
  // struct.
  struct ReceiverRestrictions {
    bool operator==(const ReceiverRestrictions& other) const;

    SignalsType signals_type;
    url::Origin script_origin;
  };

  // Values that prevent sharing network nonces used to distinghuish network
  // partitions used to fetch the signals. Note that the network partition also
  // uses the top frame site in addition to the nonce, so there's no need to
  // include that.
  //
  // Since the `signals_url` is sent to the untrusted server in front of the
  // TEE, using the same network partition for different signals URLs would leak
  // data, so key on that.
  //
  // Sharing partitions for different owners or different signals types also
  // seems potentially leaky, so include those as well.
  struct NetworkPartitionNonceKey {
    NetworkPartitionNonceKey();
    NetworkPartitionNonceKey(const url::Origin& script_origin,
                             SignalsType signals_type,
                             const GURL& trusted_signals_url);
    NetworkPartitionNonceKey(const NetworkPartitionNonceKey&);
    NetworkPartitionNonceKey(NetworkPartitionNonceKey&&);
    ~NetworkPartitionNonceKey();

    NetworkPartitionNonceKey& operator=(const NetworkPartitionNonceKey&);
    NetworkPartitionNonceKey& operator=(NetworkPartitionNonceKey&&);

    bool operator<(const NetworkPartitionNonceKey& other) const;

    // Values are roughly in order of what is expected to be most performant for
    // comparisons.
    url::Origin script_origin;
    SignalsType signals_type;
    GURL trusted_signals_url;
  };

  // Key used for live or pending requests to a trusted server. Two request with
  // the same FetchKey can be merged together, but the requests themselves may
  // differ in other fields. Before the network request is started, any request
  // with a matching fetch key may be merged into a single request. Once the
  // network request is started, however, new requests may only be merged into
  // the live request if there's a matching CacheEntry that has already
  // requested all information needed for the request.
  //
  // There may be multiple requests at once with the same FetchKey, in the case
  // a network request was started before a new request came in with values that
  // do not match any of those in the live fetch.
  //
  // Combining requests across main frame origins or owners seems potentially
  // problematic in terms of cross-origin leaks, so partition on those for now,
  // at least.
  struct FetchKey {
    FetchKey();
    // `script_origin` is the origin of the script that will receive the
    // response. For bidding signals fetches, it's the interest group owner. For
    // scoring signals fetches, it's the seller origin (component or top-level,
    // depending on which seller will be receiving the signals).
    FetchKey(scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
             FrameTreeNodeId frame_tree_node_id,
             const url::Origin& main_frame_origin,
             network::mojom::IPAddressSpace ip_address_space,
             SignalsType signals_type,
             const url::Origin& script_origin,
             const GURL& trusted_signals_url,
             const url::Origin& coordinator);
    FetchKey(const FetchKey&);
    FetchKey(FetchKey&&);

    ~FetchKey();

    FetchKey& operator=(const FetchKey&);
    FetchKey& operator=(FetchKey&&);

    bool operator<(const FetchKey& other) const;

    const url::Origin& script_origin() const {
      return network_partition_nonce_key.script_origin;
    }
    SignalsType signals_type() const {
      return network_partition_nonce_key.signals_type;
    }
    const GURL& trusted_signals_url() const {
      return network_partition_nonce_key.trusted_signals_url;
    }

    // Order here matches comparison order in operator<(), and is based on a
    // guess on what order will result in the most performant comparisons.

    NetworkPartitionNonceKey network_partition_nonce_key;

    // Used to fetch the request. Should be tied to the particular document, so
    // potentially more specific than `frame_tree_node_id`.
    //
    // Note that scoped_refptr's equality operator checks for pointer equality,
    // rather than value equality.
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory;

    // The origin of the frame running the auction that needs the signals. This
    // could potentially be used to separate compression groups instead of
    // fetches, but best to be safe.
    url::Origin main_frame_origin;

    url::Origin coordinator;

    network::mojom::IPAddressSpace ip_address_space;

    // Live requests cannot be merged across frames, due to devtools and
    // extensions hooks.
    FrameTreeNodeId frame_tree_node_id;
  };

  // A pending or live network request. May be for bidding signals or scoring
  // signals, but not both.
  struct Fetch;
  using FetchMap = std::multimap<FetchKey, Fetch>;

  // A key that distinguishes bidding signals entries in the cache. The key is
  // used to find all potential matching entries whenever
  // RequestTrustedBiddingSignals() is invoked. A response with one key cannot
  // be used to satisfy a request with another. There are some cases where even
  // when the BiddingCacheKey of a new request matches an existing
  // BiddingCacheEntry, the entry cannot be reused, in which case a new Entry is
  // used and the old one is thrown out (though the CompressionGroupData will
  // remain valid). This can happen in the case of cache expiration or the Entry
  // not having the necessary `trusted_bidding_signals_keys` or
  // `interest_group_name` after the corresponding network request has been sent
  // over the wire.
  struct BiddingCacheKey {
    BiddingCacheKey();
    BiddingCacheKey(BiddingCacheKey&&);

    // `interest_group_name` should be nullopt in the case of the
    // group-by-origin execution mode, in which case all such groups will be
    // pooled together, if the other values match, and the interest group names
    // will be stored as a value in the BiddingCacheEntry, rather than as part
    // of the key.
    BiddingCacheKey(
        const url::Origin& interest_group_owner,
        std::optional<std::string> interest_group_name,
        const GURL& trusted_signals_url,
        const url::Origin& coordinator,
        scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
        FrameTreeNodeId frame_tree_node_id,
        const url::Origin& main_frame_origin,
        network::mojom::IPAddressSpace ip_address_space,
        const url::Origin& joining_origin,
        base::Value::Dict additional_params,
        base::optional_ref<const std::string> buyer_tkv_signals);

    ~BiddingCacheKey();

    BiddingCacheKey& operator=(BiddingCacheKey&&);

    bool operator<(const BiddingCacheKey& other) const;

    // Values where mismatches are expected to be more likely are listed
    // earlier.

    // The interest group name, or nullopt, in the case of the group-by-origin
    // execution mode, as all such interest groups can be fetched together, in a
    // single partition.
    std::optional<std::string> interest_group_name;

    FetchKey fetch_key;
    url::Origin joining_origin;
    base::Value::Dict additional_params;
    std::optional<std::string> buyer_tkv_signals;
  };

  // An indexed entry in the cache for callers of
  // RequestTrustedBiddingSignals(). It maps InterestGroup information and main
  // frame origins to CompressionGroupData objects and partition IDs.
  // BiddingCacheEntries that are sent to a TEE together in the same compressed
  // partition share a CompressionGroupData, but have different partition ids.
  // BiddingCacheEntries are only destroyed when the corresponding
  // CompressionGroupData is destroyed, or when a new BiddingCacheEntry with the
  // same key replaces them.
  struct BiddingCacheEntry;
  using BiddingCacheEntryMap = std::map<BiddingCacheKey, BiddingCacheEntry>;

  // A key that distinguishes scoring signals entries in the cache. The key is
  // used to find all potential matching entries whenever
  // RequestTrustedScoringSignals() is invoked. A response with one key cannot
  // be used to satisfy a request with another.
  //
  // Currently, all parameters of each bid appear in ScoringCacheKeys, unlike
  // BiddingCacheKeys, so there's no need to check if a ScoringCacheEntry has
  // other fields necessary for there to be a cache hit, other than checking the
  // TTL. It may be worth experimenting with matching partitioning with that of
  // BiddingCacheEntries, but it seems less likely to be useful here, since most
  // requests likely have a single renderURL and no componentRenderURLs. And in
  // cases where componentRenderURLs are present, it still seems unlikely that
  // merging requests will make it more likely all the values for a single bid
  // appear in a single ScoringCacheEntry.
  struct ScoringCacheKey {
    ScoringCacheKey();
    ScoringCacheKey(ScoringCacheKey&&);

    ScoringCacheKey(
        const url::Origin& seller,
        const GURL& trusted_signals_url,
        const url::Origin& coordinator,
        scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
        FrameTreeNodeId frame_tree_node_id,
        const url::Origin& main_frame_origin,
        network::mojom::IPAddressSpace ip_address_space,
        const url::Origin& interest_group_owner,
        const url::Origin& joining_origin,
        const GURL& render_url,
        const std::vector<GURL>& component_render_urls,
        base::Value::Dict additional_params,
        base::optional_ref<const std::string> seller_tkv_signals);

    ~ScoringCacheKey();

    ScoringCacheKey& operator=(ScoringCacheKey&&);

    bool operator<(const ScoringCacheKey& other) const;

    // Values where mismatches are expected to be more likely are listed
    // earlier.

    GURL render_url;
    std::set<GURL> component_render_urls;
    FetchKey fetch_key;
    url::Origin joining_origin;
    url::Origin interest_group_owner;
    base::Value::Dict additional_params;
    std::optional<std::string> seller_tkv_signals;
  };

  // An indexed entry in the cache for callers of
  // RequestTrustedScoringSignals(). It information about the bid being scored
  // and main frame origins to CompressionGroupData objects and partition IDs.
  // ScoringCacheEntries that are sent to a TEE together in the same compressed
  // partition share a CompressionGroupData, but have different partition ids.
  // ScoringCacheEntries are only destroyed when the corresponding
  // CompressionGroupData is destroyed, or when they expire (in the latter case,
  // the CompressionGroupData may still have outstanding consumers that have yet
  // to fetch it, so may still be needed).
  struct ScoringCacheEntry;
  using ScoringCacheEntryMap = std::map<ScoringCacheKey, ScoringCacheEntry>;

  // Returns a CompressionGroupData that can be used to fetch and store data
  // associated with the provided FetchKey and joining origin. The returned
  // CompressionGroupData will be associated with a Fetch that has not yet
  // started, either a new one or a shared one. May return a new or existing
  // CompressionGroupData. Queues any newly created fetch. After calling, the
  // caller must associate the returned CompressionGroupData with its
  // CacheEntry.
  //
  // `interest_group_owner_if_scoring_signals` is only needed for scoring
  // signals fetches. For bidding signals, the `script_owner` of `fetch_key` is
  // the `interest_group_owner`, but for scoring signals, it's not, and
  // compression groups need to be split by interest group owner, to protect
  // against cross-origin size leaks due to compression.
  scoped_refptr<TrustedSignalsCacheImpl::CompressionGroupData>
  FindOrCreateCompressionGroupDataAndQueueFetch(
      const FetchKey& fetch_key,
      const url::Origin& joining_origin,
      const std::string& devtools_auction_id,
      base::optional_ref<const url::Origin>
          interest_group_owner_if_scoring_signals);

  // Starts retrieving the coordinator key for the specified fetch. Will invoke
  // StartFetchIfReady() on complete, which may happen synchronously.
  void GetCoordinatorKey(FetchMap::iterator fetch_it);

  // If the key was successfully fetched, sets `coordinator_key` and calls
  // StartFetchIfReady(). On error, calls OnFetchComplete().
  void OnCoordinatorKeyReceived(
      FetchMap::iterator fetch_it,
      base::expected<BiddingAndAuctionServerKey, std::string>
          bidding_and_auction_server_key);

  // Sets `can_start` to true for `fetch_it` and calls StartFetchIfReady(). Does
  // nothing if `can_start` is already set.
  void SetFetchCanStart(FetchMap::iterator fetch_it);

  // Starts the fetch if it has a `coordinator_key` and its `can_start` field is
  // true.
  void StartFetchIfReady(FetchMap::iterator fetch_it);

  // Called by StartFetchIfReady() to request bidding/scoring signals.
  void StartBiddingSignalsFetch(FetchMap::iterator fetch_it);
  void StartScoringSignalsFetch(FetchMap::iterator fetch_it);

  void OnFetchComplete(
      FetchMap::iterator fetch_it,
      TrustedSignalsFetcher::SignalsFetchResult signals_fetch_result);

  // Called when the last Handle that owned CompressionGroupData is destroyed.
  // May add `compression_group_data` to `lru_list_`.
  void OnLastHandleDestroyed(
      scoped_refptr<CompressionGroupData> compression_group_data);

  // Removes `lru_list_it` from LruList, updating the CompressionGroupData's
  // `lru_list_it` pointer to be nullopt. If there are no outstanding references
  // to the CompressionGroupData, this will destroy it. Called by
  // CompressionGroupData when assigning it a handle, or when destroying LruList
  // entries to free up memory.
  void RemoveFromLruList(LruList::iterator lru_list_it);

  // Called when the last reference of a CompressionGroupData object has been
  // released, and it's about to be destroyed. Does the following:
  //
  // * Removes the CompressionGroupData from `compression_group_data_`.
  //
  // * Destroys all CacheEntries associated with it.
  //
  // * If there is a pending Fetch associated with the CompressionGroupData,
  // removes the associated compression block from the Fetch (since the
  // CompressionGroupData corresponds to an entire block), cancelling the
  // Fetch if it has no non-empty cache blocks. Since compression block IDs
  // are not exposed by the API (only partition IDs within the block are),
  // there's no need to maintain compression block IDs.
  //
  // * If there is a live Fetch associated request, the associated compression
  // block isn't cleared, but its pointer to the CompressionGroupData is, and
  // the Fetch is cancelled if it has no remaining compression blocks
  // associated with CompressionGroupData objects.
  void OnCompressionGroupDataDestroyed(
      CompressionGroupData& compression_group_data);

  // Destroys `cache_entry_it` and removes it from the CompressionGroupData that
  // owns it. This does not remove data from the compression group. Its
  // CompressionGroupData must not have a pending fetch, as that would mean the
  // compression group may not retrieve data that a consumer expects it to
  // retrieve, since Fetches rely on cache entries to know what to retrieve when
  // they're started.
  void DestroyBiddingCacheEntry(BiddingCacheEntryMap::iterator cache_entry_it);
  void DestroyScoringCacheEntry(ScoringCacheEntryMap::iterator cache_entry_it);

  base::UnguessableToken GetNetworkPartitionNonce(
      const NetworkPartitionNonceKey& network_partition_nonce_key);

  // Starts `cleanup_lru_timer_` if it's not already running and `lru_list_` is
  // not empty.
  void MaybeStartCleanupTimer();

  // Removes up all entries in `lru_list_` that have passed their scheduled
  // removal time.
  void Cleanup();

  // Virtual for testing.
  virtual std::unique_ptr<TrustedSignalsFetcher> CreateFetcher();

  const raw_ptr<DataDecoderManager> data_decoder_manager_;
  const GetCoordinatorKeyCallback get_coordinator_key_callback_;

  mojo::ReceiverSet<auction_worklet::mojom::TrustedSignalsCache,
                    ReceiverRestrictions>
      receiver_set_;

  // Multimap of live and pending fetches. Fetches are removed on completion and
  // cancellation. When data is requested from the cache, if data needs to be
  // fetched from the network and there's an unstarted pending Fetch with a
  // matching FetchKey, the pending Fetch will always be used to request the
  // additional data. As a result, for any FetchKey, there will be at most one
  // pending Fetch, which will be the last Fetch with that FetchKey, since
  // multimap entries are stored in FIFO order.
  FetchMap fetches_;

  BiddingCacheEntryMap bidding_cache_entries_;
  ScoringCacheEntryMap scoring_cache_entries_;

  // Map of IDs to CompressionGroupData. CompressionGroupData objects are
  // refcounted, and removed from the map whenever the last reference is
  // released, at which point any associated BiddingCacheEntries are destroyed,
  // and the CompressionGroupData removed from any associated Fetch, destroying
  // the Fetch if no longer needed.
  std::map<base::UnguessableToken,
           raw_ptr<CompressionGroupData, CtnExperimental>>
      compression_group_data_map_;

  // Cache of the most recently used network partition nonces. When a nonce is
  // evicted, any network state cached in the network process associated with
  // the nonce will no longer be usable, and if the key for the evicted entry is
  // seen again, a new UnguessableToken will be created.
  base::LRUCache<NetworkPartitionNonceKey, base::UnguessableToken>
      network_partition_nonce_cache_;

  // Approximate size of all loaded CompressionGroupData in
  // `compression_group_data_map_`. Doesn't fully account for all fields in
  // every related data structure. Updated only on load completion and
  // CompressionGroupData destruction - compression groups that haven't been
  // fetched yet are considered to be of size 0.
  size_t size_ = 0;

  // See LruList for details.
  LruList lru_list_;

  base::OneShotTimer cleanup_timer_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_INTEREST_GROUP_TRUSTED_SIGNALS_CACHE_IMPL_H_
