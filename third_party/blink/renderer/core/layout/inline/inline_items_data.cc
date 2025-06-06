// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/inline/inline_items_data.h"

namespace blink {

void InlineItemsData::GetOpenTagItems(wtf_size_t start_index,
                                      wtf_size_t size,
                                      OpenTagItems* open_items) const {
  DCHECK_LE(size, items.size());
  for (const Member<InlineItem>& item_ptr :
       base::span(items).subspan(start_index, size)) {
    const InlineItem& item = *item_ptr;
    if (item.Type() == InlineItem::kOpenTag) {
      open_items->push_back(item_ptr);
    } else if (item.Type() == InlineItem::kCloseTag) {
      open_items->pop_back();
    }
  }
}

#if DCHECK_IS_ON()
void InlineItemsData::CheckConsistency() const {
  for (const Member<InlineItem>& item : items) {
    item->CheckTextType(text_content);
  }
}
#endif

void InlineItemsData::Trace(Visitor* visitor) const {
  visitor->Trace(items);
  visitor->Trace(offset_mapping);
}

}  // namespace blink
