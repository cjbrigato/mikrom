// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/chromeos/features.h"

#include "base/feature_list.h"

namespace remoting::features {

BASE_FEATURE(kEnableCrdSharedSessionToUnattendedDevice,
             "EnableCrdSharedSessionToUnattendedDevice",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kAutoApproveEnterpriseSharedSessions,
             "EnableAutoApproveEnterpriseSharedSessions",
             base::FEATURE_ENABLED_BY_DEFAULT);

}  // namespace remoting::features
