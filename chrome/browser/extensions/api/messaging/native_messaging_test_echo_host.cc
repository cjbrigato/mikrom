// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a simple Native Message Host application. It echoes any messages
// it receives.
#include <windows.h>

#include <stdint.h>
#include <string.h>

#include "base/containers/span.h"
#include "base/files/file.h"

int main(int argc, char* argv[]) {
  base::File read_stream = base::File(GetStdHandle(STD_INPUT_HANDLE));
  base::File write_stream = base::File(GetStdHandle(STD_OUTPUT_HANDLE));

  uint32_t message_len = 0;

  // Read and echo messages in a loop.
  while (true) {
    // Read the message's length prefix.
    size_t bytes_read =
        read_stream.ReadAtCurrentPos(base::byte_span_from_ref(message_len))
            .value_or(0);

    // If stdin was closed, the host should exit.
    if (bytes_read != sizeof(message_len)) {
      break;
    }

    // Read the message body.
    std::vector<uint8_t> message_body(message_len, '\0');
    bytes_read = read_stream.ReadAtCurrentPos(message_body).value_or(0);

    // Bail if we failed to read the entire message.
    if (bytes_read != message_len) {
      break;
    }

    // Reply by echoing the message length and body.
    write_stream.WriteAtCurrentPos(base::byte_span_from_ref(message_len));
    write_stream.WriteAtCurrentPos(message_body);
  }
}
