// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.hub;

import android.view.View;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.tab.Tab;

/**
 * Interface for controlling the Hub from a {@link Pane}. This is only available to the focused
 * pane.
 */
@NullMarked
public interface PaneHubController {
    /**
     * Sets a tab as active and hides the Hub. A tab must be selected if the browser is
     * transitioning to an active tab. Use {@link Tab.INVALID_TAB_ID} if a tab has already been
     * selected and doing so would repeat work.
     *
     * @param tabId The ID of the tab to select or {@link Tab.INVALID_TAB_ID}.
     */
    void selectTabAndHideHub(int tabId);

    /**
     * Focuses a pane taking focus away from the current pane.
     *
     * @param paneId The ID of the pane to focus.
     */
    void focusPane(@PaneId int paneId);

    /**
     * Allows retrieval of the View object that will allow switching to a particular pane. Should
     * never be used to perform click operations to switch panes, use {@link #focusPane(int)} for
     * that. Instead this is for purposes such as rooting IPHs.
     *
     * @param paneId The id the the pane to be searched for.
     * @return The view if currently present in the toolbar.
     */
    @Nullable
    View getPaneButton(@PaneId int paneId);

    /**
     * Set whether the search box background should show in the hub toolbar. The visibility for this
     * component is defaulted to gone and should only be shown for the shrink and expand animations
     * when transiting between a tab view and hub to prevent ghosting behind the hub search box.
     *
     * @param shouldShow Whether the search box background should be showing.
     */
    void setSearchBoxBackgroundProperties(boolean shouldShow);
}
