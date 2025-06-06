// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_SHELL_BROWSER_SHELL_EXTENSIONS_API_CLIENT_H_
#define EXTENSIONS_SHELL_BROWSER_SHELL_EXTENSIONS_API_CLIENT_H_

#include <memory>

#include "extensions/browser/api/extensions_api_client.h"

#include "build/build_config.h"
#include "build/chromeos_buildflags.h"

namespace extensions {

class MessagingDelegate;
class VirtualKeyboardDelegate;

class ShellExtensionsAPIClient : public ExtensionsAPIClient {
 public:
  ShellExtensionsAPIClient();
  ShellExtensionsAPIClient(const ShellExtensionsAPIClient&) = delete;
  ShellExtensionsAPIClient& operator=(const ShellExtensionsAPIClient&) = delete;
  ~ShellExtensionsAPIClient() override;

  // ExtensionsAPIClient implementation.
  void AttachWebContentsHelpers(content::WebContents* web_contents) const
      override;
#if BUILDFLAG(ENABLE_GUEST_VIEW)
  std::unique_ptr<AppViewGuestDelegate> CreateAppViewGuestDelegate()
      const override;
  std::unique_ptr<guest_view::GuestViewManagerDelegate>
  CreateGuestViewManagerDelegate() const override;
  std::unique_ptr<WebViewGuestDelegate> CreateWebViewGuestDelegate(
      WebViewGuest* web_view_guest) const override;
  std::unique_ptr<WebViewPermissionHelperDelegate>
  CreateWebViewPermissionHelperDelegate(
      WebViewPermissionHelper* web_view_permission_helper) const override;
#endif  // BUILDFLAG(ENABLE_GUEST_VIEW)
  std::unique_ptr<VirtualKeyboardDelegate> CreateVirtualKeyboardDelegate(
      content::BrowserContext* browser_context) const override;
  std::unique_ptr<DisplayInfoProvider> CreateDisplayInfoProvider()
      const override;
#if BUILDFLAG(IS_LINUX)
  FileSystemDelegate* GetFileSystemDelegate() override;
#endif
  MessagingDelegate* GetMessagingDelegate() override;
  FeedbackPrivateDelegate* GetFeedbackPrivateDelegate() override;

 private:
#if BUILDFLAG(IS_LINUX)
  std::unique_ptr<FileSystemDelegate> file_system_delegate_;
#endif
  std::unique_ptr<MessagingDelegate> messaging_delegate_;
  std::unique_ptr<FeedbackPrivateDelegate> feedback_private_delegate_;
};

}  // namespace extensions

#endif  // EXTENSIONS_SHELL_BROWSER_SHELL_EXTENSIONS_API_CLIENT_H_
