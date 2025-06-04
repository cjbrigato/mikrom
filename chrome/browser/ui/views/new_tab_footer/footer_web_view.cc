// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/new_tab_footer/footer_web_view.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/webui/new_tab_footer/new_tab_footer_ui.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/view_class_properties.h"

DEFINE_ELEMENT_IDENTIFIER_VALUE(kNtpFooterId);

namespace new_tab_footer {

NewTabFooterWebView::NewTabFooterWebView(BrowserWindowInterface* browser)
    : views::WebView(browser->GetProfile()), browser_(browser) {
  SetProperty(views::kElementIdentifierKey, kNtpFooterId);
}

NewTabFooterWebView::~NewTabFooterWebView() {
  if (!contents_wrapper_) {
    return;
  }
  contents_wrapper_->web_contents()->WasHidden();
  contents_wrapper_->SetHost(nullptr);
  contents_wrapper_ = nullptr;
}

void NewTabFooterWebView::ShowUI() {
  if (!contents_wrapper_) {
    contents_wrapper_ = std::make_unique<WebUIContentsWrapperT<NewTabFooterUI>>(
        GURL(chrome::kChromeUINewTabFooterURL), browser_->GetProfile(),
        IDS_NEW_TAB_FOOTER_NAME,
        /*esc_closes_ui=*/false);
    contents_wrapper_->SetHost(weak_factory_.GetWeakPtr());
    SetWebContents(contents_wrapper_->web_contents());
    webui::SetBrowserWindowInterface(contents_wrapper_->web_contents(),
                                     browser_);
  }

  SetVisible(true);
  contents_wrapper_->web_contents()->WasShown();
}

void NewTabFooterWebView::CloseUI() {
  SetVisible(false);
  if (contents_wrapper_) {
    contents_wrapper_->web_contents()->WasHidden();
  }
}

BEGIN_METADATA(NewTabFooterWebView)
END_METADATA

}  // namespace new_tab_footer
