// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/390223051): Remove C-library calls to fix the errors.
#pragma allow_unsafe_libc_calls
#endif

#include "components/webrtc/net_address_utils.h"

#include <stdint.h>

#include <memory>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "third_party/webrtc/rtc_base/byte_order.h"
#include "third_party/webrtc/rtc_base/socket_address.h"

namespace webrtc {

bool IPEndPointToSocketAddress(const net::IPEndPoint& ip_endpoint,
                               webrtc::SocketAddress* address) {
  sockaddr_storage addr;
  socklen_t len = sizeof(addr);
  return ip_endpoint.ToSockAddr(reinterpret_cast<sockaddr*>(&addr), &len) &&
         webrtc::SocketAddressFromSockAddrStorage(addr, address);
}

bool SocketAddressToIPEndPoint(const webrtc::SocketAddress& address,
                               net::IPEndPoint* ip_endpoint) {
  sockaddr_storage addr;
  int size = address.ToSockAddrStorage(&addr);
  return (size > 0) &&
         ip_endpoint->FromSockAddr(reinterpret_cast<sockaddr*>(&addr), size);
}

webrtc::IPAddress NetIPAddressToRtcIPAddress(const net::IPAddress& ip_address) {
  if (ip_address.IsIPv4()) {
    uint32_t address;
    memcpy(&address, ip_address.bytes().data(), sizeof(uint32_t));
    address = webrtc::NetworkToHost32(address);
    return webrtc::IPAddress(address);
  }
  if (ip_address.IsIPv6()) {
    in6_addr address;
    memcpy(&address, ip_address.bytes().data(), sizeof(in6_addr));
    return webrtc::IPAddress(address);
  }
  return webrtc::IPAddress();
}

net::IPAddress RtcIPAddressToNetIPAddress(const webrtc::IPAddress& ip_address) {
  webrtc::SocketAddress socket_address(ip_address, 0);
  net::IPEndPoint ip_endpoint;
  webrtc::SocketAddressToIPEndPoint(socket_address, &ip_endpoint);
  return ip_endpoint.address();
}

}  // namespace webrtc
