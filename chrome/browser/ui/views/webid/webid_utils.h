// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_WEBID_WEBID_UTILS_H_
#define CHROME_BROWSER_UI_VIEWS_WEBID_WEBID_UTILS_H_

#include <string>

#include "third_party/blink/public/mojom/webid/federated_auth_request.mojom.h"
#include "ui/views/widget/widget.h"

namespace content {
struct RelyingPartyData;
}  // namespace content

namespace webid {
int SelectSingleIdpTitleResourceId(blink::mojom::RpContext rp_context);
// Returns the title to be shown in the dialog. This does not include the
// subtitle. For screen reader purposes, GetAccessibleTitle() is used instead.
std::u16string GetTitle(const content::RelyingPartyData& rp_data,
                        const std::optional<std::u16string>& idp_title,
                        blink::mojom::RpContext rp_context);
std::u16string GetSubtitle(const content::RelyingPartyData& rp_data);

// Sends an accessibility event to make an announcement of the passed in
// `announcement` if available, otherwise the text in the currently focused
// view is announced.
void SendAccessibilityEvent(views::Widget* widget,
                            std::u16string announcement = std::u16string());
}  // namespace webid

#endif  // CHROME_BROWSER_UI_VIEWS_WEBID_WEBID_UTILS_H_
