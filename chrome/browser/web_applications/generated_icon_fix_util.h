// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WEB_APPLICATIONS_GENERATED_ICON_FIX_UTIL_H_
#define CHROME_BROWSER_WEB_APPLICATIONS_GENERATED_ICON_FIX_UTIL_H_

#include <iosfwd>

#include "base/time/time.h"
#include "chrome/browser/web_applications/proto/web_app.pb.h"
#include "components/webapps/common/web_app_id.h"

namespace base {
class Value;
}

namespace web_app {

class WebApp;
class WithAppResources;
class ScopedRegistryUpdate;

namespace generated_icon_fix_util {

// Must have window start time, attempt count and a known source.
bool IsValid(const proto::GeneratedIconFix& generated_icon_fix);

base::Value ToDebugValue(const proto::GeneratedIconFix* generated_icon_fix);

void SetNowForTesting(base::Time now);

bool HasRemainingAttempts(const WebApp& app);

// Checks if the current time is within the proto::GeneratedIconFix time window
// for `app`. If retroactive fixes are enabled then the absence of a time window
// implies it can retroactively start now.
bool IsWithinFixTimeWindow(const WebApp& app);

void EnsureFixTimeWindowStarted(WithAppResources& resources,
                                ScopedRegistryUpdate& update,
                                const webapps::AppId& app_id,
                                proto::GeneratedIconFixSource source);

proto::GeneratedIconFix CreateInitialTimeWindow(
    proto::GeneratedIconFixSource source);

base::TimeDelta GetThrottleDuration(const WebApp& app);

void RecordFixAttempt(WithAppResources& resources,
                      ScopedRegistryUpdate& update,
                      const webapps::AppId& app_id,
                      proto::GeneratedIconFixSource source);

}  // namespace generated_icon_fix_util

namespace proto {

bool operator==(const GeneratedIconFix& a, const GeneratedIconFix& b);

std::ostream& operator<<(std::ostream& out,
                         const GeneratedIconFixSource& source);

}  // namespace proto

}  // namespace web_app

#endif  // CHROME_BROWSER_WEB_APPLICATIONS_GENERATED_ICON_FIX_UTIL_H_
