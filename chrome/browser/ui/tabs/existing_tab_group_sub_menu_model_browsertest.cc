// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/tabs/existing_tab_group_sub_menu_model.h"

#include <memory>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_tab_menu_model_delegate.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/test/browser_test.h"
#include "url/gurl.h"

namespace {
std::unique_ptr<TabMenuModelDelegate> CreateTabMenuModelDelegate(
    Browser* browser) {
  return std::make_unique<chrome::BrowserTabMenuModelDelegate>(
      browser->session_id(), browser->profile(), browser->app_controller());
}
}  // namespace

class ExistingTabGroupSubMenuModelTest : public InProcessBrowserTest {
 public:
  ExistingTabGroupSubMenuModelTest() = default;
};

// Ensure that add to group submenu only appears when there is another group to
// move the tab into.
IN_PROC_BROWSER_TEST_F(ExistingTabGroupSubMenuModelTest, ShouldShowSubmenu) {
  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);

  TabStripModel* model = browser()->tab_strip_model();
  ASSERT_EQ(model->group_model()->ListTabGroups().size(), 0U);
  model->AddToNewGroup({0});
  ASSERT_EQ(model->group_model()->ListTabGroups().size(), 1U);
  ASSERT_TRUE(model->GetTabGroupForTab(0).has_value());
  ASSERT_FALSE(model->GetTabGroupForTab(1).has_value());
  ASSERT_EQ(model->count(), 2);

  EXPECT_FALSE(
      ExistingTabGroupSubMenuModel::ShouldShowSubmenu(model, 0, nullptr));
  EXPECT_TRUE(
      ExistingTabGroupSubMenuModel::ShouldShowSubmenu(model, 1, nullptr));
}

// Validate that the submenu has the correct items.
IN_PROC_BROWSER_TEST_F(ExistingTabGroupSubMenuModelTest, BuildSubmenuItems) {
  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);

  TabStripModel* model = browser()->tab_strip_model();
  model->AddToNewGroup({0});
  model->AddToNewGroup({1});
  ASSERT_EQ(model->group_model()->ListTabGroups().size(), 2U);
  ASSERT_TRUE(model->GetTabGroupForTab(0).has_value());
  ASSERT_TRUE(model->GetTabGroupForTab(1).has_value());
  ASSERT_FALSE(model->GetTabGroupForTab(2).has_value());
  ASSERT_EQ(model->count(), 3);

  ExistingTabGroupSubMenuModel menu1(nullptr, nullptr, model, 0);
  EXPECT_EQ(3u, menu1.GetItemCount());

  ExistingTabGroupSubMenuModel menu2(nullptr, nullptr, model, 1);
  EXPECT_EQ(3u, menu2.GetItemCount());

  ExistingTabGroupSubMenuModel menu3(nullptr, nullptr, model, 2);
  EXPECT_EQ(4u, menu3.GetItemCount());
}

// Verify tabs can be added tab groups in the same window.
IN_PROC_BROWSER_TEST_F(ExistingTabGroupSubMenuModelTest,
                       AddTabsToGroupSameWindow) {
  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);

  TabStripModel* model = browser()->tab_strip_model();
  model->AddToNewGroup({0});
  model->AddToNewGroup({1});
  ExistingTabGroupSubMenuModel menu(nullptr, nullptr, model, 2);
  EXPECT_EQ(4u, menu.GetItemCount());

  // Move the tab at index 2 into the group with the tab at index 0.
  menu.ExecuteExistingCommandForTesting(0);

  ASSERT_EQ(size_t(2), model->group_model()->ListTabGroups().size());
  EXPECT_EQ(2, model->group_model()
                   ->GetTabGroup(model->group_model()->ListTabGroups()[0])
                   ->tab_count());

  ExistingTabGroupSubMenuModel menu2(nullptr, nullptr, model, 2);
  EXPECT_EQ(3u, menu2.GetItemCount());
}

// Verify non-selected tabs can be added tab groups in the same window.
IN_PROC_BROWSER_TEST_F(ExistingTabGroupSubMenuModelTest,
                       AddNonSelectedTabsToTabGroup) {
  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);

  TabStripModel* model = browser()->tab_strip_model();
  model->AddToNewGroup({0});
  model->AddToNewGroup({1});

  // Select the tab at index 2.
  model->SelectTabAt(2);

  // Create the menu on the tab at index 3.
  ExistingTabGroupSubMenuModel menu(nullptr, nullptr, model, 3);
  EXPECT_EQ(4u, menu.GetItemCount());

  // Move the tab at index 2 into the group with the tab at index 0.
  menu.ExecuteExistingCommandForTesting(0);

  ASSERT_EQ(size_t(2), model->group_model()->ListTabGroups().size());
  EXPECT_EQ(2, model->group_model()
                   ->GetTabGroup(model->group_model()->ListTabGroups()[0])
                   ->tab_count());

  ExistingTabGroupSubMenuModel menu2(nullptr, nullptr, model, 2);
  ExistingTabGroupSubMenuModel menu3(nullptr, nullptr, model, 3);

  EXPECT_EQ(3u, menu2.GetItemCount());
  EXPECT_EQ(4u, menu3.GetItemCount());
}

// Verify tabs can be added to tab groups in other browser windows.
IN_PROC_BROWSER_TEST_F(ExistingTabGroupSubMenuModelTest,
                       AddAllSelectedTabsToAnotherWindow) {
  Browser* new_browser = Browser::Create(
      Browser::CreateParams(Browser::TYPE_NORMAL, browser()->profile(), true));

  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);

  chrome::AddTabAt(new_browser, GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(new_browser, GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(new_browser, GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(new_browser, GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);

  TabStripModel* model_1 = new_browser->tab_strip_model();
  TabStripModel* model_2 = browser()->tab_strip_model();

  EXPECT_EQ(model_1->count(), 4);
  EXPECT_EQ(model_2->count(), 4);

  std::unique_ptr<TabMenuModelDelegate> delegate_1 =
      CreateTabMenuModelDelegate(new_browser);

  // First tabs of each model consists of a tab group.
  model_1->AddToNewGroup({0});
  model_2->AddToNewGroup({0});

  ExistingTabGroupSubMenuModel menu_1(nullptr, delegate_1.get(), model_1, 1);

  // In order to move the 3 un-grouped tabs in `model_1` we must select those
  // tabs in addition to unselecting the grouped tab.
  for (int i = 1; i < model_1->count(); ++i) {
    model_1->SelectTabAt(i);
  }
  model_1->DeselectTabAt(0);

  const ui::ListSelectionModel::SelectedIndices selection_indices =
      model_1->selection_model().selected_indices();
  std::vector<int> selected_indices =
      std::vector<int>(selection_indices.begin(), selection_indices.end());
  EXPECT_EQ(selected_indices.size(), size_t(3));
  EXPECT_EQ(4u, menu_1.GetItemCount());

  // Move the 3 selected indices in model_1 to model_2.
  menu_1.ExecuteExistingCommandForTesting(1);
  EXPECT_EQ(model_2->count(), 7);

  // Verify the tab group in model_2 now has 4 tabs in it.
  TabGroup* group = model_2->group_model()->GetTabGroup(
      model_2->GetTabGroupForTab(0).value());
  EXPECT_EQ(group->tab_count(), 4);

  int num_selected = 0;

  for (int i = 0; i < model_2->count(); ++i) {
    if (model_2->IsTabSelected(i)) {
      ++num_selected;
    }
  }

  // Expect the number of tabs we moved from model_1 into model_2 is still 3.
  EXPECT_EQ(num_selected, 3);

  CloseBrowserSynchronously(new_browser);
}

IN_PROC_BROWSER_TEST_F(ExistingTabGroupSubMenuModelTest,
                       ShouldShowExistingTabGroups) {
  Browser* new_browser = Browser::Create(
      Browser::CreateParams(Browser::TYPE_NORMAL, browser()->profile(), true));

  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(new_browser, GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);

  TabStripModel* model_1 = browser()->tab_strip_model();
  TabStripModel* model_2 = new_browser->tab_strip_model();

  EXPECT_EQ(model_1->count(), 2);
  EXPECT_EQ(model_2->count(), 1);

  ASSERT_EQ(model_1->group_model()->ListTabGroups().size(), 0U);
  ASSERT_EQ(model_2->group_model()->ListTabGroups().size(), 0U);

  // create tab group in first browser window
  model_1->AddToNewGroup({0});

  ASSERT_EQ(model_1->group_model()->ListTabGroups().size(), 1U);
  ASSERT_EQ(model_2->group_model()->ListTabGroups().size(), 0U);

  std::unique_ptr<TabMenuModelDelegate> delegate_1 =
      CreateTabMenuModelDelegate(new_browser);

  ExistingTabGroupSubMenuModel menu_1(nullptr, delegate_1.get(), model_1, 1);
  ExistingTabGroupSubMenuModel menu_2(nullptr, delegate_1.get(), model_2, 0);

  EXPECT_EQ(3u, menu_1.GetItemCount());
  EXPECT_EQ(3u, menu_2.GetItemCount());

  EXPECT_FALSE(ExistingTabGroupSubMenuModel::ShouldShowSubmenu(
      model_1, 0, delegate_1.get()));
  EXPECT_TRUE(ExistingTabGroupSubMenuModel::ShouldShowSubmenu(
      model_1, 1, delegate_1.get()));
  EXPECT_TRUE(ExistingTabGroupSubMenuModel::ShouldShowSubmenu(
      model_2, 0, delegate_1.get()));

  CloseBrowserSynchronously(new_browser);
}

// Verify tab groups are display in the order they were created
IN_PROC_BROWSER_TEST_F(ExistingTabGroupSubMenuModelTest,
                       ShowTabGroupsInTheOrderTheyWereAdded) {
  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);

  TabStripModel* model = browser()->tab_strip_model();
  std::vector<tab_groups::TabGroupId> group_ids;

  group_ids.emplace_back(model->AddToNewGroup({0}));
  group_ids.emplace_back(model->AddToNewGroup({1}));
  group_ids.emplace_back(model->AddToNewGroup({2}));
  group_ids.emplace_back(model->AddToNewGroup({3}));

  ASSERT_EQ(model->group_model()->ListTabGroups().size(), size_t(4u));

  ASSERT_EQ(model->group_model()->ListTabGroups(), group_ids);
}
// Verify that pinned tabs added to a group in another window maintain
// their selection state and are inserted in the correct position.
IN_PROC_BROWSER_TEST_F(ExistingTabGroupSubMenuModelTest,
                       AddPinnedTabsToTabGroup) {
  // Window 1: 3 tabs, the first two in a group.
  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(browser(), GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);

  TabStripModel* model_1 = browser()->tab_strip_model();
  model_1->AddToNewGroup({0, 1});
  EXPECT_EQ(model_1->count(), 3);

  // Window 2: 5 tabs, 3 pinned; tabs 0, 2, and 4 are selected.
  Browser* browser_2 = Browser::Create(
      Browser::CreateParams(Browser::TYPE_NORMAL, browser()->profile(), true));

  chrome::AddTabAt(browser_2, GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(browser_2, GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(browser_2, GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(browser_2, GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);
  chrome::AddTabAt(browser_2, GURL("chrome://newtab"), /*index=*/-1,
                   /*foreground=*/true);

  TabStripModel* model_2 = browser_2->tab_strip_model();
  // Pin all tabs
  model_2->SetTabPinned(0, true);
  model_2->SetTabPinned(1, true);
  model_2->SetTabPinned(2, true);
  model_2->SetTabPinned(3, true);
  model_2->SetTabPinned(4, true);
  // Select the tabs 0, 2, 4
  model_2->SelectTabAt(0);
  model_2->SelectTabAt(2);
  model_2->SelectTabAt(4);
  EXPECT_EQ(model_2->count(), 5);

  std::unique_ptr<TabMenuModelDelegate> delegate_1 =
      CreateTabMenuModelDelegate(browser_2);
  ExistingTabGroupSubMenuModel menu_1(nullptr, delegate_1.get(), model_2, 0);

  // Move the selected tabs from Window 2 to the group in Window 1.
  menu_1.ExecuteExistingCommandForTesting(0);

  // Window 1 should now have 6 tabs.
  EXPECT_EQ(6, model_1->count());
  // Window 2 should now have 2 tabs.
  EXPECT_EQ(2, model_2->count());
  // Verify the selection state of the moved tabs.
  EXPECT_TRUE(model_1->IsTabSelected(2));
  EXPECT_TRUE(model_1->IsTabSelected(3));
  EXPECT_TRUE(model_1->IsTabSelected(4));

  CloseBrowserSynchronously(browser_2);
}
