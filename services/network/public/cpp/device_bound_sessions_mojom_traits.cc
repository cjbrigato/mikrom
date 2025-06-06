// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/public/cpp/device_bound_sessions_mojom_traits.h"

#include "services/network/public/cpp/schemeful_site_mojom_traits.h"

namespace mojo {

// static
bool StructTraits<network::mojom::DeviceBoundSessionKeyDataView,
                  net::device_bound_sessions::SessionKey>::
    Read(network::mojom::DeviceBoundSessionKeyDataView data,
         net::device_bound_sessions::SessionKey* out) {
  if (!data.ReadSite(&out->site)) {
    return false;
  }

  if (!data.ReadId(&out->id.value())) {
    return false;
  }

  return true;
}

// static
bool StructTraits<network::mojom::DeviceBoundSessionAccessDataView,
                  net::device_bound_sessions::SessionAccess>::
    Read(network::mojom::DeviceBoundSessionAccessDataView data,
         net::device_bound_sessions::SessionAccess* out) {
  if (!data.ReadAccessType(&out->access_type)) {
    return false;
  }

  if (!data.ReadSessionKey(&out->session_key)) {
    return false;
  }

  if (!data.ReadCookies(&out->cookies)) {
    return false;
  }

  return true;
}

}  // namespace mojo
