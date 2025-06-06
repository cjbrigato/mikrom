// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_IMPORTER_IMPORTER_BRIDGE_H_
#define CHROME_COMMON_IMPORTER_IMPORTER_BRIDGE_H_

#include <string>
#include <vector>

#include "base/memory/ref_counted.h"
#include "build/build_config.h"
#include "components/favicon_base/favicon_usage_data.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "components/user_data_importer/common/importer_url_row.h"

class GURL;
struct ImporterAutofillFormDataEntry;

namespace user_data_importer {
struct ImportedBookmarkEntry;
}  // namespace user_data_importer

class ImporterBridge : public base::RefCountedThreadSafe<ImporterBridge> {
 public:
  ImporterBridge();

  ImporterBridge(const ImporterBridge&) = delete;
  ImporterBridge& operator=(const ImporterBridge&) = delete;

  virtual void AddBookmarks(
      const std::vector<user_data_importer::ImportedBookmarkEntry>& bookmarks,
      const std::u16string& first_folder_name) = 0;

  virtual void AddHomePage(const GURL& home_page) = 0;

  virtual void SetFavicons(
      const favicon_base::FaviconUsageDataList& favicons) = 0;

  virtual void SetHistoryItems(
      const std::vector<user_data_importer::ImporterURLRow>& rows,
      user_data_importer::VisitSource visit_source) = 0;

  virtual void SetKeywords(
      const std::vector<user_data_importer::SearchEngineInfo>& search_engines,
      bool unique_on_host_and_path) = 0;

  virtual void SetPasswordForm(
      const user_data_importer::ImportedPasswordForm& form) = 0;

  virtual void SetAutofillFormData(
      const std::vector<ImporterAutofillFormDataEntry>& entries) = 0;

  // Notifies the coordinator that the import operation has begun.
  virtual void NotifyStarted() = 0;

  // Notifies the coordinator that the collection of data for the specified
  // item has begun.
  virtual void NotifyItemStarted(user_data_importer::ImportItem item) = 0;

  // Notifies the coordinator that the collection of data for the specified
  // item has completed.
  virtual void NotifyItemEnded(user_data_importer::ImportItem item) = 0;

  // Notifies the coordinator that the entire import operation has completed.
  virtual void NotifyEnded() = 0;

  // For InProcessImporters this calls l10n_util. For ExternalProcessImporters
  // this calls the set of strings we've ported over to the external process.
  // It's good to avoid having to create a separate ResourceBundle for the
  // external import process, since the importer only needs a few strings.
  virtual std::u16string GetLocalizedString(int message_id) = 0;

 protected:
  friend class base::RefCountedThreadSafe<ImporterBridge>;

  virtual ~ImporterBridge();
};

#endif  // CHROME_COMMON_IMPORTER_IMPORTER_BRIDGE_H_
