// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/390223051): Remove C-library calls to fix the errors.
#pragma allow_unsafe_libc_calls
#endif

#include "remoting/protocol/ice_config.h"

#include <algorithm>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/notreached.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "net/base/url_util.h"
#include "remoting/proto/remoting/v1/network_traversal_messages.pb.h"

namespace remoting::protocol {

namespace {

// See draft-petithuguenin-behave-turn-uris-01.
const int kDefaultStunTurnPort = 3478;
const int kDefaultTurnsPort = 5349;

bool ParseLifetime(const std::string& string, base::TimeDelta* result) {
  double seconds = 0;
  if (!base::EndsWith(string, "s", base::CompareCase::INSENSITIVE_ASCII) ||
      !base::StringToDouble(string.substr(0, string.size() - 1), &seconds)) {
    return false;
  }
  *result = base::Seconds(seconds);
  return true;
}

// Returns the smallest specified value, or 0 if neither is specified.
// A value is "specified" if it is greater than 0.
int MinimumSpecified(int value1, int value2) {
  if (value1 <= 0) {
    // value1 is not specified, so return value2 (or 0).
    return std::max(0, value2);
  }
  if (value2 <= 0) {
    // value1 is specified, so return it directly.
    return value1;
  }
  // Both values are specified, so return the minimum.
  return std::min(value1, value2);
}

}  // namespace

IceConfig::IceConfig() = default;
IceConfig::IceConfig(const IceConfig& other) = default;
IceConfig::~IceConfig() = default;

// static
IceConfig IceConfig::Parse(const base::Value::Dict& dictionary) {
  const base::Value::Dict* data = dictionary.FindDict("data");
  if (data) {
    return Parse(*data);
  }

  const base::Value::List* ice_servers_list = dictionary.FindList("iceServers");
  if (!ice_servers_list) {
    return IceConfig();
  }

  IceConfig ice_config;

  // Parse lifetimeDuration field.
  const std::string* lifetime_str = dictionary.FindString("lifetimeDuration");
  base::TimeDelta lifetime;
  if (!lifetime_str || !ParseLifetime(*lifetime_str, &lifetime)) {
    LOG(ERROR) << "Received invalid lifetimeDuration value: " << lifetime_str;

    // If the |lifetimeDuration| field is missing or cannot be parsed then mark
    // the config as expired so it will refreshed for the next session.
    ice_config.expiration_time = base::Time::Now();
  } else {
    ice_config.expiration_time = base::Time::Now() + lifetime;
  }

  // Parse iceServers list and store them in |ice_config|.
  bool errors_found = false;
  ice_config.max_bitrate_kbps = 0;
  for (const auto& server : *ice_servers_list) {
    const base::Value::Dict* server_dict = server.GetIfDict();
    if (!server_dict) {
      errors_found = true;
      continue;
    }

    const base::Value::List* urls_list = server_dict->FindList("urls");
    if (!urls_list) {
      errors_found = true;
      continue;
    }

    std::string username;
    const std::string* maybe_username = server_dict->FindString("username");
    if (maybe_username) {
      username = *maybe_username;
    }

    std::string password;
    const std::string* maybe_password = server_dict->FindString("credential");
    if (maybe_password) {
      password = *maybe_password;
    }

    // Compute the lowest specified bitrate of all the ICE servers.
    // Ideally the bitrate would be stored per ICE server, but it is not
    // possible (at the application level) to look up which particular
    // ICE server was used for the P2P connection.
    auto new_bitrate_double = server_dict->FindDouble("maxRateKbps");
    if (new_bitrate_double.has_value()) {
      ice_config.max_bitrate_kbps =
          MinimumSpecified(ice_config.max_bitrate_kbps,
                           static_cast<int>(new_bitrate_double.value()));
    }

    for (const auto& url : *urls_list) {
      const std::string* url_str = url.GetIfString();
      if (!url_str) {
        errors_found = true;
        continue;
      }
      if (!ice_config.AddServer(*url_str, username, password)) {
        LOG(ERROR) << "Invalid ICE server URL: " << *url_str;
      }
    }
  }

  if (errors_found) {
    std::string json;
    if (!base::JSONWriter::WriteWithOptions(
            dictionary, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json)) {
      NOTREACHED();
    }
    LOG(ERROR) << "Received ICE config with errors: " << json;
  }

  // If there are no STUN or no TURN servers then mark the config as expired so
  // it will refreshed for the next session.
  if (errors_found || ice_config.stun_servers.empty() ||
      ice_config.turn_servers.empty()) {
    ice_config.expiration_time = base::Time::Now();
  }

  return ice_config;
}

// static
IceConfig IceConfig::Parse(const apis::v1::GetIceConfigResponse& config) {
  IceConfig ice_config;

  // Parse lifetimeDuration field.
  base::TimeDelta lifetime =
      base::Seconds(config.lifetime_duration().seconds()) +
      base::Nanoseconds(config.lifetime_duration().nanos());
  ice_config.expiration_time = base::Time::Now() + lifetime;

  // Parse iceServers list and store them in |ice_config|.
  ice_config.max_bitrate_kbps = 0;
  for (const auto& server : config.servers()) {
    // Compute the lowest specified bitrate of all the ICE servers.
    // Ideally the bitrate would be stored per ICE server, but it is not
    // possible (at the application level) to look up which particular
    // ICE server was used for the P2P connection.
    ice_config.max_bitrate_kbps =
        MinimumSpecified(ice_config.max_bitrate_kbps, server.max_rate_kbps());

    for (const auto& url : server.urls()) {
      if (!ice_config.AddServer(url, server.username(), server.credential())) {
        LOG(ERROR) << "Invalid ICE server URL: " << url;
      }
    }
  }

  // If there are no STUN or no TURN servers then mark the config as expired so
  // it will be refreshed for the next session.
  if (ice_config.stun_servers.empty() || ice_config.turn_servers.empty()) {
    ice_config.expiration_time = base::Time::Now();
  }

  return ice_config;
}

bool IceConfig::AddStunServer(std::string_view url) {
  CHECK(url.starts_with("stun:"));
  return AddServer(url, /*username=*/"", /*password=*/"");
}

bool IceConfig::AddServer(std::string_view url,
                          const std::string& username,
                          const std::string& password) {
  webrtc::ProtocolType turn_transport_type = webrtc::PROTO_LAST;

  const char kTcpTransportSuffix[] = "?transport=tcp";
  const char kUdpTransportSuffix[] = "?transport=udp";
  if (base::EndsWith(url, kTcpTransportSuffix,
                     base::CompareCase::INSENSITIVE_ASCII)) {
    turn_transport_type = webrtc::PROTO_TCP;
    url.remove_suffix(strlen(kTcpTransportSuffix));
  } else if (base::EndsWith(url, kUdpTransportSuffix,
                            base::CompareCase::INSENSITIVE_ASCII)) {
    turn_transport_type = webrtc::PROTO_UDP;
    url.remove_suffix(strlen(kUdpTransportSuffix));
  }

  auto parts = base::SplitStringOnce(url, ':');
  if (!parts) {
    return false;
  }

  auto [protocol, host_and_port] = *parts;
  std::string host;
  int port;
  if (!net::ParseHostAndPort(host_and_port, &host, &port)) {
    return false;
  }

  if (protocol == "stun") {
    if (port == -1) {
      port = kDefaultStunTurnPort;
    }
    stun_servers.emplace_back(host, port);
  } else if (protocol == "turn") {
    if (port == -1) {
      port = kDefaultStunTurnPort;
    }
    if (turn_transport_type == webrtc::PROTO_LAST) {
      turn_transport_type = webrtc::PROTO_UDP;
    }
    turn_servers.emplace_back(host, port, username, password,
                              turn_transport_type, false);
  } else if (protocol == "turns") {
    if (port == -1) {
      port = kDefaultTurnsPort;
    }
    if (turn_transport_type == webrtc::PROTO_LAST) {
      turn_transport_type = webrtc::PROTO_TCP;
    }
    turn_servers.emplace_back(host, port, username, password,
                              turn_transport_type, true);
  } else {
    return false;
  }

  return true;
}

}  // namespace remoting::protocol
