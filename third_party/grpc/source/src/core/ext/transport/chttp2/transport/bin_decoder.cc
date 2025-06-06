//
//
// Copyright 2016 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/bin_decoder.h"

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>

#include "absl/base/attributes.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "src/core/lib/slice/slice.h"

static uint8_t decode_table[] = {
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 62,   0x40, 0x40, 0x40, 63,
    52,   53,   54,   55,   56,   57,   58,   59,   60,   61,   0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0,    1,    2,    3,    4,    5,    6,
    7,    8,    9,    10,   11,   12,   13,   14,   15,   16,   17,   18,
    19,   20,   21,   22,   23,   24,   25,   0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 26,   27,   28,   29,   30,   31,   32,   33,   34,   35,   36,
    37,   38,   39,   40,   41,   42,   43,   44,   45,   46,   47,   48,
    49,   50,   51,   0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40};

static const uint8_t tail_xtra[4] = {0, 0, 1, 2};

static bool input_is_valid(const uint8_t* input_ptr, size_t length) {
  size_t i;

  for (i = 0; i < length; ++i) {
    if (GPR_UNLIKELY((decode_table[input_ptr[i]] & 0xC0) != 0)) {
      ABSL_LOG(ERROR) << "Base64 decoding failed, invalid character '"
                 << static_cast<char>(*input_ptr) << "' in base64 input.\n";
      return false;
    }
  }
  return true;
}

#define COMPOSE_OUTPUT_BYTE_0(input_ptr)          \
  (uint8_t)((decode_table[(input_ptr)[0]] << 2) | \
            (decode_table[(input_ptr)[1]] >> 4))

#define COMPOSE_OUTPUT_BYTE_1(input_ptr)          \
  (uint8_t)((decode_table[(input_ptr)[1]] << 4) | \
            (decode_table[(input_ptr)[2]] >> 2))

#define COMPOSE_OUTPUT_BYTE_2(input_ptr) \
  (uint8_t)((decode_table[(input_ptr)[2]] << 6) | decode_table[(input_ptr)[3]])

// By RFC 4648, if the length of the encoded string without padding is 4n+r,
// the length of decoded string is: 1) 3n if r = 0, 2) 3n + 1 if r = 2, 3, or
// 3) invalid if r = 1.
size_t grpc_chttp2_base64_infer_length_after_decode(const grpc_slice& slice) {
  size_t len = GRPC_SLICE_LENGTH(slice);
  const uint8_t* bytes = GRPC_SLICE_START_PTR(slice);
  while (len > 0 && bytes[len - 1] == '=') {
    len--;
  }
  if (GPR_UNLIKELY(GRPC_SLICE_LENGTH(slice) - len > 2)) {
    ABSL_LOG(ERROR) << "Base64 decoding failed. Input has more than 2 paddings.";
    return 0;
  }
  size_t tuples = len / 4;
  size_t tail_case = len % 4;
  if (GPR_UNLIKELY(tail_case == 1)) {
    ABSL_LOG(ERROR) << "Base64 decoding failed. Input has a length of " << len
               << " (without padding), which is invalid.\n";
    return 0;
  }
  return (tuples * 3) + tail_xtra[tail_case];
}

bool grpc_base64_decode_partial(struct grpc_base64_decode_context* ctx) {
  size_t input_tail;

  if (ctx->input_cur > ctx->input_end || ctx->output_cur > ctx->output_end) {
    return false;
  }

  // Process a block of 4 input characters and 3 output bytes
  while (ctx->input_end >= ctx->input_cur + 4 &&
         ctx->output_end >= ctx->output_cur + 3) {
    if (!input_is_valid(ctx->input_cur, 4)) return false;
    ctx->output_cur[0] = COMPOSE_OUTPUT_BYTE_0(ctx->input_cur);
    ctx->output_cur[1] = COMPOSE_OUTPUT_BYTE_1(ctx->input_cur);
    ctx->output_cur[2] = COMPOSE_OUTPUT_BYTE_2(ctx->input_cur);
    ctx->output_cur += 3;
    ctx->input_cur += 4;
  }

  // Process the tail of input data
  input_tail = static_cast<size_t>(ctx->input_end - ctx->input_cur);
  if (input_tail == 4) {
    // Process the input data with pad chars
    if (ctx->input_cur[3] == '=') {
      if (ctx->input_cur[2] == '=' && ctx->output_end >= ctx->output_cur + 1) {
        if (!input_is_valid(ctx->input_cur, 2)) return false;
        *(ctx->output_cur++) = COMPOSE_OUTPUT_BYTE_0(ctx->input_cur);
        ctx->input_cur += 4;
      } else if (ctx->output_end >= ctx->output_cur + 2) {
        if (!input_is_valid(ctx->input_cur, 3)) return false;
        *(ctx->output_cur++) = COMPOSE_OUTPUT_BYTE_0(ctx->input_cur);
        *(ctx->output_cur++) = COMPOSE_OUTPUT_BYTE_1(ctx->input_cur);
        ;
        ctx->input_cur += 4;
      }
    }

  } else if (ctx->contains_tail && input_tail > 1) {
    // Process the input data without pad chars, but constains_tail is set
    if (ctx->output_end >= ctx->output_cur + tail_xtra[input_tail]) {
      if (!input_is_valid(ctx->input_cur, input_tail)) return false;
      switch (input_tail) {
        case 3:
          ctx->output_cur[1] = COMPOSE_OUTPUT_BYTE_1(ctx->input_cur);
          [[fallthrough]];
        case 2:
          ctx->output_cur[0] = COMPOSE_OUTPUT_BYTE_0(ctx->input_cur);
      }
      ctx->output_cur += tail_xtra[input_tail];
      ctx->input_cur += input_tail;
    }
  }

  return true;
}

grpc_slice grpc_chttp2_base64_decode(const grpc_slice& input) {
  size_t input_length = GRPC_SLICE_LENGTH(input);
  size_t output_length = input_length / 4 * 3;
  struct grpc_base64_decode_context ctx;
  grpc_slice output;

  if (GPR_UNLIKELY(input_length % 4 != 0)) {
    ABSL_LOG(ERROR) << "Base64 decoding failed, input of "
                  "grpc_chttp2_base64_decode has a length of "
               << input_length << ", which is not a multiple of 4.\n";
    return grpc_empty_slice();
  }

  if (input_length > 0) {
    const uint8_t* input_end = GRPC_SLICE_END_PTR(input);
    if (*(--input_end) == '=') {
      output_length--;
      if (*(--input_end) == '=') {
        output_length--;
      }
    }
  }
  output = GRPC_SLICE_MALLOC(output_length);

  ctx.input_cur = GRPC_SLICE_START_PTR(input);
  ctx.input_end = GRPC_SLICE_END_PTR(input);
  ctx.output_cur = GRPC_SLICE_START_PTR(output);
  ctx.output_end = GRPC_SLICE_END_PTR(output);
  ctx.contains_tail = false;

  if (GPR_UNLIKELY(!grpc_base64_decode_partial(&ctx))) {
    char* s = grpc_slice_to_c_string(input);
    ABSL_LOG(ERROR) << "Base64 decoding failed, input string:\n" << s << "\n";
    gpr_free(s);
    grpc_core::CSliceUnref(output);
    return grpc_empty_slice();
  }
  ABSL_CHECK(ctx.output_cur == GRPC_SLICE_END_PTR(output));
  ABSL_CHECK(ctx.input_cur == GRPC_SLICE_END_PTR(input));
  return output;
}

grpc_slice grpc_chttp2_base64_decode_with_length(const grpc_slice& input,
                                                 size_t output_length) {
  size_t input_length = GRPC_SLICE_LENGTH(input);
  grpc_slice output = GRPC_SLICE_MALLOC(output_length);
  struct grpc_base64_decode_context ctx;

  // The length of a base64 string cannot be 4 * n + 1
  if (GPR_UNLIKELY(input_length % 4 == 1)) {
    ABSL_LOG(ERROR) << "Base64 decoding failed, input of "
                  "grpc_chttp2_base64_decode_with_length has a length of "
               << input_length << ", which has a tail of 1 byte.\n";
    grpc_core::CSliceUnref(output);
    return grpc_empty_slice();
  }

  if (GPR_UNLIKELY(output_length >
                   input_length / 4 * 3 + tail_xtra[input_length % 4])) {
    ABSL_LOG(ERROR) << "Base64 decoding failed, output_length " << output_length
               << " is longer than the max possible output length "
               << ((input_length / 4 * 3) + tail_xtra[input_length % 4])
               << ".\n";
    grpc_core::CSliceUnref(output);
    return grpc_empty_slice();
  }

  ctx.input_cur = GRPC_SLICE_START_PTR(input);
  ctx.input_end = GRPC_SLICE_END_PTR(input);
  ctx.output_cur = GRPC_SLICE_START_PTR(output);
  ctx.output_end = GRPC_SLICE_END_PTR(output);
  ctx.contains_tail = true;

  if (GPR_UNLIKELY(!grpc_base64_decode_partial(&ctx))) {
    char* s = grpc_slice_to_c_string(input);
    ABSL_LOG(ERROR) << "Base64 decoding failed, input string:\n" << s << "\n";
    gpr_free(s);
    grpc_core::CSliceUnref(output);
    return grpc_empty_slice();
  }
  ABSL_CHECK(ctx.output_cur == GRPC_SLICE_END_PTR(output));
  ABSL_CHECK(ctx.input_cur <= GRPC_SLICE_END_PTR(input));
  return output;
}
