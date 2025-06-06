// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AFFILIATIONS_CORE_BROWSER_AFFILIATION_FETCH_THROTTLER_H_
#define COMPONENTS_AFFILIATIONS_CORE_BROWSER_AFFILIATION_FETCH_THROTTLER_H_

#include <stdint.h>

#include <memory>

#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "net/base/backoff_entry.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/network_connection_tracker.h"

namespace base {
class TickClock;
class SequencedTaskRunner;
}  // namespace base

namespace affiliations {

class AffiliationFetchThrottlerDelegate;

// Implements the throttling logic that the AffiliationBackend will use when it
// needs to issue requests over the network to fetch affiliation information.
//
// This class manages only the scheduling of the requests. It is up to the
// consumer (the AffiliationBackend) to actually assemble and send the requests,
// to report back with their status, and to retry them if desired.
// The process goes like this:
//   1.) The consumer calls SignalNetworkRequestNeeded().
//   2.) Once appropriate, OnCanSendNetworkRequest() is called on the delegate.
//   3.) The consumer sends the request, and waits until it completes.
//   4.) The consumer calls InformOfNetworkRequestComplete().
// Note that only a single request at a time is supported.
//
// If the request fails in Step 3, the consumer should not automatically retry
// it. Instead it should always proceed to Step 4, and then -- if retrying the
// request is desired -- proceed immediately to Step 1. That is, it should act
// as if another request was needed right away.
//
// Essentially, this class implements exponential backoff in case of network and
// server errors with 2 additional constraints:
// 1. No requests will be issued in the first place without network
// connectivity. This prevents the exponential backoff delay from growing huge
// during long offline periods, so that requests will not be held back for too
// long after connectivity is restored.
// 2. When a request fails in a way that indicates an immediate retry won't
// succeed (e.g., the server returns a 429 status), throttler will significantly
// increase the delay before allowing the next request. This is needed to
// prevent overwhelming the server, especially during potential outages.
// However, since the requested facet is already displayed in the Password
// Manager UI, Affiliation Backend must still eventually retrieve the server's
// response to ensure the UI information is accurate.
class AffiliationFetchThrottler
    : public network::NetworkConnectionTracker::NetworkConnectionObserver {
 public:
  // Creates an instance that will use |tick_clock| as its tick source, and will
  // post to |task_runner| to call the |delegate|'s OnSendNetworkRequest(). The
  // |delegate| and |tick_clock| should outlive the throttler.
  AffiliationFetchThrottler(
      AffiliationFetchThrottlerDelegate* delegate,
      const scoped_refptr<base::SequencedTaskRunner>& task_runner,
      network::NetworkConnectionTracker* network_connection_tracker,
      const base::TickClock* tick_clock);

  AffiliationFetchThrottler(const AffiliationFetchThrottler&) = delete;
  AffiliationFetchThrottler& operator=(const AffiliationFetchThrottler&) =
      delete;

  ~AffiliationFetchThrottler() override;

  // Signals to the throttling logic that a network request is needed, and that
  // OnCanSendNetworkRequest() should be called as soon as the request can be
  // sent. OnCanSendNetworkRequest() will always be called asynchronously.
  //
  // Calls to this method will be ignored when a request is already known to be
  // needed or while a request is in flight. To signal that another request will
  // be needed right away after the current one, call this method after calling
  // InformOfNetworkRequestComplete().
  virtual void SignalNetworkRequestNeeded();

  // Informs the back-off logic that the in-flight network request has been
  // completed. This will decide if the response code is a retryable error or
  // not and apply a long delay on non-retryable errors.
  virtual void InformOfNetworkRequestComplete(
      bool success,
      std::optional<net::HttpStatusCode> http_status_code);

  // Returns whether there is internet connection or not.
  virtual bool HasInternetConnection() const;

 protected:
  raw_ptr<AffiliationFetchThrottlerDelegate> delegate_;

 private:
  FRIEND_TEST_ALL_PREFIXES(AffiliationFetchThrottlerTest, FailedRequests);
  FRIEND_TEST_ALL_PREFIXES(AffiliationFetchThrottlerTest,
                           NonRetryableFailedRequestsSetLongDelay);
  FRIEND_TEST_ALL_PREFIXES(AffiliationFetchThrottlerTest,
                           GracePeriodAfterConnectivityIsRestored);
  FRIEND_TEST_ALL_PREFIXES(AffiliationFetchThrottlerTest,
                           GracePeriodAfterConnectivityIsRestored2);
  FRIEND_TEST_ALL_PREFIXES(AffiliationFetchThrottlerTest,
                           GracePeriodAfterConnectivityIsRestored3);
  FRIEND_TEST_ALL_PREFIXES(AffiliationFetchThrottlerTest,
                           ConnectivityLostDuringBackoff);
  FRIEND_TEST_ALL_PREFIXES(AffiliationFetchThrottlerTest,
                           ConnectivityLostAndRestoredDuringBackoff);
  FRIEND_TEST_ALL_PREFIXES(AffiliationFetchThrottlerTest, FlakyConnectivity);
  FRIEND_TEST_ALL_PREFIXES(AffiliationFetchThrottlerTest,
                           ConnectivityLostDuringRequest);
  FRIEND_TEST_ALL_PREFIXES(AffiliationFetchThrottlerTest,
                           ConnectivityLostAndRestoredDuringRequest);
  FRIEND_TEST_ALL_PREFIXES(AffiliationFetchThrottlerTest,
                           ConnectivityLostAndRestoredDuringRequest2);

  enum State { IDLE, FETCH_NEEDED, FETCH_IN_FLIGHT };

  // Exponential backoff parameters in case of network and server errors
  static const net::BackoffEntry::Policy kBackoffPolicy;

  // Minimum delay before sending the first request once network connectivity is
  // restored. The fuzzing factor in |kBackoffParameters.jitter_factor| applies.
  static const int64_t kGracePeriodAfterReconnectMs;

  // Minimum delay set after receiving a non-retryable error from Affiliation
  // Server.
  static const int64_t kBackoffAfterNonRetryableErrorHours;

  // Ensures that OnBackoffDelayExpiredCallback() is scheduled to be called back
  // once the |exponential_backoff_| delay expires.
  void EnsureCallbackIsScheduled();

  // Called back when the |exponential_backoff_| delay expires.
  void OnBackoffDelayExpiredCallback();

  // network::NetworkConnectionTracker::NetworkConnectionObserver:
  void OnConnectionChanged(network::mojom::ConnectionType type) override;

  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  raw_ptr<network::NetworkConnectionTracker> network_connection_tracker_;
  raw_ptr<const base::TickClock> tick_clock_;
  State state_;
  bool has_network_connectivity_;
  bool is_fetch_scheduled_;
  std::unique_ptr<net::BackoffEntry> exponential_backoff_;

  base::WeakPtrFactory<AffiliationFetchThrottler> weak_ptr_factory_{this};
};

}  // namespace affiliations

#endif  // COMPONENTS_AFFILIATIONS_CORE_BROWSER_AFFILIATION_FETCH_THROTTLER_H_
