// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_NEW_TAB_FOOTER_FOOTER_WEB_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_NEW_TAB_FOOTER_FOOTER_WEB_VIEW_H_

#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/webview/webview.h"

class BrowserWindowInterface;
class WebUIContentsWrapper;

DECLARE_ELEMENT_IDENTIFIER_VALUE(kNtpFooterId);

namespace new_tab_footer {

// NewTabFooterWebView is used to present the WebContents of the New Tab Footer.
class NewTabFooterWebView : public views::WebView,
                            public WebUIContentsWrapper::Host {
  METADATA_HEADER(NewTabFooterWebView, views::WebView)

 public:
  explicit NewTabFooterWebView(BrowserWindowInterface* browser);
  NewTabFooterWebView(const NewTabFooterWebView&) = delete;
  NewTabFooterWebView& operator=(const NewTabFooterWebView&) = delete;
  ~NewTabFooterWebView() override;

  // WebUIContentsWrapper::Host:
  void ShowUI() override;
  void CloseUI() override;

 private:
  raw_ptr<BrowserWindowInterface> browser_;
  std::unique_ptr<WebUIContentsWrapper> contents_wrapper_;

  base::WeakPtrFactory<NewTabFooterWebView> weak_factory_{this};
};

}  // namespace new_tab_footer

#endif  // CHROME_BROWSER_UI_VIEWS_NEW_TAB_FOOTER_FOOTER_WEB_VIEW_H_
