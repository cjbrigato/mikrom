// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_PROTOCOL_SDP_MESSAGE_H_
#define REMOTING_PROTOCOL_SDP_MESSAGE_H_

#include <map>
#include <string>
#include <vector>

namespace webrtc {
struct SdpVideoFormat;
}

namespace remoting::protocol {

// SdpMessage is used to process session descriptions messages in SDP format
// generated by WebRTC (see RFC 4566). In particularly it allows configuring
// video and audio codecs.
//
// It also normalizes the SDP message to make sure the text used for HMAC
// signature verification is the same that was signed on the sending side.
// This is necessary because WebRTC generates SDP with CRLF line endings which
// are sometimes converted to LF after passing the signaling channel.
class SdpMessage {
 public:
  explicit SdpMessage(const std::string& sdp);
  ~SdpMessage();

  bool has_audio() const { return has_audio_; }
  bool has_video() const { return has_video_; }

  // Returns string representation of the SDP message. The result always has
  // line-endings normalized to CR+LF and empty lines removed.
  std::string ToString() const;

  // Returns string representation of the SDP message for the purpose of
  // computing or verifying its signature, which is transmitted along with the
  // SDP over signaling channel. This must be implemented in exactly the same
  // way at each end of the connection.
  std::string NormalizedForSignature() const;

  // Adds specified parameters for the |codec|. Returns false if the |codec| is
  // not listed anywhere in the SDP message.
  bool AddCodecParameter(const std::string& codec,
                         const std::string& parameters_to_add);

  // Reorders the list of video formats by placing the payload associated with
  // |format| at the front. By doing this, the sender-side of the connection
  // will try to use that codec first. The SDP message will not be modified if
  // |format| is not a supported codec.
  void SetPreferredVideoFormat(const webrtc::SdpVideoFormat& format);

 private:
  struct Payload {
    size_t index;
    std::string type;
  };
  using Payloads = std::vector<Payload>;

  // Finds the lines of the form "a=rtpmap:<payload_type> <codec>/.." with the
  // specified |codec| and returns a list of the matching payload types with
  // their line numbers.
  Payloads FindCodecPayloads(const std::string& codec) const;

  // Overload for |FindCodec| which also filters based on fmtp parameter, such
  // as profile-id for video codecs.
  Payloads FindCodecPayloads(const std::string& codec,
                             const std::string& fmtp_param) const;

  std::string GetFmtpFragmentForSdpVideoFormat(
      const webrtc::SdpVideoFormat& format) const;

  std::vector<std::string> sdp_lines_;

  bool has_audio_ = false;
  bool has_video_ = false;
};

}  // namespace remoting::protocol

#endif  // REMOTING_PROTOCOL_SDP_MESSAGE_H_
