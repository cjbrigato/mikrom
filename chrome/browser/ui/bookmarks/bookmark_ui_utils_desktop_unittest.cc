// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils_desktop.h"
#include "chrome/common/webui_url_constants.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/common/bookmark_metrics.h"
#include "components/bookmarks/test/test_bookmark_client.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::ASCIIToUTF16;
using bookmarks::BookmarkModel;
using bookmarks::BookmarkNode;

namespace {

class BookmarkUIUtilsTest : public testing::Test {
  content::BrowserTaskEnvironment task_environment_;
};

TEST_F(BookmarkUIUtilsTest, HasBookmarkURLs) {
  std::unique_ptr<BookmarkModel> model(
      bookmarks::TestBookmarkClient::CreateModel());

  std::vector<raw_ptr<const BookmarkNode, VectorExperimental>> nodes;

  // This tests that |nodes| contains an URL.
  const BookmarkNode* page1 = model->AddURL(
      model->bookmark_bar_node(), 0, u"Google", GURL("http://google.com"));
  nodes.push_back(page1);
  EXPECT_TRUE(bookmarks::HasBookmarkURLs(nodes));

  nodes.clear();

  // This tests that |nodes| does not contain any URL.
  const BookmarkNode* folder1 =
      model->AddFolder(model->bookmark_bar_node(), 0, u"Folder1");
  nodes.push_back(folder1);
  EXPECT_FALSE(bookmarks::HasBookmarkURLs(nodes));

  // This verifies if HasBookmarkURLs iterates through immediate children.
  model->AddURL(folder1, 0, u"Foo", GURL("http://randomsite.com"));
  EXPECT_TRUE(bookmarks::HasBookmarkURLs(nodes));

  // This verifies that HasBookmarkURLS does not iterate through descendants.
  // i.e, it should not find an URL inside a two or three level hierarchy.
  // So we add another folder to |folder1| and add another page to that new
  // folder to create a two level hierarchy.

  // But first we have to remove the URL from |folder1|.
  model->Remove(folder1->children().front().get(),
                bookmarks::metrics::BookmarkEditSource::kOther, FROM_HERE);

  const BookmarkNode* subfolder1 = model->AddFolder(folder1, 0, u"Subfolder1");

  // Now add the URL to that |subfolder1|.
  model->AddURL(subfolder1, 0, u"BAR", GURL("http://bar-foo.com"));
  EXPECT_FALSE(bookmarks::HasBookmarkURLs(nodes));
}

TEST_F(BookmarkUIUtilsTest, HasBookmarkURLsAllowedInIncognitoMode) {
  std::unique_ptr<BookmarkModel> model(
      bookmarks::TestBookmarkClient::CreateModel());
  std::vector<raw_ptr<const BookmarkNode, VectorExperimental>> nodes;
  const GURL history_page = GURL(chrome::kChromeUIHistoryURL);

  // This tests that |nodes| contains an disabled-in-incognito URL.
  const BookmarkNode* page1 =
      model->AddURL(model->bookmark_bar_node(), 0, u"History", history_page);
  nodes.push_back(page1);
  EXPECT_FALSE(bookmarks::HasBookmarkURLsAllowedInIncognitoMode(nodes));
  nodes.clear();

  // This tests that |nodes| contains an URL that can be opened in incognito
  // mode.
  const BookmarkNode* page2 = model->AddURL(
      model->bookmark_bar_node(), 0, u"Google", GURL("http://google.com"));
  nodes.push_back(page2);
  EXPECT_TRUE(bookmarks::HasBookmarkURLsAllowedInIncognitoMode(nodes));

  nodes.clear();

  // This tests that |nodes| does not contain any URL.
  const BookmarkNode* folder1 =
      model->AddFolder(model->bookmark_bar_node(), 0, u"Folder1");
  nodes.push_back(folder1);
  EXPECT_FALSE(bookmarks::HasBookmarkURLsAllowedInIncognitoMode(nodes));

  // This verifies if HasBookmarkURLsAllowedInIncognitoMode iterates through
  // immediate children.
  // Add disabled-in-incognito url.
  model->AddURL(folder1, 0, u"Foo", history_page);
  EXPECT_FALSE(bookmarks::HasBookmarkURLsAllowedInIncognitoMode(nodes));
  // Add normal url.
  model->AddURL(folder1, 0, u"Foo", GURL("http://randomsite.com"));
  EXPECT_TRUE(bookmarks::HasBookmarkURLsAllowedInIncognitoMode(nodes));

  // This verifies that HasBookmarkURLsAllowedInIncognitoMode does not iterate
  // through descendants.
  // i.e, it should not find an URL inside a two or three level hierarchy.
  // So we add another folder to |folder1| and add another page to that new
  // folder to create a two level hierarchy.

  // But first we have to remove the URL from |folder1|.
  model->Remove(folder1->children().front().get(),
                bookmarks::metrics::BookmarkEditSource::kOther, FROM_HERE);

  const BookmarkNode* subfolder1 = model->AddFolder(folder1, 0, u"Subfolder1");

  // Now add the URL to that |subfolder1|.
  model->AddURL(subfolder1, 0, u"BAR", GURL("http://bar-foo.com"));
  EXPECT_FALSE(bookmarks::HasBookmarkURLsAllowedInIncognitoMode(nodes));
}

}  // namespace
