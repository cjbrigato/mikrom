// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_BROWSERTEST_UTIL_H_
#define CHROME_BROWSER_EXTENSIONS_BROWSERTEST_UTIL_H_

#include <stddef.h>

#include "build/build_config.h"

class Browser;
class GURL;
class Profile;

namespace content {
class WebContents;
}

namespace extensions {

class Extension;

namespace browsertest_util {

#if BUILDFLAG(IS_CHROMEOS)
// On chromeos, the extension cache directory must be initialized before
// extensions can be installed in some situations (e.g. policy force installs
// via update urls). The chromeos device setup scripts take care of this in
// actual production devices, but some tests need to do it manually.
void CreateAndInitializeLocalCache();
#endif

// Launches a new app window for `app` in `profile`.
Browser* LaunchAppBrowser(Profile* profile, const Extension* app);

// Adds a tab to `browser` and returns the newly added WebContents.
content::WebContents* AddTab(Browser* browser, const GURL& url);

// Returns the number of WindowControllers with the Profile `profile`.
size_t GetWindowControllerCountInProfile(Profile* profile);

}  // namespace browsertest_util
}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_BROWSERTEST_UTIL_H_
