// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_NETWORK_PUBLIC_CPP_ISOLATION_INFO_MOJOM_TRAITS_H_
#define SERVICES_NETWORK_PUBLIC_CPP_ISOLATION_INFO_MOJOM_TRAITS_H_

#include <optional>

#include "mojo/public/cpp/bindings/enum_traits.h"
#include "mojo/public/cpp/bindings/struct_traits.h"
#include "net/base/isolation_info.h"
#include "net/base/network_isolation_partition.h"
#include "net/base/url_util.h"
#include "net/cookies/site_for_cookies.h"
#include "services/network/public/cpp/network_isolation_partition_mojom_traits.h"
#include "services/network/public/cpp/schemeful_site_mojom_traits.h"
#include "services/network/public/mojom/isolation_info.mojom-shared.h"
#include "url/mojom/origin_mojom_traits.h"
#include "url/origin.h"

namespace mojo {

template <>
struct COMPONENT_EXPORT(NETWORK_CPP_BASE)
    EnumTraits<network::mojom::IsolationInfoRequestType,
               net::IsolationInfo::RequestType> {
  static network::mojom::IsolationInfoRequestType ToMojom(
      net::IsolationInfo::RequestType request_type);
  static bool FromMojom(network::mojom::IsolationInfoRequestType request_type,
                        net::IsolationInfo::RequestType* out);
};

template <>
struct COMPONENT_EXPORT(NETWORK_CPP_BASE)
    EnumTraits<network::mojom::IsolationInfoFrameAncestorRelation,
               net::IsolationInfo::FrameAncestorRelation> {
  static network::mojom::IsolationInfoFrameAncestorRelation ToMojom(
      net::IsolationInfo::FrameAncestorRelation frame_ancestor_relation);
  static bool FromMojom(network::mojom::IsolationInfoFrameAncestorRelation
                            frame_ancestor_relation,
                        net::IsolationInfo::FrameAncestorRelation* out);
};

template <>
struct COMPONENT_EXPORT(NETWORK_CPP_BASE)
    StructTraits<network::mojom::IsolationInfoDataView, net::IsolationInfo> {
  static net::IsolationInfo::RequestType request_type(
      const net::IsolationInfo& input) {
    return input.request_type();
  }

  static const std::optional<url::Origin>& top_frame_origin(
      const net::IsolationInfo& input) {
    return input.top_frame_origin();
  }

  static const std::optional<url::Origin>& frame_origin(
      const net::IsolationInfo& input) {
    return input.frame_origin();
  }

  static const std::optional<base::UnguessableToken>& nonce(
      const net::IsolationInfo& input) {
    return input.nonce();
  }

  static net::NetworkIsolationPartition network_isolation_partition(
      const net::IsolationInfo& input) {
    return input.GetNetworkIsolationPartition();
  }

  static const net::SiteForCookies& site_for_cookies(
      const net::IsolationInfo& input) {
    return input.site_for_cookies();
  }

  static std::optional<net::IsolationInfo::FrameAncestorRelation>
  frame_ancestor_relation(const net::IsolationInfo& input) {
    return input.frame_ancestor_relation();
  }

  static bool Read(network::mojom::IsolationInfoDataView data,
                   net::IsolationInfo* out);
};

}  // namespace mojo

#endif  // SERVICES_NETWORK_PUBLIC_CPP_ISOLATION_INFO_MOJOM_TRAITS_H_
