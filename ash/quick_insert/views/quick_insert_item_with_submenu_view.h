// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_QUICK_INSERT_VIEWS_QUICK_INSERT_ITEM_WITH_SUBMENU_VIEW_H_
#define ASH_QUICK_INSERT_VIEWS_QUICK_INSERT_ITEM_WITH_SUBMENU_VIEW_H_

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ash/ash_export.h"
#include "ash/quick_insert/quick_insert_search_result.h"
#include "ash/quick_insert/views/quick_insert_item_view.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/metadata/view_factory.h"

namespace views {
class ImageModel;
class ImageView;
class Label;
}  // namespace views

namespace ash {

// View for a Quick Insert item which has a submenu.
class ASH_EXPORT QuickInsertItemWithSubmenuView : public QuickInsertItemView {
  METADATA_HEADER(QuickInsertItemWithSubmenuView, QuickInsertItemView)

 public:
  QuickInsertItemWithSubmenuView();
  QuickInsertItemWithSubmenuView(const QuickInsertItemWithSubmenuView&) =
      delete;
  QuickInsertItemWithSubmenuView& operator=(
      const QuickInsertItemWithSubmenuView&) = delete;
  ~QuickInsertItemWithSubmenuView() override;

  void SetLeadingIcon(const ui::ImageModel& icon);

  void SetText(const std::u16string& text);

  void AddEntry(QuickInsertSearchResult item, SelectItemCallback callback);

  bool IsEmpty() const;

  void ShowSubmenu();

  // QuickInsertItemView:
  void OnMouseEntered(const ui::MouseEvent& event) override;

  std::u16string_view GetTextForTesting() const;

 private:
  raw_ptr<views::ImageView> leading_icon_view_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
  std::vector<std::pair<QuickInsertSearchResult, SelectItemCallback>> entries_;

  base::WeakPtrFactory<QuickInsertItemWithSubmenuView> weak_ptr_factory_{this};
};

BEGIN_VIEW_BUILDER(ASH_EXPORT,
                   QuickInsertItemWithSubmenuView,
                   QuickInsertItemView)
VIEW_BUILDER_PROPERTY(ui::ImageModel, LeadingIcon)
VIEW_BUILDER_PROPERTY(std::u16string, Text)
END_VIEW_BUILDER

}  // namespace ash

DEFINE_VIEW_BUILDER(ASH_EXPORT, ash::QuickInsertItemWithSubmenuView)

#endif  // ASH_QUICK_INSERT_VIEWS_QUICK_INSERT_ITEM_WITH_SUBMENU_VIEW_H_
