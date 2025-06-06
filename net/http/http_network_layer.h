// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_NETWORK_LAYER_H_
#define NET_HTTP_HTTP_NETWORK_LAYER_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/threading/thread_checker.h"
#include "net/base/net_export.h"
#include "net/http/http_transaction_factory.h"

namespace net {

class HttpNetworkSession;

class NET_EXPORT HttpNetworkLayer : public HttpTransactionFactory {
 public:
  // Construct a HttpNetworkLayer with an existing HttpNetworkSession which
  // contains a valid ProxyResolutionService. The HttpNetworkLayer must be
  // destroyed before |session|.
  explicit HttpNetworkLayer(HttpNetworkSession* session);

  HttpNetworkLayer(const HttpNetworkLayer&) = delete;
  HttpNetworkLayer& operator=(const HttpNetworkLayer&) = delete;

  ~HttpNetworkLayer() override;

  // HttpTransactionFactory methods:
  std::unique_ptr<HttpTransaction> CreateTransaction(
      RequestPriority priority) override;
  HttpCache* GetCache() override;
  HttpNetworkSession* GetSession() override;

 private:
  const raw_ptr<HttpNetworkSession> session_;

  THREAD_CHECKER(thread_checker_);
};

}  // namespace net

#endif  // NET_HTTP_HTTP_NETWORK_LAYER_H_
