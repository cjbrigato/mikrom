//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/ext/transport/chttp2/transport/frame_rst_stream.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/port_platform.h>
#include <stddef.h>

#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "absl/random/distributions.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "src/core/ext/transport/chttp2/transport/call_tracer_wrapper.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/legacy_frame.h"
#include "src/core/ext/transport/chttp2/transport/ping_callbacks.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/transport/http2_errors.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/util/status_helper.h"

grpc_slice grpc_chttp2_rst_stream_create(
    uint32_t id, uint32_t code, grpc_core::CallTracerInterface* call_tracer) {
  static const size_t frame_size = 13;
  grpc_slice slice = GRPC_SLICE_MALLOC(frame_size);
  if (call_tracer != nullptr) {
    call_tracer->RecordOutgoingBytes({frame_size, 0, 0});
  }
  uint8_t* p = GRPC_SLICE_START_PTR(slice);

  // Frame size.
  *p++ = 0;
  *p++ = 0;
  *p++ = 4;
  // Frame type.
  *p++ = GRPC_CHTTP2_FRAME_RST_STREAM;
  // Flags.
  *p++ = 0;
  // Stream ID.
  *p++ = static_cast<uint8_t>(id >> 24);
  *p++ = static_cast<uint8_t>(id >> 16);
  *p++ = static_cast<uint8_t>(id >> 8);
  *p++ = static_cast<uint8_t>(id);
  // Error code.
  *p++ = static_cast<uint8_t>(code >> 24);
  *p++ = static_cast<uint8_t>(code >> 16);
  *p++ = static_cast<uint8_t>(code >> 8);
  *p++ = static_cast<uint8_t>(code);

  return slice;
}

void grpc_chttp2_add_rst_stream_to_next_write(
    grpc_chttp2_transport* t, uint32_t id, uint32_t code,
    grpc_core::CallTracerInterface* call_tracer) {
  t->num_pending_induced_frames++;
  grpc_slice_buffer_add(&t->qbuf,
                        grpc_chttp2_rst_stream_create(id, code, call_tracer));
}

grpc_error_handle grpc_chttp2_rst_stream_parser_begin_frame(
    grpc_chttp2_rst_stream_parser* parser, uint32_t length, uint8_t flags) {
  if (length != 4) {
    return GRPC_ERROR_CREATE(absl::StrFormat(
        "invalid rst_stream: length=%d, flags=%02x", length, flags));
  }
  parser->byte = 0;
  return absl::OkStatus();
}

grpc_error_handle grpc_chttp2_rst_stream_parser_parse(void* parser,
                                                      grpc_chttp2_transport* t,
                                                      grpc_chttp2_stream* s,
                                                      const grpc_slice& slice,
                                                      int is_last) {
  const uint8_t* const beg = GRPC_SLICE_START_PTR(slice);
  const uint8_t* const end = GRPC_SLICE_END_PTR(slice);
  const uint8_t* cur = beg;
  grpc_chttp2_rst_stream_parser* p =
      static_cast<grpc_chttp2_rst_stream_parser*>(parser);

  while (p->byte != 4 && cur != end) {
    p->reason_bytes[p->byte] = *cur;
    cur++;
    p->byte++;
  }
  uint64_t framing_bytes = static_cast<uint64_t>(end - cur);
  s->call_tracer_wrapper.RecordIncomingBytes({framing_bytes, 0, 0});

  if (p->byte == 4) {
    ABSL_CHECK(is_last);
    uint32_t reason = ((static_cast<uint32_t>(p->reason_bytes[0])) << 24) |
                      ((static_cast<uint32_t>(p->reason_bytes[1])) << 16) |
                      ((static_cast<uint32_t>(p->reason_bytes[2])) << 8) |
                      ((static_cast<uint32_t>(p->reason_bytes[3])));
    GRPC_TRACE_LOG(http, INFO)
        << "[chttp2 transport=" << t << " stream=" << s
        << "] received RST_STREAM(reason=" << reason << ")";
    grpc_error_handle error;
    if (reason != GRPC_HTTP2_NO_ERROR || s->trailing_metadata_buffer.empty()) {
      error = grpc_error_set_int(
          grpc_error_set_str(
              GRPC_ERROR_CREATE("RST_STREAM"),
              grpc_core::StatusStrProperty::kGrpcMessage,
              absl::StrCat("Received RST_STREAM with error code ", reason)),
          grpc_core::StatusIntProperty::kHttp2Error,
          static_cast<intptr_t>(reason));
    }
    if (!t->is_client &&
        absl::Bernoulli(t->bitgen, t->ping_on_rst_stream_percent / 100.0)) {
      ++t->num_pending_induced_frames;
      t->ping_callbacks.RequestPing();
      grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_KEEPALIVE_PING);
    }
    grpc_chttp2_mark_stream_closed(t, s, true, true, error);
  }

  return absl::OkStatus();
}
