// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/offline_pages/content/background_loader/background_loader_contents.h"

#include <utility>

#include "build/build_config.h"
#include "content/public/browser/media_stream_request.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom-shared.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"

namespace background_loader {

BackgroundLoaderContents::BackgroundLoaderContents(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  // It is very important that we create the web contents with
  // CreateParams::initially_hidden == false, and that we never change the
  // visibility after that.  If we did change it, then background throttling
  // could kill the background offliner while it was running.
  content::WebContents::CreateParams create_params(browser_context_);
  // Background, so not user-visible.
  create_params.is_never_composited = true;
  web_contents_ = content::WebContents::Create(create_params);
  web_contents_->SetOwnerLocationForDebug(FROM_HERE);
  web_contents_->SetAudioMuted(true);
  web_contents_->SetDelegate(this);
}

BackgroundLoaderContents::~BackgroundLoaderContents() = default;

void BackgroundLoaderContents::LoadPage(const GURL& url) {
  web_contents_->GetController().LoadURL(
      url /* url to be loaded */,
      content::Referrer() /* Default referrer policy, no referring url */,
      ui::PAGE_TRANSITION_LINK /* page transition type: clicked on link */,
      std::string() /* extra headers */);
}

void BackgroundLoaderContents::Cancel() {
  web_contents_->Close();
}

void BackgroundLoaderContents::SetDelegate(Delegate* delegate) {
  delegate_ = delegate;
}

void BackgroundLoaderContents::CloseContents(content::WebContents* source) {
  // Do nothing. Other pages should not be able to close a background page.
  //
  // TODO(crbug.com/374382473): This used to be NOTREACHED() but is reachable as
  // of 2024-11-20. It should either be made not reachable (and the NOTREACHED()
  // added back) or document why this should be reachable (as opposed to the
  // "should not be able to close" in the comment above).
}

bool BackgroundLoaderContents::ShouldSuppressDialogs(
    content::WebContents* source) {
  // Dialog prompts are not actionable in the background.
  return true;
}

bool BackgroundLoaderContents::ShouldFocusPageAfterCrash(
    content::WebContents* source) {
  // Background page should never be focused.
  return false;
}

void BackgroundLoaderContents::CanDownload(
    const GURL& url,
    const std::string& request_method,
    base::OnceCallback<void(bool)> callback) {
  if (delegate_) {
    delegate_->CanDownload(std::move(callback));
  } else {
    // Do not download anything if there's no delegate.
    std::move(callback).Run(false);
  }
}

bool BackgroundLoaderContents::IsWebContentsCreationOverridden(
    content::RenderFrameHost* opener,
    content::SiteInstance* source_site_instance,
    content::mojom::WindowContainerType window_container_type,
    const GURL& opener_url,
    const std::string& frame_name,
    const GURL& target_url) {
  // Background pages should not create other webcontents/tabs.
  return true;
}

content::WebContents* BackgroundLoaderContents::AddNewContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents> new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture,
    bool* was_blocked) {
  // Pop-ups should be blocked;
  // background pages should not create other contents
  if (was_blocked != nullptr) {
    *was_blocked = true;
  }
  return nullptr;
}

#if BUILDFLAG(IS_ANDROID)
bool BackgroundLoaderContents::ShouldBlockMediaRequest(const GURL& url) {
  // Background pages should not have access to media.
  return true;
}
#endif

void BackgroundLoaderContents::RequestMediaAccessPermission(
    content::WebContents* contents,
    const content::MediaStreamRequest& request,
    content::MediaResponseCallback callback) {
  // No permissions granted, act as if dismissed.
  std::move(callback).Run(
      blink::mojom::StreamDevicesSet(),
      blink::mojom::MediaStreamRequestResult::PERMISSION_DISMISSED,
      std::unique_ptr<content::MediaStreamUI>());
}

bool BackgroundLoaderContents::CheckMediaAccessPermission(
    content::RenderFrameHost* render_frame_host,
    const url::Origin& security_origin,
    blink::mojom::MediaStreamType type) {
  return false;  // No permissions granted.
}

bool BackgroundLoaderContents::ShouldAllowLazyLoad() {
  return false;
}

BackgroundLoaderContents::BackgroundLoaderContents()
    : browser_context_(nullptr) {
  web_contents_.reset();
}

}  // namespace background_loader
