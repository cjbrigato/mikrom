// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/p2p/socket_tcp.h"

#include <stddef.h>

#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/containers/span_writer.h"
#include "base/functional/bind.h"
#include "base/numerics/byte_conversions.h"
#include "base/time/time.h"
#include "components/webrtc/fake_ssl_client_socket.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/network_anonymization_key.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/ssl_client_socket.h"
#include "net/socket/tcp_client_socket.h"
#include "services/network/proxy_resolving_client_socket.h"
#include "services/network/proxy_resolving_client_socket_factory.h"
#include "services/network/public/cpp/p2p_param_traits.h"
#include "third_party/webrtc/media/base/rtp_utils.h"
#include "third_party/webrtc/rtc_base/time_utils.h"
#include "url/gurl.h"

namespace network {
namespace {

using PacketLength = uint16_t;
constexpr size_t kPacketHeaderSize = sizeof(PacketLength);
constexpr int kTcpReadBufferSize = 4096;
constexpr int kPacketLengthOffset = 2;
constexpr int kTurnChannelDataHeaderSize = 4;
constexpr int kTcpRecvSocketBufferSize = 128 * 1024;
constexpr int kTcpSendSocketBufferSize = 128 * 1024;

bool IsTlsClientSocket(P2PSocketType type) {
  return (type == P2P_SOCKET_STUN_TLS_CLIENT || type == P2P_SOCKET_TLS_CLIENT);
}

bool IsPseudoTlsClientSocket(P2PSocketType type) {
  return (type == P2P_SOCKET_SSLTCP_CLIENT ||
          type == P2P_SOCKET_STUN_SSLTCP_CLIENT);
}

}  // namespace

P2PSocketTcp::SendBuffer::SendBuffer() : rtc_packet_id(-1) {}
P2PSocketTcp::SendBuffer::SendBuffer(
    int32_t rtc_packet_id,
    scoped_refptr<net::DrainableIOBuffer> buffer)
    : rtc_packet_id(rtc_packet_id), buffer(buffer) {}
P2PSocketTcp::SendBuffer::SendBuffer(const SendBuffer& rhs) = default;
P2PSocketTcp::SendBuffer::~SendBuffer() = default;

P2PSocketTcpBase::P2PSocketTcpBase(
    Delegate* delegate,
    mojo::PendingRemote<mojom::P2PSocketClient> client,
    mojo::PendingReceiver<mojom::P2PSocket> socket,
    P2PSocketType type,
    const net::NetworkTrafficAnnotationTag& traffic_annotation,
    ProxyResolvingClientSocketFactory* proxy_resolving_socket_factory)
    : P2PSocket(delegate, std::move(client), std::move(socket), P2PSocket::TCP),
      type_(type),
      traffic_annotation_(traffic_annotation),
      proxy_resolving_socket_factory_(proxy_resolving_socket_factory) {}

P2PSocketTcpBase::~P2PSocketTcpBase() = default;

void P2PSocketTcpBase::InitAccepted(const net::IPEndPoint& remote_address,
                                    std::unique_ptr<net::StreamSocket> socket) {
  DCHECK(socket);
  remote_address_.ip_address = remote_address;
  // TODO(ronghuawu): Add FakeSSLServerSocket.
  socket_ = std::move(socket);
  DoRead();
}

void P2PSocketTcpBase::Init(
    const net::IPEndPoint& local_address,
    uint16_t min_port,
    uint16_t max_port,
    const P2PHostAndIPEndPoint& remote_address,
    const net::NetworkAnonymizationKey& network_anonymization_key) {
  DCHECK(!socket_);

  remote_address_ = remote_address;

  net::HostPortPair dest_host_port_pair;
  // If there is a domain name, let's try it first, it's required by some proxy
  // to only take hostname for CONNECT. If it has been DNS resolved, the result
  // is likely cached and shouldn't cause 2nd DNS resolution in the case of
  // direct connect (i.e. no proxy).
  if (!remote_address.hostname.empty()) {
    dest_host_port_pair = net::HostPortPair(remote_address.hostname,
                                            remote_address.ip_address.port());
  } else {
    DCHECK(!remote_address.ip_address.address().empty());
    dest_host_port_pair =
        net::HostPortPair::FromIPEndPoint(remote_address.ip_address);
  }

  // TODO(mallinath) - We are ignoring local_address altogether. We should
  // find a way to inject this into ProxyResolvingClientSocket. This could be
  // a problem on multi-homed host.

  socket_ = proxy_resolving_socket_factory_->CreateSocket(
      GURL("https://" + dest_host_port_pair.ToString()),
      network_anonymization_key, IsTlsClientSocket(type_));

  if (IsPseudoTlsClientSocket(type_)) {
    socket_ = std::make_unique<webrtc::FakeSSLClientSocket>(std::move(socket_));
  }

  int status = socket_->Connect(
      base::BindOnce(&P2PSocketTcpBase::OnConnected, base::Unretained(this)));
  if (status != net::ERR_IO_PENDING)
    OnConnected(status);
}

void P2PSocketTcpBase::OnConnected(int result) {
  DCHECK_NE(result, net::ERR_IO_PENDING);

  if (result != net::OK) {
    LOG(WARNING) << "Error from connecting socket, result=" << result;
    OnError();
    return;
  }

  OnOpen();
}

void P2PSocketTcpBase::OnOpen() {
  // Setting socket send and receive buffer size.
  if (net::OK != socket_->SetReceiveBufferSize(kTcpRecvSocketBufferSize)) {
    LOG(WARNING) << "Failed to set socket receive buffer size to "
                 << kTcpRecvSocketBufferSize;
  }

  if (net::OK != socket_->SetSendBufferSize(kTcpSendSocketBufferSize)) {
    LOG(WARNING) << "Failed to set socket send buffer size to "
                 << kTcpSendSocketBufferSize;
  }

  if (!DoSendSocketCreateMsg())
    return;

  DoRead();
}

bool P2PSocketTcpBase::DoSendSocketCreateMsg() {
  DCHECK(socket_.get());

  net::IPEndPoint local_address;
  int result = socket_->GetLocalAddress(&local_address);
  if (result < 0) {
    LOG(ERROR) << "P2PSocketTcpBase::OnConnected: unable to get local"
               << " address: " << result;
    OnError();
    return false;
  }

  VLOG(1) << "Local address: " << local_address.ToString();

  net::IPEndPoint remote_address;

  // GetPeerAddress returns ERR_NAME_NOT_RESOLVED if the socket is connected
  // through a proxy.
  result = socket_->GetPeerAddress(&remote_address);
  if (result < 0 && result != net::ERR_NAME_NOT_RESOLVED) {
    LOG(ERROR) << "P2PSocketTcpBase::OnConnected: unable to get peer"
               << " address: " << result;
    OnError();
    return false;
  }

  if (!remote_address.address().empty()) {
    VLOG(1) << "Remote address: " << remote_address.ToString();
    if (remote_address_.ip_address.address().empty()) {
      // Save |remote_address| if address is empty.
      remote_address_.ip_address = remote_address;
    }
  } else {
    VLOG(1) << "Remote address is unknown since connection is proxied";
  }

  // If we are not doing TLS, we are ready to send data now.
  // In case of TLS SignalConnect will be sent only after TLS handshake is
  // successful. So no buffering will be done at socket handlers if any
  // packets sent before that by the application.
  client_->SocketCreated(local_address, remote_address);
  return true;
}

void P2PSocketTcpBase::DoRead() {
  while (true) {
    if (!read_buffer_.get()) {
      read_buffer_ = base::MakeRefCounted<net::GrowableIOBuffer>();
      read_buffer_->SetCapacity(kTcpReadBufferSize);
    } else if (read_buffer_->RemainingCapacity() < kTcpReadBufferSize) {
      // Make sure that we always have at least kTcpReadBufferSize of
      // remaining capacity in the read buffer. Normally all packets
      // are smaller than kTcpReadBufferSize, so this is not really
      // required.
      read_buffer_->SetCapacity(read_buffer_->capacity() + kTcpReadBufferSize -
                                read_buffer_->RemainingCapacity());
    }
    const int result = socket_->Read(
        read_buffer_.get(), read_buffer_->RemainingCapacity(),
        base::BindOnce(&P2PSocketTcp::OnRead, base::Unretained(this)));
    if (result == net::ERR_IO_PENDING || !HandleReadResult(result))
      return;
  }
}

void P2PSocketTcpBase::OnRead(int result) {
  if (HandleReadResult(result))
    DoRead();
}

bool P2PSocketTcpBase::OnPacket(base::span<const uint8_t> data) {
  if (!connected_) {
    P2PSocket::StunMessageType type;
    bool stun = GetStunPacketType(data, &type);
    if (stun && IsRequestOrResponse(type)) {
      connected_ = true;
    } else if (!stun || type == STUN_DATA_INDICATION) {
      LOG(ERROR) << "Received unexpected data packet from "
                 << remote_address_.ip_address.ToString()
                 << " before STUN binding is finished. "
                 << "Terminating connection.";
      OnError();
      return false;
    }
  }

  if (data.size() == 0) {
    // https://tools.ietf.org/html/rfc4571#section-2 allows null packets which
    // are ignored.
    LOG(WARNING) << "Ignoring empty RTP-over-TCP frame.";
    return true;
  }

  auto packet = mojom::P2PReceivedPacket::New(
      data, remote_address_.ip_address,
      base::TimeTicks() + base::Nanoseconds(webrtc::TimeNanos()),
      webrtc::EcnMarking::kNotEct);

  std::vector<mojom::P2PReceivedPacketPtr> received_packets;
  received_packets.push_back(std::move(packet));

  // TODO(crbug.com/40243224): Batch multiple packets in the TCP case as well.
  client_->DataReceived(std::move(received_packets));

  delegate_->DumpPacket(data, true);
  return true;
}

void P2PSocketTcpBase::WriteOrQueue(SendBuffer& send_buffer) {
  if (write_buffer_.buffer.get()) {
    write_queue_.push(send_buffer);
    return;
  }

  write_buffer_ = send_buffer;
  DoWrite();
}

void P2PSocketTcpBase::DoWrite() {
  while (!write_pending_ && write_buffer_.buffer.get()) {
    int result = socket_->Write(
        write_buffer_.buffer.get(), write_buffer_.buffer->BytesRemaining(),
        base::BindOnce(&P2PSocketTcp::OnWritten, base::Unretained(this)),
        traffic_annotation_);

    if (result == net::ERR_IO_PENDING) {
      write_pending_ = true;
    } else if (!HandleWriteResult(result)) {
      break;
    }
  }
}

void P2PSocketTcpBase::OnWritten(int result) {
  DCHECK(write_pending_);
  DCHECK_NE(result, net::ERR_IO_PENDING);

  write_pending_ = false;

  if (HandleWriteResult(result))
    DoWrite();
}

bool P2PSocketTcpBase::HandleWriteResult(int result) {
  DCHECK(write_buffer_.buffer.get());

  if (result < 0) {
    LOG(ERROR) << "Error when sending data in TCP socket: " << result;
    OnError();
    return false;
  }

  write_buffer_.buffer->DidConsume(result);
  if (write_buffer_.buffer->BytesRemaining() == 0) {
    int64_t send_time_ms = webrtc::TimeMillis();
    client_->SendComplete(
        P2PSendPacketMetrics(0, write_buffer_.rtc_packet_id, send_time_ms));
    if (write_queue_.empty()) {
      write_buffer_.buffer = nullptr;
      write_buffer_.rtc_packet_id = -1;
    } else {
      write_buffer_ = write_queue_.front();
      write_queue_.pop();
    }
  }
  return true;
}

bool P2PSocketTcpBase::HandleReadResult(int result) {
  if (result < 0) {
    LOG(ERROR) << "Error when reading from TCP socket: " << result;
    OnError();
    return false;
  } else if (result == 0) {
    LOG(WARNING) << "Remote peer has shutdown TCP socket.";
    OnError();
    return false;
  }

  read_buffer_->set_offset(read_buffer_->offset() + result);
  base::span<uint8_t> span = read_buffer_->span_before_offset();
  while (!span.empty()) {
    size_t bytes_consumed = 0;
    if (!ProcessInput(span, &bytes_consumed)) {
      return false;
    }
    if (!bytes_consumed) {
      break;
    }
    span = span.subspan(bytes_consumed);
  }
  // We've consumed all complete packets from the buffer; now move any remaining
  // bytes to the head of the buffer and set offset to reflect this.
  read_buffer_->everything().copy_prefix_from(span);
  read_buffer_->set_offset(span.size());

  return true;
}

bool P2PSocketTcpBase::SendPacket(base::span<const uint8_t> data,
                                  const P2PPacketInfo& packet_info) {
  // Renderer should use this socket only to send data to |remote_address_|.
  if (data.size() > kMaximumPacketSize ||
      !(packet_info.destination == remote_address_.ip_address)) {
    NOTREACHED();
  }

  if (!connected_) {
    P2PSocket::StunMessageType type = P2PSocket::StunMessageType();
    bool stun = GetStunPacketType(data, &type);
    if (!stun || type == STUN_DATA_INDICATION) {
      LOG(ERROR) << "Page tried to send a data packet to "
                 << packet_info.destination.ToString()
                 << " before STUN binding is finished.";
      OnError();
      return false;
    }
  }

  DoSend(packet_info.destination, data, packet_info.packet_options);
  return true;
}

void P2PSocketTcpBase::Send(base::span<const uint8_t> data,
                            const P2PPacketInfo& packet_info) {
  SendPacket(data, packet_info);
}

void P2PSocketTcpBase::SendBatch(
    std::vector<mojom::P2PSendPacketPtr> packet_batch) {
  for (auto& packet : packet_batch) {
    // If there's an error in SendPacket, this object is destroyed by the
    // internal call to P2PSocket::OnError, so do not reference this after
    // SendPacket returns false.
    if (!SendPacket(packet->data, packet->packet_info)) {
      return;
    }
  }
}

void P2PSocketTcpBase::SetOption(P2PSocketOption option, int32_t value) {
  switch (option) {
    case P2P_SOCKET_OPT_RCVBUF:
      socket_->SetReceiveBufferSize(value);
      break;
    case P2P_SOCKET_OPT_SNDBUF:
      socket_->SetSendBufferSize(value);
      break;
    case P2P_SOCKET_OPT_DSCP:
    case P2P_SOCKET_OPT_RECV_ECN:
      return;  // For TCP sockets DSCP, ECN setting is not available.
    default:
      NOTREACHED();
  }
}

P2PSocketTcp::P2PSocketTcp(
    Delegate* delegate,
    mojo::PendingRemote<mojom::P2PSocketClient> client,
    mojo::PendingReceiver<mojom::P2PSocket> socket,
    P2PSocketType type,
    const net::NetworkTrafficAnnotationTag& traffic_annotation,
    ProxyResolvingClientSocketFactory* proxy_resolving_socket_factory)
    : P2PSocketTcpBase(delegate,
                       std::move(client),
                       std::move(socket),
                       type,
                       traffic_annotation,
                       proxy_resolving_socket_factory) {
  DCHECK(type == P2P_SOCKET_TCP_CLIENT || type == P2P_SOCKET_SSLTCP_CLIENT ||
         type == P2P_SOCKET_TLS_CLIENT);
}

P2PSocketTcp::~P2PSocketTcp() {}

bool P2PSocketTcp::ProcessInput(base::span<const uint8_t> input,
                                size_t* bytes_consumed) {
  *bytes_consumed = 0;
  if (input.size() < kPacketHeaderSize)
    return true;

  uint32_t packet_size = base::U16FromBigEndian(input.first<2u>());
  if (input.size() < packet_size + kPacketHeaderSize)
    return true;

  *bytes_consumed = kPacketHeaderSize + packet_size;

  return OnPacket(input.subspan(kPacketHeaderSize, packet_size));
}

void P2PSocketTcp::DoSend(const net::IPEndPoint& to,
                          base::span<const uint8_t> data,
                          const webrtc::AsyncSocketPacketOptions& options) {
  const size_t buffer_size = kPacketHeaderSize + data.size();
  SendBuffer send_buffer(
      options.packet_id,
      base::MakeRefCounted<net::DrainableIOBuffer>(
          base::MakeRefCounted<net::IOBufferWithSize>(buffer_size),
          buffer_size));
  {
    base::SpanWriter writer(send_buffer.buffer->span());
    writer.WriteU16BigEndian(base::checked_cast<uint16_t>(data.size()));
    // We've written the full header now.
    static_assert(kPacketHeaderSize == sizeof(uint16_t));
    writer.Write(data);
    CHECK_EQ(writer.remaining(), 0u);
  }

  base::span<uint8_t> send_buffer_without_header =
      send_buffer.buffer->span().subspan(kPacketHeaderSize);
  webrtc::ApplyPacketOptions(send_buffer_without_header.data(),
                             send_buffer_without_header.size(),
                             options.packet_time_params, webrtc::TimeMicros());

  WriteOrQueue(send_buffer);
}

// P2PSocketStunTcp
P2PSocketStunTcp::P2PSocketStunTcp(
    Delegate* delegate,
    mojo::PendingRemote<mojom::P2PSocketClient> client,
    mojo::PendingReceiver<mojom::P2PSocket> socket,
    P2PSocketType type,
    const net::NetworkTrafficAnnotationTag& traffic_annotation,
    ProxyResolvingClientSocketFactory* proxy_resolving_socket_factory)
    : P2PSocketTcpBase(delegate,
                       std::move(client),
                       std::move(socket),
                       type,
                       traffic_annotation,
                       proxy_resolving_socket_factory) {
  DCHECK(type == P2P_SOCKET_STUN_TCP_CLIENT ||
         type == P2P_SOCKET_STUN_SSLTCP_CLIENT ||
         type == P2P_SOCKET_STUN_TLS_CLIENT);
}

P2PSocketStunTcp::~P2PSocketStunTcp() {}

bool P2PSocketStunTcp::ProcessInput(base::span<const uint8_t> input,
                                    size_t* bytes_consumed) {
  *bytes_consumed = 0;
  if (input.size() < kPacketHeaderSize + kPacketLengthOffset)
    return true;

  int pad_bytes;
  size_t packet_size = GetExpectedPacketSize(input, &pad_bytes);

  if (input.size() < packet_size + pad_bytes)
    return true;

  // We have a complete packet. Read through it.
  *bytes_consumed = packet_size + pad_bytes;
  return OnPacket(input.first(packet_size));
}

void P2PSocketStunTcp::DoSend(const net::IPEndPoint& to,
                              base::span<const uint8_t> data,
                              const webrtc::AsyncSocketPacketOptions& options) {
  // Each packet is expected to have header (STUN/TURN ChannelData), where
  // header contains message type and and length of message.
  CHECK_GE(data.size(), kPacketHeaderSize + kPacketLengthOffset);

  int pad_bytes;
  size_t expected_len = GetExpectedPacketSize(data, &pad_bytes);

  // Accepts only complete STUN/TURN packets.
  CHECK_EQ(data.size(), expected_len);

  // Add any pad bytes to the total size.
  int buffer_size = data.size() + pad_bytes;
  std::vector<uint8_t> buffer;
  buffer.reserve(buffer_size);
  buffer.assign(data.begin(), data.end());
  if (pad_bytes) {
    DCHECK_LE(pad_bytes, 4);
    buffer.insert(buffer.end(), pad_bytes, 0);
  }

  SendBuffer send_buffer(
      options.packet_id,
      base::MakeRefCounted<net::DrainableIOBuffer>(
          base::MakeRefCounted<net::VectorIOBuffer>(std::move(buffer)),
          buffer_size));

  webrtc::ApplyPacketOptions(send_buffer.buffer->bytes(), data.size(),
                             options.packet_time_params, webrtc::TimeMicros());

  // WriteOrQueue may free the memory, so dump it first.
  delegate_->DumpPacket(send_buffer.buffer->span(), false);

  WriteOrQueue(send_buffer);
}

int P2PSocketStunTcp::GetExpectedPacketSize(base::span<const uint8_t> data,
                                            int* pad_bytes) {
  DCHECK_LE(static_cast<size_t>(kTurnChannelDataHeaderSize), data.size());
  // Get packet type (STUN or TURN).
  uint16_t msg_type = base::U16FromBigEndian(data.subspan<0u, 2u>());
  // Both stun and turn had length at offset 2.
  int packet_size =
      int{base::U16FromBigEndian(data.subspan<kPacketLengthOffset, 2u>())};

  *pad_bytes = 0;
  // Add heder length to packet length.
  if ((msg_type & 0xC000) == 0) {
    packet_size += kStunHeaderSize;
  } else {
    packet_size += kTurnChannelDataHeaderSize;
    // Calculate any padding if present.
    if (packet_size % 4)
      *pad_bytes = 4 - packet_size % 4;
  }
  return packet_size;
}

}  // namespace network
