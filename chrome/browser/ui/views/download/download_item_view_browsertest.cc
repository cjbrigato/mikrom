// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/download/download_item_view.h"

#include "chrome/browser/download/download_item_model.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/download/download_shelf_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/download/public/common/mock_download_item.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/fake_download_item.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace {

using download::DownloadItem;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRefOfCopy;

// Default values for the mock download item.
const base::FilePath::CharType kDefaultTargetFilePath[] =
    FILE_PATH_LITERAL("/foo/bar/foo.bar");
const char kDefaultURL[] = "http://example.com/foo.bar";

class DownloadItemViewTest : public InProcessBrowserTest {
 public:
  DownloadItemViewTest() = default;
  ~DownloadItemViewTest() override = default;

  DownloadItemViewTest(const DownloadItemViewTest&) = delete;
  DownloadItemViewTest& operator=(const DownloadItemViewTest&) = delete;

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();

    mock_item_ = std::make_unique<NiceMock<download::MockDownloadItem>>();
    ON_CALL(*mock_item_, GetState())
        .WillByDefault(Return(DownloadItem::IN_PROGRESS));
    ON_CALL(*mock_item_, GetURL())
        .WillByDefault(ReturnRefOfCopy(GURL(kDefaultURL)));
    ON_CALL(*mock_item_, GetTargetFilePath())
        .WillByDefault(ReturnRefOfCopy(base::FilePath(kDefaultTargetFilePath)));
    content::DownloadItemUtils::AttachInfoForTesting(
        mock_item_.get(), browser()->profile(), nullptr);

    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(browser());
    shelf_view_ =
        std::make_unique<DownloadShelfView>(browser(), browser_view);
    browser_view->SetDownloadShelfForTest(shelf_view_.get());
    browser_view->GetDownloadShelf()->AddDownload(
        DownloadItemModel::Wrap(mock_item_.get()));
  }

  void TearDownOnMainThread() override {
    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(browser());
    // It's possible the browser or its window is already being torn down.
    // We check browser_view before dereferencing it.
    if (browser_view) {
      browser_view->SetDownloadShelfForTest(nullptr);
    }
    shelf_view_.reset();
    mock_item_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  DownloadItemView* GetDownloadItemView() {
    return GetDownloadShelfView()->GetViewOfLastDownloadItemForTesting();
  }

  DownloadShelfView* GetDownloadShelfView() {
    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(browser());
    return static_cast<DownloadShelfView*>(browser_view->GetDownloadShelf());
  }

 private:
  std::unique_ptr<DownloadShelfView> shelf_view_;
  std::unique_ptr<NiceMock<download::MockDownloadItem>> mock_item_;
};

IN_PROC_BROWSER_TEST_F(DownloadItemViewTest, AccessibleProperties) {
  auto* item_view = GetDownloadItemView();
  ui::AXNodeData data;

  ASSERT_TRUE(item_view);
  item_view->GetViewAccessibility().GetAccessibleNodeData(&data);
  EXPECT_EQ(data.role, ax::mojom::Role::kGroup);
  EXPECT_EQ(data.GetString16Attribute(ax::mojom::StringAttribute::kName),
            item_view->GetStatusTextForTesting() + u" ");
}

}  // namespace
