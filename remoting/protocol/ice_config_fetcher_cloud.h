// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_PROTOCOL_ICE_CONFIG_FETCHER_CLOUD_H_
#define REMOTING_PROTOCOL_ICE_CONFIG_FETCHER_CLOUD_H_

#include <memory>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "remoting/base/cloud_service_client.h"
#include "remoting/protocol/ice_config_fetcher.h"

namespace google::internal::remoting::cloud::v1alpha {
class GenerateIceConfigResponse;
}  // namespace google::internal::remoting::cloud::v1alpha

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace remoting {

class HttpStatus;
class InstanceIdentityTokenGetter;
class OAuthTokenGetter;

namespace protocol {

// IceConfigRequest that fetches an IceConfig from the Remoting Cloud API.
class IceConfigFetcherCloud final : public protocol::IceConfigFetcher {
 public:
  IceConfigFetcherCloud(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      OAuthTokenGetter* oauth_token_getter,
      InstanceIdentityTokenGetter* instance_identity_token_getter);

  IceConfigFetcherCloud(const IceConfigFetcherCloud&) = delete;
  IceConfigFetcherCloud& operator=(const IceConfigFetcherCloud&) = delete;

  ~IceConfigFetcherCloud() override;

  // IceConfigFetcher implementation.
  void GetIceConfig(OnIceConfigCallback callback) override;

 private:
  friend class IceConfigFetcherCloudTest;

  // Overloads used to create callbacks for |instance_identity_token_getter_|.
  void GetIceConfigWithIdToken(OnIceConfigCallback callback,
                               std::string_view instance_identity_token);

  void OnResponse(OnIceConfigCallback callback,
                  const HttpStatus& status,
                  std::unique_ptr<::google::internal::remoting::cloud::v1alpha::
                                      GenerateIceConfigResponse> response);

  OnIceConfigCallback on_ice_config_callback_;
  std::unique_ptr<CloudServiceClient> service_client_;
  const raw_ptr<InstanceIdentityTokenGetter> instance_identity_token_getter_;

  base::WeakPtrFactory<IceConfigFetcherCloud> weak_factory_{this};
};

}  // namespace protocol
}  // namespace remoting

#endif  // REMOTING_PROTOCOL_ICE_CONFIG_FETCHER_CLOUD_H_
