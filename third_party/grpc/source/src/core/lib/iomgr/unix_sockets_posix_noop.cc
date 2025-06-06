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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/unix_sockets_posix.h"

#ifndef GRPC_HAVE_UNIX_SOCKET

#include <string>

#include "absl/log/absl_check.h"

void grpc_create_socketpair_if_unix(int /* sv */[2]) {
  // TODO: Either implement this for the non-Unix socket case or make
  // sure that it is never called in any such case. Until then, leave an
  // assertion to notify if this gets called inadvertently
  ABSL_CHECK(0);
}

absl::StatusOr<std::vector<grpc_resolved_address>>
grpc_resolve_unix_domain_address(absl::string_view /* name */) {
  return absl::UnknownError("Unix domain sockets are not supported on Windows");
}

absl::StatusOr<std::vector<grpc_resolved_address>>
grpc_resolve_unix_abstract_domain_address(absl::string_view /* name */) {
  return absl::UnknownError("Unix domain sockets are not supported on Windows");
}

int grpc_is_unix_socket(const grpc_resolved_address* /* addr */) {
  return false;
}

void grpc_unlink_if_unix_domain_socket(
    const grpc_resolved_address* /* addr */) {}

#endif
