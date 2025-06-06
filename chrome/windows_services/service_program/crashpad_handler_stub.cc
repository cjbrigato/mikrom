// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/windows_services/service_program/crashpad_handler.h"

std::optional<int> RunAsCrashpadHandlerIfRequired(const base::CommandLine&) {
  return std::nullopt;
}
