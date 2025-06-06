// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/public/cpp/isolation_info_mojom_traits.h"

#include "base/notreached.h"
#include "base/unguessable_token.h"
#include "net/base/isolation_info.h"
#include "net/base/network_isolation_partition.h"
#include "services/network/public/cpp/cookie_manager_shared_mojom_traits.h"
#include "services/network/public/cpp/crash_keys.h"
#include "services/network/public/mojom/isolation_info.mojom-shared.h"

namespace mojo {

bool EnumTraits<network::mojom::IsolationInfoRequestType,
                net::IsolationInfo::RequestType>::
    FromMojom(network::mojom::IsolationInfoRequestType request_type,
              net::IsolationInfo::RequestType* out) {
  switch (request_type) {
    case network::mojom::IsolationInfoRequestType::kMainFrame:
      *out = net::IsolationInfo::RequestType::kMainFrame;
      return true;
    case network::mojom::IsolationInfoRequestType::kSubFrame:
      *out = net::IsolationInfo::RequestType::kSubFrame;
      return true;
    case network::mojom::IsolationInfoRequestType::kOther:
      *out = net::IsolationInfo::RequestType::kOther;
      return true;
  }
  return false;
}

bool EnumTraits<network::mojom::IsolationInfoFrameAncestorRelation,
                net::IsolationInfo::FrameAncestorRelation>::
    FromMojom(network::mojom::IsolationInfoFrameAncestorRelation
                  frame_ancestor_relation,
              net::IsolationInfo::FrameAncestorRelation* out) {
  switch (frame_ancestor_relation) {
    case network::mojom::IsolationInfoFrameAncestorRelation::kSameOrigin:
      *out = net::IsolationInfo::FrameAncestorRelation::kSameOrigin;
      return true;
    case network::mojom::IsolationInfoFrameAncestorRelation::kSameSite:
      *out = net::IsolationInfo::FrameAncestorRelation::kSameSite;
      return true;
    case network::mojom::IsolationInfoFrameAncestorRelation::kCrossSite:
      *out = net::IsolationInfo::FrameAncestorRelation::kCrossSite;
      return true;
  }
  return false;
}

network::mojom::IsolationInfoRequestType EnumTraits<
    network::mojom::IsolationInfoRequestType,
    net::IsolationInfo::RequestType>::ToMojom(net::IsolationInfo::RequestType
                                                  request_type) {
  switch (request_type) {
    case net::IsolationInfo::RequestType::kMainFrame:
      return network::mojom::IsolationInfoRequestType::kMainFrame;
    case net::IsolationInfo::RequestType::kSubFrame:
      return network::mojom::IsolationInfoRequestType::kSubFrame;
    case net::IsolationInfo::RequestType::kOther:
      return network::mojom::IsolationInfoRequestType::kOther;
  }

  NOTREACHED();
}

network::mojom::IsolationInfoFrameAncestorRelation
EnumTraits<network::mojom::IsolationInfoFrameAncestorRelation,
           net::IsolationInfo::FrameAncestorRelation>::
    ToMojom(net::IsolationInfo::FrameAncestorRelation frame_ancestor_relation) {
  switch (frame_ancestor_relation) {
    case net::IsolationInfo::FrameAncestorRelation::kSameOrigin:
      return network::mojom::IsolationInfoFrameAncestorRelation::kSameOrigin;
    case net::IsolationInfo::FrameAncestorRelation::kSameSite:
      return network::mojom::IsolationInfoFrameAncestorRelation::kSameSite;
    case net::IsolationInfo::FrameAncestorRelation::kCrossSite:
      return network::mojom::IsolationInfoFrameAncestorRelation::kCrossSite;
  }

  NOTREACHED();
}

bool StructTraits<network::mojom::IsolationInfoDataView, net::IsolationInfo>::
    Read(network::mojom::IsolationInfoDataView data, net::IsolationInfo* out) {
  std::optional<url::Origin> top_frame_origin;
  std::optional<url::Origin> frame_origin;
  std::optional<base::UnguessableToken> nonce;
  net::SiteForCookies site_for_cookies;
  net::IsolationInfo::RequestType request_type;
  net::NetworkIsolationPartition network_isolation_partition;
  std::optional<net::IsolationInfo::FrameAncestorRelation>
      frame_ancestor_relation;

  if (!data.ReadTopFrameOrigin(&top_frame_origin)) {
    network::debug::SetDeserializationCrashKeyString("isolation_top_origin");
    return false;
  }
  if (!data.ReadFrameOrigin(&frame_origin)) {
    network::debug::SetDeserializationCrashKeyString("isolation_frame_origin");
    return false;
  }
  if (!data.ReadNonce(&nonce) || !data.ReadSiteForCookies(&site_for_cookies) ||
      !data.ReadRequestType(&request_type) ||
      !data.ReadNetworkIsolationPartition(&network_isolation_partition) ||
      !data.ReadFrameAncestorRelation(&frame_ancestor_relation)) {
    return false;
  }

  std::optional<net::IsolationInfo> isolation_info =
      net::IsolationInfo::CreateIfConsistent(
          request_type, top_frame_origin, frame_origin, site_for_cookies, nonce,
          network_isolation_partition, std::move(frame_ancestor_relation));
  if (!isolation_info) {
    network::debug::SetDeserializationCrashKeyString("isolation_inconsistent");
    return false;
  }

  *out = std::move(*isolation_info);
  return true;
}

}  // namespace mojo
