// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_ERROR_UI_DESKTOP_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_ERROR_UI_DESKTOP_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/extensions/extension_error_ui.h"
#include "chrome/browser/ui/global_error/global_error.h"

class Browser;
class Profile;

namespace extensions {

class ManagementPolicy;
class ExtensionGlobalError;

class ExtensionErrorUIDesktop : public ExtensionErrorUI {
 public:
  explicit ExtensionErrorUIDesktop(ExtensionErrorUI::Delegate* delegate);

  ExtensionErrorUIDesktop(const ExtensionErrorUIDesktop&) = delete;
  ExtensionErrorUIDesktop& operator=(const ExtensionErrorUIDesktop&) = delete;

  ~ExtensionErrorUIDesktop() override;

  bool ShowErrorInBubbleView() override;
  void ShowExtensions() override;
  void Close() override;

  GlobalErrorWithStandardBubble* GetErrorForTesting();
  void SetManagementPolicyForTesting(ManagementPolicy* management_policy);

 private:
  // The profile associated with this error.
  raw_ptr<Profile> profile_ = nullptr;

  // The browser the bubble view was shown into.
  raw_ptr<Browser> browser_ = nullptr;

  std::unique_ptr<ExtensionGlobalError> global_error_;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_ERROR_UI_DESKTOP_H_
