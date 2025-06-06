// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/shell_integration.h"

#include <AppKit/AppKit.h>
#include <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "base/apple/bridging.h"
#include "base/apple/bundle_locations.h"
#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/mac/mac_util.h"
#include "base/strings/sys_string_conversions.h"
#include "build/branding_buildflags.h"
#include "chrome/common/channel_info.h"
#include "components/version_info/version_info.h"
#import "net/base/apple/url_conversions.h"

namespace shell_integration {

namespace {

// Returns the bundle id of the default client application for the given
// scheme or nil on failure.
NSString* GetBundleIdForDefaultAppForScheme(NSString* scheme) {
  NSURL* scheme_url =
      [NSURL URLWithString:[scheme stringByAppendingString:@":"]];

  NSURL* default_app_url =
      [NSWorkspace.sharedWorkspace URLForApplicationToOpenURL:scheme_url];
  if (!default_app_url) {
    return nil;
  }

  NSBundle* default_app_bundle = [NSBundle bundleWithURL:default_app_url];
  return default_app_bundle.bundleIdentifier;
}

// Returns the bundle ID of the default client application for `type`, or nil on
// failure.
NSString* GetBundleIdForDefaultAppForUTType(NSString* type) {
  UTType* uttype = [UTType typeWithIdentifier:type];
  if (!uttype) {
    return nil;
  }
  NSURL* default_app_url =
      [NSWorkspace.sharedWorkspace URLForApplicationToOpenContentType:uttype];
  if (!default_app_url) {
    return nil;
  }
  return [NSBundle bundleWithURL:default_app_url].bundleIdentifier;
}

}  // namespace

bool SetAsDefaultBrowser() {
  // We really do want the outer bundle here, not the main bundle since
  // setting a shortcut to Chrome as the default browser doesn't make sense.
  NSURL* app_bundle = base::apple::OuterBundleURL();
  if (!app_bundle) {
    return false;
  }

  [NSWorkspace.sharedWorkspace setDefaultApplicationAtURL:app_bundle
                                     toOpenURLsWithScheme:@"http"
                                        completionHandler:^(NSError*){
                                        }];
  [NSWorkspace.sharedWorkspace setDefaultApplicationAtURL:app_bundle
                                     toOpenURLsWithScheme:@"https"
                                        completionHandler:^(NSError*){
                                        }];
  [NSWorkspace.sharedWorkspace setDefaultApplicationAtURL:app_bundle
                                        toOpenContentType:UTTypeHTML
                                        completionHandler:^(NSError*){
                                        }];
  // TODO(https://crbug.com/40248220): Passing empty completion handlers,
  // above, is kinda broken, but given that this API is synchronous, nothing
  // better can be done. This entire API should be rebuilt.

  // The CoreServicesUIAgent presents a dialog asking the user to confirm their
  // new default browser choice, but the agent sometimes orders the dialog
  // behind the Chrome window. The user never sees the dialog, and therefore
  // never confirms the change. Make the CoreServicesUIAgent active so the
  // confirmation dialog comes to the front.
  NSString* const kCoreServicesUIAgentBundleID =
      @"com.apple.coreservices.uiagent";

  for (NSRunningApplication* application in NSWorkspace.sharedWorkspace
           .runningApplications) {
    if ([application.bundleIdentifier
            isEqualToString:kCoreServicesUIAgentBundleID]) {
      [application activateWithOptions:NSApplicationActivateAllWindows];
      break;
    }
  }

  return true;
}

bool SetAsDefaultClientForScheme(const std::string& scheme) {
  if (scheme.empty()) {
    return false;
  }

  if (GetDefaultSchemeClientSetPermission() != SET_DEFAULT_UNATTENDED) {
    return false;
  }

  // We really do want the main bundle here since it makes sense to set an
  // app shortcut as a default scheme handler.
  NSURL* app_bundle = base::apple::MainBundleURL();
  if (!app_bundle) {
    return false;
  }

  [NSWorkspace.sharedWorkspace
      setDefaultApplicationAtURL:app_bundle
            toOpenURLsWithScheme:base::SysUTF8ToNSString(scheme)
               completionHandler:^(NSError*){
               }];

  // TODO(https://crbug.com/40248220): Passing empty completion handlers,
  // above, is kinda broken, but given that this API is synchronous, nothing
  // better can be done. This entire API should be rebuilt.
  return true;
}

bool SetAsDefaultHandlerForUTType(const std::string& type) {
  if (type.empty()) {
    return false;
  }
  UTType* uttype = [UTType typeWithIdentifier:base::SysUTF8ToNSString(type)];
  if (!uttype) {
    return false;
  }
  NSURL* app_bundle = base::apple::OuterBundleURL();
  if (!app_bundle) {
    return false;
  }
  [NSWorkspace.sharedWorkspace setDefaultApplicationAtURL:app_bundle
                                        toOpenContentType:uttype
                                        completionHandler:^(NSError*){
                                        }];
  return true;
}

std::u16string GetApplicationNameForScheme(const GURL& url) {
  NSURL* ns_url = net::NSURLWithGURL(url);
  if (!ns_url) {
    return {};
  }

  NSURL* app_url =
      [NSWorkspace.sharedWorkspace URLForApplicationToOpenURL:ns_url];
  if (!app_url) {
    return std::u16string();
  }

  NSString* app_display_name =
      [NSFileManager.defaultManager displayNameAtPath:app_url.path];
  return base::SysNSStringToUTF16(app_display_name);
}

std::vector<base::FilePath> GetAllApplicationPathsForURL(const GURL& url) {
  NSURL* ns_url = net::NSURLWithGURL(url);
  if (!ns_url) {
    return {};
  }

  NSArray* app_urls =
      [NSWorkspace.sharedWorkspace URLsForApplicationsToOpenURL:ns_url];
  if (app_urls.count == 0) {
    return {};
  }

  std::vector<base::FilePath> app_paths;
  app_paths.reserve(app_urls.count);
  for (NSURL* app_url in app_urls) {
    app_paths.push_back(base::apple::NSURLToFilePath(app_url));
  }
  return app_paths;
}

bool CanApplicationHandleURL(const base::FilePath& app_path, const GURL& url) {
  NSURL* ns_item_url = net::NSURLWithGURL(url);
  NSURL* ns_app_url = base::apple::FilePathToNSURL(app_path);
  Boolean result = FALSE;
  LSCanURLAcceptURL(base::apple::NSToCFPtrCast(ns_item_url),
                    base::apple::NSToCFPtrCast(ns_app_url), kLSRolesAll,
                    kLSAcceptDefault, &result);
  return result;
}

#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
// Returns true if `other_identifier` is another instance (or channel) of
// Chrome, given Chrome bundle ID `chrome_identifier`.
bool IsAnotherChromeChannel(NSString* chrome_identifier,
                            NSString* other_identifier) {
  // Flavors of Chrome are of the constructions "com.google.Chrome" and
  // "com.google.Chrome.beta". If the first three components match, then these
  // are variant flavors.
  auto three_components_only_lopper = [](NSString* bundle_id) {
    NSMutableArray<NSString*>* parts =
        [[bundle_id componentsSeparatedByString:@"."] mutableCopy];
    while (parts.count > 3) {
      [parts removeLastObject];
    }
    return [parts componentsJoinedByString:@"."];
  };
  NSString* chrome_identifier_lopped =
      three_components_only_lopper(chrome_identifier);
  NSString* other_identifier_lopped =
      three_components_only_lopper(other_identifier);
  return [chrome_identifier_lopped isEqualToString:other_identifier_lopped];
}
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING)

// Attempt to determine if this instance of Chrome is the default browser and
// return the appropriate state. (Defined as being the handler for HTTP/HTTPS
// schemes; we don't want to report "no" here if the user has simply chosen
// to open HTML files in a text editor and FTP links with an FTP client.)
DefaultWebClientState GetDefaultBrowser() {
  // We really do want the outer bundle here, since this we want to know the
  // status of the main Chrome bundle and not a shortcut.
  NSString* my_identifier = base::apple::OuterBundle().bundleIdentifier;
  if (!my_identifier) {
    return UNKNOWN_DEFAULT;
  }

  NSString* default_browser = GetBundleIdForDefaultAppForScheme(@"http");
  if ([default_browser isEqualToString:my_identifier]) {
    return IS_DEFAULT;
  }

#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
  if (IsAnotherChromeChannel(my_identifier, default_browser)) {
    return OTHER_MODE_IS_DEFAULT;
  }
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING)
  return NOT_DEFAULT;
}

// Returns true if Firefox is the default browser for the current user.
bool IsFirefoxDefaultBrowser() {
  return [GetBundleIdForDefaultAppForScheme(@"http")
      isEqualToString:@"org.mozilla.firefox"];
}

// Attempt to determine if this instance of Chrome is the default client
// application for the given scheme and return the appropriate state.
DefaultWebClientState IsDefaultClientForScheme(const std::string& scheme) {
  if (scheme.empty()) {
    return UNKNOWN_DEFAULT;
  }

  // We really do want the main bundle here since it makes sense to set an
  // app shortcut as a default scheme handler.
  NSString* my_identifier = base::apple::MainBundle().bundleIdentifier;
  if (!my_identifier) {
    return UNKNOWN_DEFAULT;
  }

  NSString* default_browser =
      GetBundleIdForDefaultAppForScheme(base::SysUTF8ToNSString(scheme));
  return [default_browser isEqualToString:my_identifier] ? IS_DEFAULT
                                                         : NOT_DEFAULT;
}

DefaultWebClientState IsDefaultHandlerForUTType(const std::string& type) {
  if (type.empty()) {
    return UNKNOWN_DEFAULT;
  }
  NSString* my_identifier = base::apple::OuterBundle().bundleIdentifier;
  if (!my_identifier) {
    return UNKNOWN_DEFAULT;
  }
  NSString* default_app =
      GetBundleIdForDefaultAppForUTType(base::SysUTF8ToNSString(type));
  if (!default_app) {
    return UNKNOWN_DEFAULT;
  }
  if ([default_app isEqualToString:my_identifier]) {
    return IS_DEFAULT;
  }
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
  if (IsAnotherChromeChannel(my_identifier, default_app)) {
    return OTHER_MODE_IS_DEFAULT;
  }
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING)
  return NOT_DEFAULT;
}

namespace internal {

DefaultWebClientSetPermission GetPlatformSpecificDefaultWebClientSetPermission(
    WebClientSetMethod method) {
  // This should be `SET_DEFAULT_INTERACTIVE`, but that changes how
  // `DefaultBrowserWorker` and `DefaultSchemeClientWorker` work.
  // TODO(https://crbug.com/40248220): Migrate all callers to the new API,
  // migrate all the Mac code to integrate with it, and change this to return
  // the correct value.
  return SET_DEFAULT_UNATTENDED;
}

}  // namespace internal

}  // namespace shell_integration
