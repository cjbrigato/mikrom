// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/mock_crx_installer.h"

namespace extensions {

MockCrxInstaller::MockCrxInstaller(content::BrowserContext* context)
    : CrxInstaller(context, nullptr, nullptr) {}

MockCrxInstaller::~MockCrxInstaller() = default;

}  // namespace extensions
