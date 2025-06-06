// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DEVTOOLS_PROTOCOL_STORAGE_HANDLER_H_
#define CHROME_BROWSER_DEVTOOLS_PROTOCOL_STORAGE_HANDLER_H_

#include "base/memory/weak_ptr.h"
#include "chrome/browser/devtools/protocol/protocol.h"
#include "chrome/browser/devtools/protocol/storage.h"

namespace content {
class WebContents;
}  // namespace content

class StorageHandler : public protocol::Storage::Backend {
 public:
  StorageHandler(content::WebContents* web_contents,
                 protocol::UberDispatcher* dispatcher);

  StorageHandler(const StorageHandler&) = delete;
  StorageHandler& operator=(const StorageHandler&) = delete;

  ~StorageHandler() override;

 private:
  void RunBounceTrackingMitigations(
      std::unique_ptr<RunBounceTrackingMitigationsCallback> callback) override;

  // Returns the effective Related Website Sets in use by this profile, which
  // synchronously iterates over all the effective entries.
  void GetRelatedWebsiteSets(
      std::unique_ptr<GetRelatedWebsiteSetsCallback> callback) override;

  protocol::Response GetAffectedUrlsForThirdPartyCookieMetadata(
      const protocol::String& first_party_url,
      std::unique_ptr<protocol::Array<protocol::String>> third_party_urls,
      std::unique_ptr<protocol::Array<protocol::String>>* matched_urls)
      override;

  static void GotDeletedSites(
      std::unique_ptr<RunBounceTrackingMitigationsCallback> callback,
      const std::vector<std::string>& sites);

  base::WeakPtr<content::WebContents> web_contents_;
};

#endif  // CHROME_BROWSER_DEVTOOLS_PROTOCOL_STORAGE_HANDLER_H_
