// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_PROTOCOL_CHANNEL_SOCKET_ADAPTER_H_
#define REMOTING_PROTOCOL_CHANNEL_SOCKET_ADAPTER_H_

#include <stddef.h>

#include "base/compiler_specific.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/threading/thread_checker.h"
#include "net/base/net_errors.h"
#include "remoting/protocol/p2p_datagram_socket.h"
// TODO(zhihuang):Replace #include by forward declaration once proper
// inheritance is defined for webrtc::IceTransportInternal and
// webrtc::P2PTransportChannel.
#include "third_party/webrtc/p2p/base/ice_transport_internal.h"
// TODO(johan): Replace #include by forward declaration once proper
// inheritance is defined for webrtc::PacketTransportInterface and
// webrtc::TransportChannel.
#include "third_party/webrtc/p2p/base/packet_transport_internal.h"
#include "third_party/webrtc/rtc_base/async_packet_socket.h"
#include "third_party/webrtc/rtc_base/socket_address.h"
#include "third_party/webrtc/rtc_base/third_party/sigslot/sigslot.h"

namespace remoting::protocol {

// TransportChannelSocketAdapter implements P2PDatagramSocket interface on
// top of webrtc::IceTransportInternal. It is used by IceTransport to provide
// P2PDatagramSocket interface for channels.
class TransportChannelSocketAdapter : public P2PDatagramSocket,
                                      public sigslot::has_slots<> {
 public:
  // Doesn't take ownership of |ice_transport|. |ice_transport| must outlive
  // this adapter.
  explicit TransportChannelSocketAdapter(
      webrtc::IceTransportInternal* ice_transport);

  TransportChannelSocketAdapter(const TransportChannelSocketAdapter&) = delete;
  TransportChannelSocketAdapter& operator=(
      const TransportChannelSocketAdapter&) = delete;

  ~TransportChannelSocketAdapter() override;

  // Sets callback that should be called when the adapter is being
  // destroyed. The callback is not allowed to touch the adapter, but
  // can do anything else, e.g. destroy the TransportChannel.
  void SetOnDestroyedCallback(base::OnceClosure callback);

  // Closes the stream. |error_code| specifies error code that will
  // be returned by Recv() and Send() after the stream is closed.
  // Must be called before the session and the channel are destroyed.
  void Close(int error_code);

  // P2PDatagramSocket interface.
  int Recv(const scoped_refptr<net::IOBuffer>& buf,
           int buf_len,
           const net::CompletionRepeatingCallback& callback) override;
  int Send(const scoped_refptr<net::IOBuffer>& buf,
           int buf_len,
           const net::CompletionRepeatingCallback& callback) override;

 private:
  void OnNewPacket(webrtc::PacketTransportInternal* transport,
                   const webrtc::ReceivedIpPacket& packet);
  void OnWritableState(webrtc::PacketTransportInternal* transport);
  void OnChannelDestroyed(webrtc::IceTransportInternal* ice_transport);

  raw_ptr<webrtc::IceTransportInternal> channel_;

  base::OnceClosure destruction_callback_;

  net::CompletionRepeatingCallback read_callback_;
  scoped_refptr<net::IOBuffer> read_buffer_;
  int read_buffer_size_;

  net::CompletionRepeatingCallback write_callback_;
  scoped_refptr<net::IOBuffer> write_buffer_;
  int write_buffer_size_;

  int closed_error_code_ = net::OK;

  THREAD_CHECKER(thread_checker_);
};

}  // namespace remoting::protocol

#endif  // REMOTING_PROTOCOL_CHANNEL_SOCKET_ADAPTER_H_
