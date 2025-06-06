// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/quick_insert/views/quick_insert_list_item_container_view.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>

#include "ash/quick_insert/views/quick_insert_item_view.h"
#include "ash/quick_insert/views/quick_insert_item_with_submenu_view.h"
#include "ash/quick_insert/views/quick_insert_list_item_view.h"
#include "ash/quick_insert/views/quick_insert_traversable_item_container.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace ash {
namespace {

std::unique_ptr<views::View> CreateListItemView() {
  auto view = std::make_unique<views::View>();
  view->SetUseDefaultFillLayout(true);
  view->GetViewAccessibility().SetRole(ax::mojom::Role::kListItem);
  return view;
}

}  // namespace

QuickInsertListItemContainerView::QuickInsertListItemContainerView() {
  // Lay out items as a full-width vertical list.
  SetLayoutManager(std::make_unique<views::BoxLayout>())
      ->SetOrientation(views::LayoutOrientation::kVertical);
}

QuickInsertListItemContainerView::~QuickInsertListItemContainerView() = default;

views::View* QuickInsertListItemContainerView::GetTopItem() {
  return items_.view_size() == 0u ? nullptr : items_.view_at(0u);
}

views::View* QuickInsertListItemContainerView::GetBottomItem() {
  return items_.view_size() == 0u ? nullptr
                                  : items_.view_at(items_.view_size() - 1u);
}

views::View* QuickInsertListItemContainerView::GetItemAbove(views::View* item) {
  const std::optional<size_t> index = items_.GetIndexOfView(item);
  if (!index.has_value() || *index == 0u) {
    return nullptr;
  }

  return items_.view_at(*index - 1u);
}

views::View* QuickInsertListItemContainerView::GetItemBelow(views::View* item) {
  const std::optional<size_t> index = items_.GetIndexOfView(item);
  if (!index.has_value() || *index == items_.view_size() - 1u) {
    return nullptr;
  }

  return items_.view_at(*index + 1u);
}

views::View* QuickInsertListItemContainerView::GetItemLeftOf(
    views::View* item) {
  return nullptr;
}

views::View* QuickInsertListItemContainerView::GetItemRightOf(
    views::View* item) {
  return nullptr;
}

bool QuickInsertListItemContainerView::ContainsItem(views::View* item) {
  return items_.GetIndexOfView(item).has_value();
}

QuickInsertListItemView* QuickInsertListItemContainerView::AddListItem(
    std::unique_ptr<QuickInsertListItemView> list_item) {
  items_.Add(list_item.get(), items_.view_size());
  return AddChildView(CreateListItemView())->AddChildView(std::move(list_item));
}

QuickInsertItemWithSubmenuView*
QuickInsertListItemContainerView::AddItemWithSubmenu(
    std::unique_ptr<QuickInsertItemWithSubmenuView> item_with_submenu) {
  items_.Add(item_with_submenu.get(), items_.view_size());
  return AddChildView(CreateListItemView())
      ->AddChildView(std::move(item_with_submenu));
}

BEGIN_METADATA(QuickInsertListItemContainerView)
END_METADATA

}  // namespace ash
