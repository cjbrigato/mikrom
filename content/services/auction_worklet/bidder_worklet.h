// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SERVICES_AUCTION_WORKLET_BIDDER_WORKLET_H_
#define CONTENT_SERVICES_AUCTION_WORKLET_BIDDER_WORKLET_H_

#include <stdint.h>

#include <cmath>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/containers/lru_cache.h"
#include "base/containers/unique_ptr_adapters.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "content/common/content_export.h"
#include "content/services/auction_worklet/auction_v8_helper.h"
#include "content/services/auction_worklet/bidder_worklet_thread_selector.h"
#include "content/services/auction_worklet/deprecated_url_lazy_filler.h"
#include "content/services/auction_worklet/direct_from_seller_signals_requester.h"
#include "content/services/auction_worklet/execution_mode_util.h"
#include "content/services/auction_worklet/public/mojom/auction_shared_storage_host.mojom.h"
#include "content/services/auction_worklet/public/mojom/auction_worklet_service.mojom-forward.h"
#include "content/services/auction_worklet/public/mojom/auction_worklet_service.mojom.h"
#include "content/services/auction_worklet/public/mojom/bidder_worklet.mojom-forward.h"
#include "content/services/auction_worklet/public/mojom/bidder_worklet.mojom.h"
#include "content/services/auction_worklet/public/mojom/private_aggregation_request.mojom.h"
#include "content/services/auction_worklet/public/mojom/real_time_reporting.mojom.h"
#include "content/services/auction_worklet/public/mojom/trusted_signals_cache.mojom-forward.h"
#include "content/services/auction_worklet/report_bindings.h"
#include "content/services/auction_worklet/set_bid_bindings.h"
#include "content/services/auction_worklet/trusted_signals.h"
#include "content/services/auction_worklet/trusted_signals_kvv2_manager.h"
#include "content/services/auction_worklet/trusted_signals_request_manager.h"
#include "content/services/auction_worklet/worklet_loader.h"
#include "gin/dictionary.h"
#include "mojo/public/cpp/bindings/associated_receiver_set.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/struct_ptr.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "third_party/blink/public/common/interest_group/ad_display_size.h"
#include "third_party/blink/public/common/interest_group/auction_config.h"
#include "third_party/blink/public/mojom/interest_group/interest_group_types.mojom-forward.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "v8/include/v8-persistent-handle.h"

namespace v8 {
class UnboundScript;
}  // namespace v8

namespace auction_worklet {

class ContextRecycler;

// Represents a bidder worklet for FLEDGE
// (https://github.com/WICG/turtledove/blob/main/FLEDGE.md). Loads and runs the
// bidder worklet's Javascript.
//
// Each worklet object can only be used to load and run a single script's
// generateBid() and (if the bid is won) reportWin() once.
//
// The BidderWorklet is non-threadsafe, and lives entirely on the main / user
// sequence. It has an internal V8State object that runs scripts, and is only
// used on the V8 sequence.
//
// TODO(mmenke): Make worklets reuseable. Allow a single BidderWorklet instance
// to both be used for two generateBid() calls for different interest groups
// with the same owner in the same auction, and to be used to bid for the same
// interest group in different auctions.
class CONTENT_EXPORT BidderWorklet : public mojom::BidderWorklet,
                                     public mojom::GenerateBidFinalizer {
 public:
  // Deletes the worklet immediately and resets the BidderWorklet's Mojo pipe
  // with the provided description. See mojo::Receiver::ResetWithReason().
  using ClosePipeCallback =
      base::OnceCallback<void(const std::string& description)>;

  using PrivateAggregationRequests =
      std::vector<auction_worklet::mojom::PrivateAggregationRequestPtr>;

  using RealTimeReportingContributions =
      std::vector<auction_worklet::mojom::RealTimeReportingContributionPtr>;

  // Classification of how trusted signals related to this worklet.
  // This is used for histograms, so entries should not be reordered or
  // otherwise renumbered.
  enum class SignalsOriginRelation {
    kNoTrustedSignals,
    kSameOriginSignals,
    kCrossOriginSignals,

    kMaxValue = kCrossOriginSignals
  };

  // Starts loading the worklet script on construction, as well as the trusted
  // bidding data, if necessary. Will then call the script's generateBid()
  // function and invoke the callback with the results. Callback will always be
  // invoked asynchronously, once a bid has been generated or a fatal error has
  // occurred.
  //
  // Data is cached and will be reused by ReportWin().
  //
  // `trusted_signals_kvv2_manager` must remain valid for the lifetime of the
  // BidderWorklet.
  BidderWorklet(
      std::vector<scoped_refptr<AuctionV8Helper>> v8_helpers,
      std::vector<mojo::PendingRemote<mojom::AuctionSharedStorageHost>>
          shared_storage_hosts,
      bool pause_for_debugger_on_start,
      mojo::PendingRemote<network::mojom::URLLoaderFactory>
          pending_url_loader_factory,
      mojo::PendingRemote<auction_worklet::mojom::AuctionNetworkEventsHandler>
          auction_network_events_handler,
      TrustedSignalsKVv2Manager* trusted_signals_kvv2_manager,
      mojom::InProgressAuctionDownloadPtr script_source_load,
      mojom::InProgressAuctionDownloadPtr wasm_helper_load,
      const std::optional<GURL>& trusted_bidding_signals_url,
      const std::string& trusted_bidding_signals_slot_size_param,
      const url::Origin& top_window_origin,
      mojom::AuctionWorkletPermissionsPolicyStatePtr permissions_policy_state,
      std::optional<uint16_t> experiment_group_id,
      mojom::TrustedSignalsPublicKeyPtr public_key);
  explicit BidderWorklet(const BidderWorklet&) = delete;
  ~BidderWorklet() override;
  BidderWorklet& operator=(const BidderWorklet&) = delete;

  // Sets the callback to be invoked on errors which require closing the pipe.
  // Callback will also immediately delete `this`. Not an argument to
  // constructor because the Mojo ReceiverId needs to be bound to the callback,
  // but can only get that after creating the worklet. Must be called
  // immediately after creating a BidderWorklet.
  void set_close_pipe_callback(ClosePipeCallback close_pipe_callback) {
    close_pipe_callback_ = std::move(close_pipe_callback);
  }

  std::vector<int> context_group_ids_for_testing() const;

  uint64_t group_by_origin_key_hash_salt_for_testing() const {
    return thread_selector_.group_by_origin_key_hash_salt_for_testing();
  }

  static bool IsKAnon(const mojom::BidderWorkletNonSharedParams*
                          bidder_worklet_non_shared_params,
                      const std::string& key);

  // This doesn't look at the component ads.
  static bool IsMainAdKAnon(
      const mojom::BidderWorkletNonSharedParams*
          bidder_worklet_non_shared_params,
      const GURL& script_source_url,
      const SetBidBindings::BidAndWorkletOnlyMetadata& bid_and_metadata);

  static bool IsComponentAdKAnon(
      const mojom::BidderWorkletNonSharedParams*
          bidder_worklet_non_shared_params,
      const blink::AdDescriptor& ad_component_descriptor);

  static bool SupportMultiBid();

  // mojom::BidderWorklet implementation:
  void BeginGenerateBid(
      mojom::BidderWorkletNonSharedParamsPtr bidder_worklet_non_shared_params,
      mojom::TrustedSignalsCacheKeyPtr trusted_signals_cache_key,
      mojom::KAnonymityBidMode kanon_mode,
      const url::Origin& interest_group_join_origin,
      const std::optional<GURL>& direct_from_seller_per_buyer_signals,
      const std::optional<GURL>& direct_from_seller_auction_signals,
      const url::Origin& browser_signal_seller_origin,
      const std::optional<url::Origin>& browser_signal_top_level_seller_origin,
      const base::TimeDelta browser_signal_recency,
      bool browser_signal_for_debugging_only_sampling,
      blink::mojom::BiddingBrowserSignalsPtr bidding_browser_signals,
      base::Time auction_start_time,
      const std::optional<blink::AdSize>& requested_ad_size,
      uint16_t multi_bid_limit,
      uint64_t group_by_origin_id,
      uint64_t trace_id,
      mojo::PendingAssociatedRemote<mojom::GenerateBidClient>
          generate_bid_client,
      mojo::PendingAssociatedReceiver<mojom::GenerateBidFinalizer>
          bid_finalizer) override;
  void SendPendingSignalsRequests() override;
  void ReportWin(
      bool is_for_additional_bid,
      const std::optional<std::string>& interest_group_name_reporting_id,
      const std::optional<std::string>& buyer_reporting_id,
      const std::optional<std::string>& buyer_and_seller_reporting_id,
      const std::optional<std::string>& selected_buyer_and_seller_reporting_id,
      const std::optional<std::string>& auction_signals_json,
      const std::optional<std::string>& per_buyer_signals_json,
      const std::optional<GURL>& direct_from_seller_per_buyer_signals,
      const std::optional<std::string>&
          direct_from_seller_per_buyer_signals_header_ad_slot,
      const std::optional<GURL>& direct_from_seller_auction_signals,
      const std::optional<std::string>&
          direct_from_seller_auction_signals_header_ad_slot,
      const std::string& seller_signals_json,
      mojom::KAnonymityStatus kanon_status,
      const GURL& browser_signal_render_url,
      double browser_signal_bid,
      const std::optional<blink::AdCurrency>& browser_signal_bid_currency,
      double browser_signal_highest_scoring_other_bid,
      const std::optional<blink::AdCurrency>&
          browser_signal_highest_scoring_other_bid_currency,
      bool browser_signal_made_highest_scoring_other_bid,
      std::optional<double> browser_signal_ad_cost,
      std::optional<uint16_t> browser_signal_modeling_signals,
      uint8_t browser_signal_join_count,
      uint8_t browser_signal_recency,
      const url::Origin& browser_signal_seller_origin,
      const std::optional<url::Origin>& browser_signal_top_level_seller_origin,
      const std::optional<base::TimeDelta> browser_signal_reporting_timeout,
      std::optional<uint32_t> bidding_signals_data_version,
      const std::optional<std::string>& aggregate_win_signals,
      uint64_t trace_id,
      ReportWinCallback report_win_callback) override;
  void ConnectDevToolsAgent(
      mojo::PendingAssociatedReceiver<blink::mojom::DevToolsAgent> agent,
      uint32_t thread_index) override;

  // mojom::GenerateBidFinalizer implementation.
  void FinishGenerateBid(
      const std::optional<std::string>& auction_signals_json,
      const std::optional<std::string>& per_buyer_signals_json,
      const std::optional<base::TimeDelta> per_buyer_timeout,
      const std::optional<blink::AdCurrency>& expected_buyer_currency,
      const std::optional<GURL>& direct_from_seller_per_buyer_signals,
      const std::optional<std::string>&
          direct_from_seller_per_buyer_signals_header_ad_slot,
      const std::optional<GURL>& direct_from_seller_auction_signals,
      const std::optional<std::string>&
          direct_from_seller_auction_signals_header_ad_slot) override;

 private:
  struct GenerateBidTask {
    GenerateBidTask();
    ~GenerateBidTask();

    base::CancelableTaskTracker::TaskId task_id =
        base::CancelableTaskTracker::kBadTaskId;

    mojom::BidderWorkletNonSharedParamsPtr bidder_worklet_non_shared_params;
    mojom::KAnonymityBidMode kanon_mode;
    std::optional<std::string> auction_signals_json;
    std::optional<std::string> per_buyer_signals_json;
    std::optional<base::TimeDelta> per_buyer_timeout;
    std::optional<blink::AdCurrency> expected_buyer_currency;
    url::Origin browser_signal_seller_origin;
    std::optional<url::Origin> browser_signal_top_level_seller_origin;
    base::TimeDelta browser_signal_recency;
    bool browser_signal_for_debugging_only_sampling;
    blink::mojom::BiddingBrowserSignalsPtr bidding_browser_signals;
    std::optional<blink::AdSize> requested_ad_size;
    uint16_t multi_bid_limit;
    base::Time auction_start_time;
    uint64_t group_by_origin_id;
    uint64_t trace_id;

    // Time where tracing for wait_generate_bid_deps began.
    base::TimeTicks trace_wait_deps_start;
    // How long various inputs were waited for.
    base::TimeDelta wait_code;
    base::TimeDelta wait_trusted_signals;
    base::TimeDelta wait_direct_from_seller_signals;
    base::TimeDelta wait_promises;

    // Time the BidderWorklet finished waiting for trusted bidding signals,
    // used to compute UMA for the time it takes to resume generating bids
    // after downloading signals.
    base::TimeTicks trusted_bidding_signals_download_complete_time;

    // Time where the BidderWorklet finished waiting for GenerateBid
    // dependencies, used to compute start and end times for latency phase UKMs.
    base::TimeTicks generate_bid_start_time;

    // Set while loading a trusted signals via the legacy
    // TrustedSignalsRequestManager is in progress.
    std::unique_ptr<TrustedSignalsRequestManager::Request>
        trusted_bidding_signals_request;

    // Used when there's a KVv2 request managed by the
    // TrustedSignalsKVv2Manager. Not cleared until the generateBid() call is
    // complete, to keep the signals cached in the manager.
    std::unique_ptr<TrustedSignalsKVv2Manager::Request>
        trusted_bidding_signals_kvv2_request;

    // Results of loading trusted bidding signals.
    scoped_refptr<TrustedSignals::Result> trusted_bidding_signals_result;
    // True if failed loading valid trusted bidding signals.
    bool trusted_bidding_signals_fetch_failed = false;
    // Error message returned by attempt to load
    // `trusted_bidding_signals_result`. Errors loading it are not fatal, so
    // such errors are cached here and only reported on bid completion.
    std::optional<std::string> trusted_bidding_signals_error_msg;

    // Set to true once the callback sent to the OnBiddingSignalsReceived()
    // method of `generate_bid_client` has been invoked. the Javascript
    // generateBid() method will not be run until that happens.
    bool signals_received_callback_invoked = false;

    // Set to true once FinishGenerateBid bound to this task is called.
    bool finalize_generate_bid_called = false;

    // Which receiver id the GenerateBidFinalizer pipe bound to this task
    // is registered under.
    std::optional<mojo::ReceiverId> finalize_generate_bid_receiver_id;

    // Set while loading is in progress.
    std::unique_ptr<DirectFromSellerSignalsRequester::Request>
        direct_from_seller_request_per_buyer_signals;
    std::unique_ptr<DirectFromSellerSignalsRequester::Request>
        direct_from_seller_request_auction_signals;
    // Results of loading DirectFromSellerSignals.
    DirectFromSellerSignalsRequester::Result
        direct_from_seller_result_per_buyer_signals;
    DirectFromSellerSignalsRequester::Result
        direct_from_seller_result_auction_signals;
    // DirectFromSellerSignals errors are fatal, so no error information is
    // stored here.

    // Header-based DirectFromSellerSignals.
    std::optional<std::string>
        direct_from_seller_per_buyer_signals_header_ad_slot;
    std::optional<std::string>
        direct_from_seller_auction_signals_header_ad_slot;

    mojo::AssociatedRemote<mojom::GenerateBidClient> generate_bid_client;
  };

  using GenerateBidTaskList = std::list<GenerateBidTask>;

  struct ReportWinTask {
    ReportWinTask();
    ~ReportWinTask();

    bool is_for_additional_bid;
    std::optional<std::string> interest_group_name_reporting_id;
    std::optional<std::string> buyer_reporting_id;
    std::optional<std::string> buyer_and_seller_reporting_id;
    std::optional<std::string> selected_buyer_and_seller_reporting_id;
    std::optional<std::string> auction_signals_json;
    std::optional<std::string> per_buyer_signals_json;
    std::string seller_signals_json;
    mojom::KAnonymityStatus kanon_status;
    GURL browser_signal_render_url;
    double browser_signal_bid;
    std::optional<blink::AdCurrency> browser_signal_bid_currency;
    double browser_signal_highest_scoring_other_bid;
    std::optional<blink::AdCurrency>
        browser_signal_highest_scoring_other_bid_currency;
    bool browser_signal_made_highest_scoring_other_bid;
    std::optional<double> browser_signal_ad_cost;
    std::optional<uint16_t> browser_signal_modeling_signals;
    std::optional<std::string> aggregate_win_signals;
    uint8_t browser_signal_join_count;
    uint8_t browser_signal_recency;
    url::Origin browser_signal_seller_origin;
    std::optional<url::Origin> browser_signal_top_level_seller_origin;
    std::optional<base::TimeDelta> browser_signal_reporting_timeout;
    std::optional<uint32_t> bidding_signals_data_version;
    uint64_t trace_id;

    // Time where tracing for wait_report_win_deps began.
    base::TimeTicks trace_wait_deps_start;
    // How long various inputs were waited for.
    base::TimeDelta wait_code;
    base::TimeDelta wait_direct_from_seller_signals;

    // Set while loading is in progress.
    std::unique_ptr<DirectFromSellerSignalsRequester::Request>
        direct_from_seller_request_per_buyer_signals;
    std::unique_ptr<DirectFromSellerSignalsRequester::Request>
        direct_from_seller_request_auction_signals;
    // Results of loading DirectFromSellerSignals.
    DirectFromSellerSignalsRequester::Result
        direct_from_seller_result_per_buyer_signals;
    DirectFromSellerSignalsRequester::Result
        direct_from_seller_result_auction_signals;
    // DirectFromSellerSignals errors are fatal, so no error information is
    // stored here.

    // Header-based DirectFromSellerSignals.
    std::optional<std::string>
        direct_from_seller_per_buyer_signals_header_ad_slot;
    std::optional<std::string>
        direct_from_seller_auction_signals_header_ad_slot;

    ReportWinCallback callback;
  };

  using ReportWinTaskList = std::list<ReportWinTask>;

  // Portion of BidderWorklet that deals with V8 execution, and therefore lives
  // on the v8 thread --- everything except the constructor must be run there.
  class V8State {
   public:
    V8State(
        scoped_refptr<AuctionV8Helper> v8_helper,
        scoped_refptr<AuctionV8Helper::DebugId> debug_id,
        mojo::PendingRemote<mojom::AuctionSharedStorageHost>
            shared_storage_host_remote,
        const GURL& script_source_url,
        const url::Origin& top_window_origin,
        mojom::AuctionWorkletPermissionsPolicyStatePtr permissions_policy_state,
        const std::optional<GURL>& wasm_helper_url,
        const std::optional<GURL>& trusted_bidding_signals_url,
        base::WeakPtr<BidderWorklet> parent);

    void SetWorkletScript(WorkletLoader::Result worklet_script);
    void SetWasmHelper(WorkletWasmLoader::Result wasm_helper);

    // These match the mojom GenerateBidCallback / ReportWinCallback functions,
    // except the errors vectors are passed by value. They're callbacks that
    // must be invoked on the main sequence, and passed to the V8State.
    using GenerateBidCallbackInternal = base::OnceCallback<void(
        std::vector<mojom::BidderWorkletBidPtr> bids,
        std::optional<uint32_t> bidding_signals_data_version,
        std::optional<GURL> debug_loss_report_url,
        std::optional<GURL> debug_win_report_url,
        std::optional<double> set_priority,
        base::flat_map<std::string, mojom::PrioritySignalsDoublePtr>
            update_priority_signals_overrides,
        PrivateAggregationRequests pa_requests,
        PrivateAggregationRequests non_kanon_pa_requests,
        RealTimeReportingContributions real_time_contributions,
        base::TimeDelta bidding_latency,
        mojom::RejectReason reject_reason,
        bool script_timed_out,
        std::vector<std::string> error_msgs)>;
    using ReportWinCallbackInternal = base::OnceCallback<void(
        std::optional<GURL> report_url,
        base::flat_map<std::string, GURL> ad_beacon_map,
        base::flat_map<std::string, std::string> ad_macro_map,
        PrivateAggregationRequests pa_requests,
        mojom::PrivateModelTrainingRequestDataPtr pmt_request_data,
        base::TimeDelta reporting_latency,
        bool script_timed_out,
        std::vector<std::string> errors)>;

    // Matches GenerateBidCallbackInternal, but with only one
    // BidderWorkletBidPtr.
    struct SingleGenerateBidResult {
      SingleGenerateBidResult();
      SingleGenerateBidResult(
          std::unique_ptr<ContextRecycler> context_recycler_for_rerun,
          std::vector<SetBidBindings::BidAndWorkletOnlyMetadata> bids,
          std::optional<uint32_t> bidding_signals_data_version,
          std::optional<GURL> debug_loss_report_url,
          std::optional<GURL> debug_win_report_url,
          std::optional<double> set_priority,
          base::flat_map<std::string, mojom::PrioritySignalsDoublePtr>
              update_priority_signals_overrides,
          PrivateAggregationRequests pa_requests,
          RealTimeReportingContributions real_time_contributions,
          mojom::RejectReason reject_reason,
          bool script_timed_out,
          std::vector<std::string> error_msgs);

      SingleGenerateBidResult(const SingleGenerateBidResult&) = delete;
      SingleGenerateBidResult(SingleGenerateBidResult&&);

      ~SingleGenerateBidResult();
      SingleGenerateBidResult& operator=(const SingleGenerateBidResult&) =
          delete;
      SingleGenerateBidResult& operator=(SingleGenerateBidResult&&);

      // If the context was not saved for user-configurable reuse mechanicsm,
      // it's returned here to be available for any re-run for k-anonymity.
      std::unique_ptr<ContextRecycler> context_recycler_for_rerun;

      std::vector<SetBidBindings::BidAndWorkletOnlyMetadata> bids;
      std::optional<uint32_t> bidding_signals_data_version;
      std::optional<GURL> debug_loss_report_url;
      std::optional<GURL> debug_win_report_url;
      std::optional<double> set_priority;
      base::flat_map<std::string, mojom::PrioritySignalsDoublePtr>
          update_priority_signals_overrides;
      PrivateAggregationRequests pa_requests;
      PrivateAggregationRequests non_kanon_pa_requests;
      RealTimeReportingContributions real_time_contributions;
      mojom::RejectReason reject_reason;
      bool script_timed_out;
      std::vector<std::string> error_msgs;
    };

    bool SetBrowserSignals(
        ContextRecycler& context_recycler,
        v8::Local<v8::Context>& context,
        bool is_for_additional_bid,
        const std::optional<std::string>& interest_group_name_reporting_id,
        const std::optional<std::string>& buyer_reporting_id,
        const std::optional<std::string>& buyer_and_seller_reporting_id,
        const std::optional<std::string>&
            selected_buyer_and_seller_reporting_id,
        const GURL& browser_signal_render_url,
        DeprecatedUrlLazyFiller* deprecated_render_url,
        double browser_signal_bid,
        const std::optional<blink::AdCurrency>& browser_signal_bid_currency,
        double browser_signal_highest_scoring_other_bid,
        const std::optional<blink::AdCurrency>&
            browser_signal_highest_scoring_other_bid_currency,
        bool browser_signal_made_highest_scoring_other_bid,
        const std::optional<double>& browser_signal_ad_cost,
        const std::optional<uint16_t>& browser_signal_modeling_signals,
        uint8_t browser_signal_join_count,
        uint8_t browser_signal_recency,
        const url::Origin& browser_signal_seller_origin,
        const std::optional<url::Origin>&
            browser_signal_top_level_seller_origin,
        const std::optional<uint32_t>& bidding_signals_data_version,
        const std::string& kanon_status,
        const base::TimeDelta reporting_timeout,
        v8::Local<v8::Object> browser_signals,
        gin::Dictionary& browser_signals_dict);

    // Sets up arguments for the `reportAggregateWin()` JavaScript function,
    // returning true on success, false on failure.
    bool SetReportAggregateWinArgs(
        v8::Local<v8::Context>& context,
        ContextRecycler& context_recycler,
        const std::optional<std::string>& auction_signals_json,
        const std::optional<std::string>& per_buyer_signals_json,
        const std::string& seller_signals_json,
        bool is_for_additional_bid,
        const std::optional<std::string>& interest_group_name_reporting_id,
        const std::optional<std::string>& buyer_reporting_id,
        const std::optional<std::string>& buyer_and_seller_reporting_id,
        const std::optional<std::string>&
            selected_buyer_and_seller_reporting_id,
        const GURL& browser_signal_render_url,
        double browser_signal_bid,
        const std::optional<blink::AdCurrency>& browser_signal_bid_currency,
        double browser_signal_highest_scoring_other_bid,
        const std::optional<blink::AdCurrency>&
            browser_signal_highest_scoring_other_bid_currency,
        bool browser_signal_made_highest_scoring_other_bid,
        const std::optional<double>& browser_signal_ad_cost,
        const std::optional<uint16_t>& browser_signal_modeling_signals,
        uint8_t browser_signal_join_count,
        uint8_t browser_signal_recency,
        const url::Origin& browser_signal_seller_origin,
        const std::optional<url::Origin>&
            browser_signal_top_level_seller_origin,
        const std::optional<base::TimeDelta> browser_signal_reporting_timeout,
        const std::optional<uint32_t>& bidding_signals_data_version,
        const std::string& kanon_status,
        const std::optional<std::string>& aggregate_win_signals,
        const base::TimeDelta reporting_timeout,
        const v8::Local<v8::Value>& per_buyer_signals,
        const v8::Local<v8::Value>& auction_signals,
        const ReportBindings::ModelingSignalsConfig& modeling_signals_config,
        v8::LocalVector<v8::Value>& report_aggregate_win_args);

    void ReportWin(
        bool is_for_additional_bid,
        const std::optional<std::string>& interest_group_name_reporting_id,
        const std::optional<std::string>& buyer_reporting_id,
        const std::optional<std::string>& buyer_and_seller_reporting_id,
        const std::optional<std::string>&
            selected_buyer_and_seller_reporting_id,
        const std::optional<std::string>& auction_signals_json,
        const std::optional<std::string>& per_buyer_signals_json,
        DirectFromSellerSignalsRequester::Result
            direct_from_seller_result_per_buyer_signals,
        const std::optional<std::string>&
            direct_from_seller_per_buyer_signals_header_ad_slot,
        DirectFromSellerSignalsRequester::Result
            direct_from_seller_result_auction_signals,
        const std::optional<std::string>&
            direct_from_seller_auction_signals_header_ad_slot,
        const std::string& seller_signals_json,
        mojom::KAnonymityStatus kanon_status,
        const GURL& browser_signal_render_url,
        double browser_signal_bid,
        const std::optional<blink::AdCurrency>& browser_signal_bid_currency,
        double browser_signal_highest_scoring_other_bid,
        const std::optional<blink::AdCurrency>&
            browser_signal_highest_scoring_other_bid_currency,
        bool browser_signal_made_highest_scoring_other_bid,
        const std::optional<double>& browser_signal_ad_cost,
        const std::optional<uint16_t>& browser_signal_modeling_signals,
        uint8_t browser_signal_join_count,
        uint8_t browser_signal_recency,
        const url::Origin& browser_signal_seller_origin,
        const std::optional<url::Origin>&
            browser_signal_top_level_seller_origin,
        const std::optional<base::TimeDelta> browser_signal_reporting_timeout,
        const std::optional<uint32_t>& bidding_signals_data_version,
        const std::optional<std::string>& aggregate_win_signals,
        uint64_t trace_id,
        ReportWinCallbackInternal callback);

    void GenerateBid(
        mojom::BidderWorkletNonSharedParamsPtr bidder_worklet_non_shared_params,
        mojom::KAnonymityBidMode kanon_mode,
        const std::optional<std::string>& auction_signals_json,
        const std::optional<std::string>& per_buyer_signals_json,
        DirectFromSellerSignalsRequester::Result
            direct_from_seller_result_per_buyer_signals,
        const std::optional<std::string>&
            direct_from_seller_per_buyer_signals_header_ad_slot,
        DirectFromSellerSignalsRequester::Result
            direct_from_seller_result_auction_signals,
        const std::optional<std::string>&
            direct_from_seller_auction_signals_header_ad_slot,
        const std::optional<base::TimeDelta> per_buyer_timeout,
        const std::optional<blink::AdCurrency>& expected_buyer_currency,
        const url::Origin& browser_signal_seller_origin,
        const std::optional<url::Origin>&
            browser_signal_top_level_seller_origin,
        const base::TimeDelta browser_signal_recency,
        bool browser_signal_for_debugging_only_sampling,
        blink::mojom::BiddingBrowserSignalsPtr bidding_browser_signals,
        base::Time auction_start_time,
        const std::optional<blink::AdSize>& requested_ad_size,
        uint16_t multi_bid_limit,
        uint64_t group_by_origin_id,
        scoped_refptr<TrustedSignals::Result> trusted_bidding_signals_result,
        bool trusted_bidding_signals_fetch_failed,
        uint64_t trace_id,
        base::ScopedClosureRunner cleanup_generate_bid_task,
        GenerateBidCallbackInternal callback);

    void ConnectDevToolsAgent(
        mojo::PendingAssociatedReceiver<blink::mojom::DevToolsAgent> agent);

    // Create a context recycler, run the top level script, and add bindings.
    // This context recycler will be saved for later use by a GenerateBid call.
    void PrepareContextRecycler(uint64_t trace_id);

   private:
    friend class base::DeleteHelper<V8State>;
    ~V8State();

    // Returns nullopt on error.
    // `context_recycler_for_rerun` is permitted to be null, and should only be
    // set if `restrict_to_kanon_ads` is true.
    std::optional<SingleGenerateBidResult> RunGenerateBidOnce(
        const mojom::BidderWorkletNonSharedParams&
            bidder_worklet_non_shared_params,
        const std::string* auction_signals_json,
        const std::string* per_buyer_signals_json,
        const DirectFromSellerSignalsRequester::Result&
            direct_from_seller_result_per_buyer_signals,
        const std::optional<std::string>&
            direct_from_seller_per_buyer_signals_header_ad_slot,
        const DirectFromSellerSignalsRequester::Result&
            direct_from_seller_result_auction_signals,
        const std::optional<std::string>&
            direct_from_seller_auction_signals_header_ad_slot,
        const std::optional<base::TimeDelta> per_buyer_timeout,
        const std::optional<blink::AdCurrency>& expected_buyer_currency,
        const url::Origin& browser_signal_seller_origin,
        const url::Origin* browser_signal_top_level_seller_origin,
        const base::TimeDelta browser_signal_recency,
        bool browser_signal_for_debugging_only_sampling,
        const blink::mojom::BiddingBrowserSignalsPtr& bidding_browser_signals,
        base::Time auction_start_time,
        const std::optional<blink::AdSize>& requested_ad_size,
        uint16_t multi_bid_limit,
        uint64_t group_by_origin_id,
        const scoped_refptr<TrustedSignals::Result>&
            trusted_bidding_signals_result,
        uint64_t trace_id,
        std::unique_ptr<ContextRecycler> context_recycler_for_rerun,
        bool restrict_to_kanon_ads);

    std::unique_ptr<ContextRecycler>
    CreateContextRecyclerAndRunTopLevelForGenerateBid(
        uint64_t trace_id,
        AuctionV8Helper::TimeLimit& total_timeout,
        bool should_deep_freeze,
        bool& script_timed_out,
        std::vector<std::string>& errors_out);

    void FinishInit(mojo::PendingRemote<mojom::AuctionSharedStorageHost>
                        shared_storage_host_remote);

    void PostReportWinCallbackToUserThread(
        ReportWinCallbackInternal callback,
        const std::optional<GURL>& report_url,
        base::flat_map<std::string, GURL> ad_beacon_map,
        base::flat_map<std::string, std::string> ad_macro_map,
        PrivateAggregationRequests pa_requests,
        mojom::PrivateModelTrainingRequestDataPtr pmt_request_data,
        base::TimeDelta reporting_latency,
        bool script_timed_out,
        std::vector<std::string> errors);

    void PostErrorBidCallbackToUserThread(
        GenerateBidCallbackInternal callback,
        base::TimeDelta bidding_latency,
        PrivateAggregationRequests non_kanon_pa_requests,
        RealTimeReportingContributions real_time_contributions,
        std::vector<std::string> error_msgs = std::vector<std::string>());

    static void PostResumeToUserThread(
        base::WeakPtr<BidderWorklet> parent,
        scoped_refptr<base::SequencedTaskRunner> user_thread);

    const scoped_refptr<AuctionV8Helper> v8_helper_;
    const scoped_refptr<AuctionV8Helper::DebugId> debug_id_;
    const base::WeakPtr<BidderWorklet> parent_;
    const scoped_refptr<base::SequencedTaskRunner> user_thread_;

    const url::Origin owner_;

    // Compiled script, not bound to any context. Can be repeatedly bound to
    // different context and executed, without persisting any state.
    v8::Global<v8::UnboundScript> worklet_script_;

    // Loaded WASM module. Can be used to create instances for each context.
    WorkletWasmLoader::Result wasm_helper_;

    const GURL script_source_url_;
    const url::Origin top_window_origin_;
    mojom::AuctionWorkletPermissionsPolicyStatePtr permissions_policy_state_;
    const std::optional<GURL> wasm_helper_url_;
    const std::optional<GURL> trusted_bidding_signals_url_;
    const std::optional<url::Origin> trusted_bidding_signals_origin_;

    // This must be above the ContextRecyclers, since they own
    // SharedStorageBindings, which have raw pointers to it.
    mojo::Remote<mojom::AuctionSharedStorageHost> shared_storage_host_remote_;


    // ContextRecyclers we prepare in advance, along with a bool indicating if
    // there was a timeout and any errors in preparing the context. These can be
    // used by any execution mode, but for "frozen-context" mode, the context
    // will need to be frozen before it's used.
    std::vector<std::tuple<std::unique_ptr<ContextRecycler>,
                           bool,
                           std::vector<std::string>>>
        unused_context_recyclers_;

    // The number of contexts we created that were not premade. Used for UMA:
    // Ads.InterestGroup.Auction.NonPremadeContextsCreated.
    size_t non_premade_contexts_created = 0;

    ExecutionModeHelper execution_mode_helper_;

    SEQUENCE_CHECKER(v8_sequence_checker_);
  };

  void ResumeIfPaused();
  void Start();

  void OnScriptDownloaded(std::vector<WorkletLoader::Result> worklet_scripts,
                          std::optional<std::string> error_msg);
  void OnWasmDownloaded(std::vector<WorkletWasmLoader::Result> worklet_scripts,
                        std::optional<std::string> error_msg);
  void MaybeRecordCodeWait();
  void RunReadyTasks();

  // If our scripts are downloaded but we aren't ready to generate the first
  // bid (and haven't generated any bids yet), prepare some contexts for later
  // use, including running the top level script and adding bindings.
  void MaybePrepareContexts();

  void OnTrustedBiddingSignalsDownloaded(
      GenerateBidTaskList::iterator task,
      scoped_refptr<TrustedSignals::Result> result,
      std::optional<std::string> error_msg);

  // Invoked when the GenerateBidClient associated with `task` is destroyed.
  // Cancels bid generation.
  void OnGenerateBidClientDestroyed(GenerateBidTaskList::iterator task);

  // Callback passed to mojom::GenerateBidClient::OnSignalsReceived. Sets
  // `task->signals_received_callback_invoked` to true, and invokes
  // GenerateBidIfReady().
  void SignalsReceivedCallback(GenerateBidTaskList::iterator task);

  void HandleDirectFromSellerForGenerateBid(
      const std::optional<GURL>& direct_from_seller_per_buyer_signals,
      const std::optional<GURL>& direct_from_seller_auction_signals,
      GenerateBidTaskList::iterator task);

  void OnDirectFromSellerPerBuyerSignalsDownloadedGenerateBid(
      GenerateBidTaskList::iterator task,
      DirectFromSellerSignalsRequester::Result result);

  void OnDirectFromSellerAuctionSignalsDownloadedGenerateBid(
      GenerateBidTaskList::iterator task,
      DirectFromSellerSignalsRequester::Result result);

  // Returns true iff all generateBid()'s inputs are ready. The JS and WASM
  // may or may not be ready yet.
  bool GenerateBidTaskHasInputs(const GenerateBidTask& task) const;

  // Returns true iff all generateBid()'s prerequisite loading tasks have
  // completed.
  bool IsReadyToGenerateBid(const GenerateBidTask& task) const;

  // We only want to eagerly compile JS if we've received promises (to avoid
  // eager compilation if an auction will be aborted) and we're not waiting on
  // the Javascript script only (so that we can start generating the bid as soon
  // as we get the script).
  void SetEagerJsCompilation(bool eagerly_compile_js);

  // Checks if IsReadyToGenerateBid(). If so, calls generateBid(), and invokes
  // the task callback with the resulting bid, if any.
  void GenerateBidIfReady(GenerateBidTaskList::iterator task);

  void OnDirectFromSellerPerBuyerSignalsDownloadedReportWin(
      ReportWinTaskList::iterator task,
      DirectFromSellerSignalsRequester::Result result);

  void OnDirectFromSellerAuctionSignalsDownloadedReportWin(
      ReportWinTaskList::iterator task,
      DirectFromSellerSignalsRequester::Result result);

  // Returns true iff all reportWin()'s prerequisite loading tasks have
  // completed.
  bool IsReadyToReportWin(const ReportWinTask& task) const;

  // Checks IsReadyToReportWin(). If so, calls reportWin(), and invokes the task
  // callback with the reporting information.
  void RunReportWinIfReady(ReportWinTaskList::iterator task);

  // Invokes the `callback` of `task` with the provided values, and removes
  // `task` from `generate_bid_tasks_`.
  void DeliverBidCallbackOnUserThread(
      GenerateBidTaskList::iterator task,
      size_t thread_index_used_for_task,
      std::vector<mojom::BidderWorkletBidPtr> bids,
      std::optional<uint32_t> bidding_signals_data_version,
      std::optional<GURL> debug_loss_report_url,
      std::optional<GURL> debug_win_report_url,
      std::optional<double> set_priority,
      base::flat_map<std::string, mojom::PrioritySignalsDoublePtr>
          update_priority_signals_overrides,
      PrivateAggregationRequests pa_requests,
      PrivateAggregationRequests non_kanon_pa_requests,
      RealTimeReportingContributions real_time_contributions,
      base::TimeDelta bidding_latency,
      mojom::RejectReason reject_reason,
      bool script_timed_out,
      std::vector<std::string> error_msgs);

  // Removes `task` from `generate_bid_tasks_` only. Used in case where the
  // V8 work for task was cancelled only (via the `cleanup_generate_bid_task`
  // parameter getting destroyed).
  void CleanUpBidTaskOnUserThread(GenerateBidTaskList::iterator task);

  // Invokes the `callback` of `task` with the provided values, and removes
  // `task` from `report_win_tasks_`.
  void DeliverReportWinOnUserThread(
      ReportWinTaskList::iterator task,
      size_t thread_index_used_for_task,
      std::optional<GURL> report_url,
      base::flat_map<std::string, GURL> ad_beacon_map,
      base::flat_map<std::string, std::string> ad_macro_map,
      PrivateAggregationRequests pa_requests,
      mojom::PrivateModelTrainingRequestDataPtr pmt_request_data,
      base::TimeDelta reporting_latency,
      bool script_timed_out,
      std::vector<std::string> errors);

  // Returns true if unpaused and the script and WASM helper (if needed) have
  // loaded.
  bool IsCodeReady() const;

  std::vector<scoped_refptr<base::SequencedTaskRunner>> v8_runners_;
  std::vector<scoped_refptr<AuctionV8Helper>> v8_helpers_;
  std::vector<scoped_refptr<AuctionV8Helper::DebugId>> debug_ids_;

  // Generates the thread index to use for parsing trusted signals, for handling
  // `generateBid` when the execution mode is not group-by-origin, and for
  // `reportWin`.
  BidderWorkletThreadSelector thread_selector_;

  mojo::Remote<network::mojom::URLLoaderFactory> url_loader_factory_;
  // Owned by the AuctionWorkletService that owns `this`.
  const raw_ptr<TrustedSignalsKVv2Manager> trusted_signals_kvv2_manager_;

  bool paused_;

  size_t resumed_count_ = 0;

  // Values shared by all interest groups that the BidderWorklet can be used
  // for.
  const GURL script_source_url_;
  mojom::InProgressAuctionDownloadPtr script_source_load_;
  std::optional<GURL> wasm_helper_url_;
  mojom::InProgressAuctionDownloadPtr wasm_helper_load_;

  // Populated only if `this` was created with a non-null
  // `trusted_scoring_signals_url`.
  std::unique_ptr<TrustedSignalsRequestManager>
      trusted_signals_request_manager_;

  // Used for fetching DirectFromSellerSignals from subresource bundles (and
  // caching responses).
  DirectFromSellerSignalsRequester
      direct_from_seller_requester_per_buyer_signals_;
  DirectFromSellerSignalsRequester
      direct_from_seller_requester_auction_signals_;

  // Top window origin for the auctions sharing this BidderWorklet.
  const url::Origin top_window_origin_;

  // These are deleted once each resource is loaded.
  std::unique_ptr<WorkletLoader> worklet_loader_;
  std::unique_ptr<WorkletWasmLoader> wasm_loader_;
  base::TimeTicks code_download_start_;
  std::optional<base::TimeDelta> js_fetch_latency_;
  std::optional<base::TimeDelta> wasm_fetch_latency_;

  // Lives on `v8_runners_`. Since it's deleted there via DeleteSoon, tasks can
  // be safely posted from main thread to it with an Unretained pointer.
  std::vector<std::unique_ptr<V8State, base::OnTaskRunnerDeleter>> v8_state_;

  // Pending calls to the corresponding Javascript methods. Only accessed on
  // main thread, but iterators to their elements are bound to callbacks passed
  // to the v8 thread, so these need to be std::lists rather than std::vectors.
  GenerateBidTaskList generate_bid_tasks_;
  ReportWinTaskList report_win_tasks_;

  // Binds finalization of GenerateBid calls to `this`.
  mojo::AssociatedReceiverSet<mojom::GenerateBidFinalizer,
                              GenerateBidTaskList::iterator>
      finalize_receiver_set_;

  // Whether any bid was finalized. We use this as a heuristic to indicate that
  // at least one auction is going to proceed without being aborted. In
  // particular, we use it to determine whether we should prepare contexts.
  bool finalized_any_bid_ = false;

  ClosePipeCallback close_pipe_callback_;

  // Errors that occurred while loading the code, if any.
  std::vector<std::string> load_code_error_msgs_;

  base::CancelableTaskTracker cancelable_task_tracker_;

  mojo::Remote<auction_worklet::mojom::AuctionNetworkEventsHandler>
      auction_network_events_handler_;

  // We use a separate task tracker for context preparation tasks,
  // so that once we're ready to generate bids, we can easily
  // cancel all preparation tasks.
  base::CancelableTaskTracker context_preparation_task_tracker_;

  SEQUENCE_CHECKER(user_sequence_checker_);

  // Used when posting callbacks back from V8State.
  base::WeakPtrFactory<BidderWorklet> weak_ptr_factory_{this};
};

}  // namespace auction_worklet

#endif  // CONTENT_SERVICES_AUCTION_WORKLET_BIDDER_WORKLET_H_
