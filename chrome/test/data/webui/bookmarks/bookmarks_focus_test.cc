// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/web_ui_mocha_browser_test.h"
#include "content/public/test/browser_test.h"

class BookmarksFocusTest : public WebUIMochaFocusTest {
 protected:
  BookmarksFocusTest() { set_test_loader_host(chrome::kChromeUIBookmarksHost); }
};

IN_PROC_BROWSER_TEST_F(BookmarksFocusTest, FolderNode) {
  RunTest("bookmarks/folder_node_focus_test.js", "mocha.run()");
}

IN_PROC_BROWSER_TEST_F(BookmarksFocusTest, List) {
  RunTest("bookmarks/list_focus_test.js", "mocha.run()");
}

IN_PROC_BROWSER_TEST_F(BookmarksFocusTest, DialogFocusManager) {
  RunTest("bookmarks/dialog_focus_manager_test.js", "mocha.run()");
}

IN_PROC_BROWSER_TEST_F(BookmarksFocusTest, DNDManager) {
  RunTest("bookmarks/dnd_manager_test.js", "mocha.run()");
}
