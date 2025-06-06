// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_IMPORTER_MOCK_IMPORTER_BRIDGE_H_
#define CHROME_COMMON_IMPORTER_MOCK_IMPORTER_BRIDGE_H_

#include <string>
#include <vector>

#include "chrome/common/importer/importer_autofill_form_data_entry.h"
#include "chrome/common/importer/importer_bridge.h"
#include "components/user_data_importer/common/imported_bookmark_entry.h"
#include "testing/gmock/include/gmock/gmock.h"

class MockImporterBridge : public ImporterBridge {
 public:
  MockImporterBridge();

  MOCK_METHOD2(
      AddBookmarks,
      void(const std::vector<user_data_importer::ImportedBookmarkEntry>&,
           const std::u16string&));
  MOCK_METHOD1(AddHomePage, void(const GURL&));
  MOCK_METHOD1(SetFavicons, void(const favicon_base::FaviconUsageDataList&));
  MOCK_METHOD2(SetHistoryItems,
               void(const std::vector<user_data_importer::ImporterURLRow>&,
                    user_data_importer::VisitSource));
  MOCK_METHOD2(SetKeywords,
               void(const std::vector<user_data_importer::SearchEngineInfo>&,
                    bool));
  MOCK_METHOD1(SetPasswordForm,
               void(const user_data_importer::ImportedPasswordForm&));
  MOCK_METHOD1(SetAutofillFormData,
               void(const std::vector<ImporterAutofillFormDataEntry>&));
  MOCK_METHOD0(NotifyStarted, void());
  MOCK_METHOD1(NotifyItemStarted, void(user_data_importer::ImportItem));
  MOCK_METHOD1(NotifyItemEnded, void(user_data_importer::ImportItem));
  MOCK_METHOD0(NotifyEnded, void());
  MOCK_METHOD1(GetLocalizedString, std::u16string(int));

 private:
  ~MockImporterBridge() override;
};

#endif  // CHROME_COMMON_IMPORTER_MOCK_IMPORTER_BRIDGE_H_
