// Copyright 2018 gRPC authors.
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

#include "src/core/lib/iomgr/internal_errqueue.h"

#include <grpc/support/port_platform.h>

#include "absl/log/absl_log.h"
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_TCP

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

#include "src/core/util/strerror.h"

namespace grpc_core {

bool KernelSupportsErrqueue() {
  static const bool errqueue_supported = []() {
#ifdef GRPC_LINUX_ERRQUEUE
    // Both-compile time and run-time linux kernel versions should be at
    // least 4.0.0
    struct utsname buffer;
    if (uname(&buffer) != 0) {
      ABSL_LOG(ERROR) << "uname: " << StrError(errno);
      return false;
    }
    char* release = buffer.release;
    if (release == nullptr) {
      return false;
    }

    if (strtol(release, nullptr, 10) >= 4) {
      return true;
    } else {
      ABSL_VLOG(2) << "ERRQUEUE support not enabled";
    }
#endif  // GRPC_LINUX_ERRQUEUE
    return false;
  }();
  return errqueue_supported;
}
}  // namespace grpc_core

#endif  // GRPC_POSIX_SOCKET_TCP
