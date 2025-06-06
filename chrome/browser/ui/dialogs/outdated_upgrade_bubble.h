// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_DIALOGS_OUTDATED_UPGRADE_BUBBLE_H_
#define CHROME_BROWSER_UI_DIALOGS_OUTDATED_UPGRADE_BUBBLE_H_

namespace content {
class PageNavigator;
}

namespace ui {
class ElementContext;
}

// OutdatedUpgradeBubbleView warns the user that an upgrade is long overdue.
// It is intended to be used as the content of a bubble anchored off of the
// Chrome toolbar.
void ShowOutdatedUpgradeBubble(ui::ElementContext element_context,
                               content::PageNavigator* page_navigator,
                               bool auto_update_enabled);

#endif  // CHROME_BROWSER_UI_DIALOGS_OUTDATED_UPGRADE_BUBBLE_H_
