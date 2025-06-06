// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/omnibox/clipboard_utils.h"

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "components/omnibox/browser/omnibox_text_util.h"
#include "components/omnibox/browser/omnibox_view.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/data_transfer_policy/data_transfer_endpoint.h"

bool CanGetClipboardText() {
  // Note that this is intended for pre-flighting and so therefore, in this
  // context, it's unsafe to actually try to access the clipboard (especially on
  // macOS with Pasteboard Privacy). Therefore, make reasonable assumptions
  // below (marked as such) to avoid inspecting actual clipboard data.

  // Try text format.
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  ui::DataTransferEndpoint data_dst = ui::DataTransferEndpoint(
      ui::EndpointType::kDefault, {.notify_if_restricted = false});
  if (clipboard->IsFormatAvailable(ui::ClipboardFormatType::PlainTextType(),
                                   ui::ClipboardBuffer::kCopyPaste,
                                   &data_dst)) {
    // Reasonable assumption: If there is text data on the clipboard, it's of
    // non-zero length.
    return true;
  }

  // Try bookmark format.
  if (clipboard->IsFormatAvailable(ui::ClipboardFormatType::UrlType(),
                                   ui::ClipboardBuffer::kCopyPaste,
                                   &data_dst)) {
    // Reasonable assumption: The URL is valid.
    return true;
  }

  return false;
}

std::u16string GetClipboardText(bool notify_if_restricted) {
  // Try text format.
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  ui::DataTransferEndpoint data_dst =
      ui::DataTransferEndpoint(ui::EndpointType::kDefault,
                               {.notify_if_restricted = notify_if_restricted});
  if (clipboard->IsFormatAvailable(ui::ClipboardFormatType::PlainTextType(),
                                   ui::ClipboardBuffer::kCopyPaste,
                                   &data_dst)) {
    std::u16string text;
    clipboard->ReadText(ui::ClipboardBuffer::kCopyPaste, &data_dst, &text);
    text = text.substr(0, kMaxClipboardTextLength);
    return omnibox::SanitizeTextForPaste(text);
  }

  // Try bookmark format.
  //
  // It is tempting to try bookmark format first, but the URL we get out of a
  // bookmark has been cannonicalized via GURL.  This means if a user copies
  // and pastes from the URL bar to itself, the text will get fixed up and
  // cannonicalized, which is not what the user expects.  By pasting in this
  // order, we are sure to paste what the user copied.
  if (clipboard->IsFormatAvailable(ui::ClipboardFormatType::UrlType(),
                                   ui::ClipboardBuffer::kCopyPaste,
                                   &data_dst)) {
    std::string url_str;
    clipboard->ReadBookmark(&data_dst, nullptr, &url_str);
    // pass resulting url string through GURL to normalize
    GURL url(url_str);
    if (url.is_valid()) {
      return omnibox::StripJavascriptSchemas(base::UTF8ToUTF16(url.spec()));
    }
  }

  return std::u16string();
}
