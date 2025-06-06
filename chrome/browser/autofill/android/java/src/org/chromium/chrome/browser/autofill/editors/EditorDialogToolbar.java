// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.autofill.editors;

import android.content.Context;
import android.util.AttributeSet;
import android.view.Menu;
import android.view.MenuItem;

import androidx.appcompat.widget.Toolbar;

import org.chromium.build.annotations.NullMarked;
import org.chromium.chrome.browser.autofill.R;

/** Simple class for displaying a toolbar in the editor dialog. */
@NullMarked
class EditorDialogToolbar extends Toolbar {
    private boolean mShowDeleteMenuItem = true;

    /** Constructor for when the toolbar is inflated from XML. */
    public EditorDialogToolbar(Context context, AttributeSet attrs) {
        super(context, attrs);
        inflateMenu(R.menu.prefeditor_editor_menu);
        updateMenu();
    }

    /** Sets whether or not the the delete menu item will be shown. */
    public void setShowDeleteMenuItem(boolean state) {
        mShowDeleteMenuItem = state;
        updateMenu();
    }

    /** Updates what is displayed in the menu. */
    public void updateMenu() {
        Menu menu = getMenu();

        MenuItem deleteMenuItem = menu.findItem(R.id.delete_menu_id);
        if (deleteMenuItem != null) deleteMenuItem.setVisible(mShowDeleteMenuItem);
    }
}
