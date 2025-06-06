// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.autofill.vcn;

import static org.chromium.build.NullUtil.assumeNonNull;

import org.chromium.base.Callback;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.layouts.LayoutStateProvider;
import org.chromium.chrome.browser.layouts.LayoutStateProvider.LayoutStateObserver;
import org.chromium.chrome.browser.layouts.LayoutType;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabSelectionType;
import org.chromium.chrome.browser.tabmodel.TabList;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelObserver;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;

/**
 * The lifecycle for the virtual card number (VCN) enrollment bottom sheet. Notifies the caller when
 * a tab or layout changes (e.g., going into the "tab overview"), so the bottom sheet can be
 * dismissed. Ignores page navigations.
 */
@NullMarked
/*package*/ class AutofillVcnEnrollBottomSheetLifecycle
        implements Callback<TabModelSelector>, TabModelObserver, LayoutStateObserver {
    private final Callback<TabModel> mCurrentTabModelObserver = this::onTabModelSelected;
    private final LayoutStateProvider mLayoutStateProvider;
    private final ObservableSupplier<TabModelSelector> mTabModelSelectorSupplier;
    private @Nullable Runnable mOnEndOfLifecycle;
    private boolean mHasBegun;
    private @Nullable TabModelSelector mTabModelSelector;
    private @Nullable TabModel mTabModel;

    /**
     * Constructs the lifecycle for the virtual card number (VCN) enrollment bottom sheet.
     *
     * @param layoutStateProvider Exposes a way to listen to layout state changes.
     * @param tabModelSelectorSupplier Supplies the tab model selector.
     */
    AutofillVcnEnrollBottomSheetLifecycle(
            LayoutStateProvider layoutStateProvider,
            ObservableSupplier<TabModelSelector> tabModelSelectorSupplier) {
        mLayoutStateProvider = layoutStateProvider;
        mTabModelSelectorSupplier = tabModelSelectorSupplier;
    }

    /** @return Whether the lifecycle can begin. Depends on the current layout. */
    boolean canBegin() {
        return !mHasBegun && mLayoutStateProvider.isLayoutVisible(LayoutType.BROWSING);
    }

    /**
     * Begins the lifecycle of the virtual card number (VCN) enrollment bottom sheet. Starts
     * observing tab and layout changes.
     *
     * @param onEndOfLifecycle The callback to run when the bottom sheet must be hidden. Invoked
     *                         when a tab or layout changes (e.g., going into "tab overview").
     */
    void begin(Runnable onEndOfLifecycle) {
        mOnEndOfLifecycle = onEndOfLifecycle;
        mHasBegun = true;

        mLayoutStateProvider.addObserver(this);
        mTabModelSelectorSupplier.addObserver(this);
    }

    /**
     * @return Whether the lifecycle has begun. Returns true when observing tab and layout changes.
     */
    boolean hasBegun() {
        return mHasBegun;
    }

    /**
     * Ends the lifecycle of the virtual card number (VCN) enrollment bottom sheet. Stops observing
     * tab and layout changes.
     */
    void end() {
        mLayoutStateProvider.removeObserver(this);
        mTabModelSelectorSupplier.removeObserver(this);

        if (mTabModelSelector != null) {
            mTabModelSelector.getCurrentTabModelSupplier().removeObserver(mCurrentTabModelObserver);
        }
        if (mTabModel != null) mTabModel.removeObserver(this);

        mHasBegun = false;
    }

    private void endLifecycleAndNotifyCaller() {
        end();
        assumeNonNull(mOnEndOfLifecycle);
        mOnEndOfLifecycle.run();
    }

    private void onTabModelSelected(@Nullable TabModel newModel) {
        if (newModel == mTabModel) return;

        if (mTabModel != null) {
            endLifecycleAndNotifyCaller();
            return;
        }

        if (newModel != null) {
            mTabModel = newModel;
            mTabModel.addObserver(this);
        }
    }

    // Implements LayoutStateObserver for LayoutStateProvider.
    @Override
    public void onStartedShowing(int layoutType) {
        endLifecycleAndNotifyCaller();
    }

    // Implements Callback<TabModelSelector> for ObservableSupplier<TabModelSelector>.
    @Override
    public void onResult(TabModelSelector tabModelSelector) {
        mTabModelSelectorSupplier.removeObserver(this);

        mTabModelSelector = tabModelSelector;
        tabModelSelector.getCurrentTabModelSupplier().addObserver(mCurrentTabModelObserver);

        TabModel currentTabModel = mTabModelSelector.getCurrentModel();
        if (currentTabModel.index() != TabList.INVALID_TAB_INDEX) {
            mTabModel = currentTabModel;
            mTabModel.addObserver(this);
        }
    }

    // Implements TabModelObserver for TabModel.
    @Override
    public void didSelectTab(Tab tab, @TabSelectionType int type, int lastId) {
        if (tab == null || tab.getId() != lastId) endLifecycleAndNotifyCaller();
    }
}
