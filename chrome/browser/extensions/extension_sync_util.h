// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_SYNC_UTIL_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_SYNC_UTIL_H_

#include "extensions/buildflags/buildflags.h"

static_assert(BUILDFLAG(ENABLE_EXTENSIONS_CORE));

namespace content {
class BrowserContext;
}

class Profile;

namespace extensions {
class Extension;

namespace sync_util {

// Returns true if `extension` should be synced.
bool ShouldSync(content::BrowserContext* context, const Extension* extension);

// Returns if syncing is enabled for extensions for the given `profile`.
bool IsSyncingExtensionsEnabled(Profile* profile);

// Note: transport mode is defined as when user is signed in but does not have
// `kSync` consent level. This means that syncing consent is handled
// individually for different data types instead of a single consent level that
// is applied for all data types.

// Returns whether the current `profile` is in transport mode and syncing
// extensions is enabled.
bool IsSyncingExtensionsInTransportMode(Profile* profile);

}  // namespace sync_util
}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_SYNC_UTIL_H_
