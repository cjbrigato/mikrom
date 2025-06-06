// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/390223051): Remove C-library calls to fix the errors.
#pragma allow_unsafe_libc_calls
#endif

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "net/base/io_buffer.h"
#include "net/dns/dns_query.h"

// Entry point for LibFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  auto packet = base::MakeRefCounted<net::IOBufferWithSize>(size);
  memcpy(packet->data(), data, size);
  auto out = std::make_unique<net::DnsQuery>(packet);
  out->Parse(size);
  return 0;
}
