// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_MANAGEMENT_CONSTANTS_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_MANAGEMENT_CONSTANTS_H_

#include <stddef.h>

#include <string>
#include <string_view>

#include "base/containers/fixed_flat_map.h"
#include "extensions/buildflags/buildflags.h"
#include "extensions/common/manifest.h"

static_assert(BUILDFLAG(ENABLE_EXTENSIONS_CORE));

namespace extensions {
namespace schema_constants {

extern const char kWildcard[];

extern const char kInstallationMode[];
extern const char kAllowed[];
extern const char kBlocked[];
extern const char kForceInstalled[];
extern const char kNormalInstalled[];
extern const char kRemoved[];

extern const char kBlockedPermissions[];
extern const char kAllowedPermissions[];

extern const char kPolicyBlockedHosts[];
extern const char kPolicyAllowedHosts[];
extern const size_t kMaxItemsURLPatternSet;

extern const char kUpdateUrl[];
extern const char kOverrideUpdateUrl[];
extern const char kInstallSources[];
extern const char kAllowedTypes[];

extern const char kMinimumVersionRequired[];

extern const char kUpdateUrlPrefix[];

extern const char kToolbarPin[];
extern const char kForcePinned[];
extern const char kDefaultUnpinned[];

extern const char kFileUrlNavigationAllowed[];

// If the install of an extension is blocked this admin defined message is
// appended to the error message displayed in the Chrome Webstore.
extern const char kBlockedInstallMessage[];

inline constexpr auto kAllowedTypesMap =
    base::MakeFixedFlatMap<std::string_view, Manifest::Type>({
        {"extension", Manifest::TYPE_EXTENSION},
        {"theme", Manifest::TYPE_THEME},
        {"user_script", Manifest::TYPE_USER_SCRIPT},
        {"hosted_app", Manifest::TYPE_HOSTED_APP},
        {"legacy_packaged_app", Manifest::TYPE_LEGACY_PACKAGED_APP},
        {"platform_app", Manifest::TYPE_PLATFORM_APP},
        {"chromeos_system_extension", Manifest::TYPE_CHROMEOS_SYSTEM_EXTENSION},
    });

// Helper function over `kAllowedTypesMap`, returns Manifest::TYPE_UNKNOWN if
// not found.
Manifest::Type GetManifestType(const std::string& name);

}  // namespace schema_constants
}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_MANAGEMENT_CONSTANTS_H_
