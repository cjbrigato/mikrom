// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.educational_tip;

import android.content.Context;

import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.build.annotations.NullMarked;
import org.chromium.chrome.browser.hub.PaneId;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;

/** The delegate to provide actions for the educational tips module. */
@NullMarked
public interface EducationTipModuleActionDelegate {

    /** Gets the application context. */
    Context getContext();

    /** Gets the profile supplier. */
    ObservableSupplier<Profile> getProfileSupplier();

    /** Gets the tab model selector. */
    TabModelSelector getTabModelSelector();

    /** Gets the instance of {@link BottomSheetController}. */
    BottomSheetController getBottomSheetController();

    /**
     * Opens the Hub layout and displays a specific pane.
     *
     * @param paneId The id of the pane to show.
     */
    void openHubPane(@PaneId int paneId);

    /** Opens the grid tab switcher and show the tab grip Iph dialog. */
    void openTabGroupIphDialog();

    /** Opens the app menu and highlights the quick delete menu item. */
    void openAndHighlightQuickDeleteMenuItem();

    /** Opens the the history sync opt in page. */
    void showHistorySyncOptIn(Runnable removeModuleCallback);

    /**
     * Returns the total number of tabs for relaunch across both regular and incognito browsing
     * modes through shared preference key.
     */
    int getTabCountForRelaunchFromSharedPrefs();
}
