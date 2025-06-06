// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_IP_PROTECTION_COMMON_IP_PROTECTION_TOKEN_MOJO_FETCHER_H_
#define COMPONENTS_IP_PROTECTION_COMMON_IP_PROTECTION_TOKEN_MOJO_FETCHER_H_

#include <cstdint>

#include "base/memory/scoped_refptr.h"
#include "components/ip_protection/common/ip_protection_token_fetcher.h"

namespace ip_protection {

class IpProtectionCoreHostRemote;
enum class ProxyLayer;

// Manages fetching tokens via Mojo. This is a simple wrapper around
// `IpProtectionCoreHostRemote`.
class IpProtectionTokenMojoFetcher : public IpProtectionTokenFetcher {
 public:
  explicit IpProtectionTokenMojoFetcher(
      scoped_refptr<IpProtectionCoreHostRemote> core_host_remote);
  ~IpProtectionTokenMojoFetcher() override;

  // IpProtectionTokenFetcher implementation:
  void TryGetAuthTokens(uint32_t batch_size,
                        ProxyLayer proxy_layer,
                        TryGetAuthTokensCallback callback) override;

 private:
  scoped_refptr<IpProtectionCoreHostRemote> core_host_remote_;
};

}  // namespace ip_protection

#endif  // COMPONENTS_IP_PROTECTION_COMMON_IP_PROTECTION_TOKEN_MOJO_FETCHER_H_
