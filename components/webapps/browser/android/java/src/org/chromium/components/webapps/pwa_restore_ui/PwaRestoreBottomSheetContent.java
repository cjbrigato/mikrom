// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.webapps.pwa_restore_ui;

import android.content.Context;
import android.view.View;

import androidx.annotation.StringRes;

import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetContent;
import org.chromium.components.webapps.R;

/** The class handling the bottom sheet install for the PWA Restore UI. */
@NullMarked
public class PwaRestoreBottomSheetContent implements BottomSheetContent {
    // The view for our bottom sheet.
    private final PwaRestoreBottomSheetView mView;

    // This content's priority.
    private @ContentPriority int mPriority = ContentPriority.LOW;

    // Helps keep track of whether the Back button was pressed.
    private final ObservableSupplierImpl<Boolean> mBackPressStateChangedSupplier =
            new ObservableSupplierImpl<>();

    // The handler to notify when the (Android) Back button is pressed.
    Runnable mOsBackButtonClicked;

    public PwaRestoreBottomSheetContent(
            PwaRestoreBottomSheetView view, Runnable onOsBackButtonClicked) {
        mView = view;

        mOsBackButtonClicked = onOsBackButtonClicked;
        mBackPressStateChangedSupplier.set(true);
    }

    public void setPriority(@ContentPriority int priority) {
        mPriority = priority;
    }

    // BottomSheetContent:

    @Override
    public View getContentView() {
        return mView.getContentView();
    }

    @Override
    public @Nullable View getToolbarView() {
        return null;
    }

    @Override
    public float getHalfHeightRatio() {
        return BottomSheetContent.HeightMode.DISABLED;
    }

    @Override
    public float getFullHeightRatio() {
        return BottomSheetContent.HeightMode.WRAP_CONTENT;
    }

    @Override
    public ObservableSupplierImpl<Boolean> getBackPressStateChangedSupplier() {
        return mBackPressStateChangedSupplier;
    }

    @Override
    public void onBackPressed() {
        mOsBackButtonClicked.run();
    }

    @Override
    public int getVerticalScrollOffset() {
        return 0;
    }

    @Override
    public void destroy() {}

    @Override
    public int getPriority() {
        return mPriority;
    }

    @Override
    public boolean swipeToDismissEnabled() {
        return false;
    }

    @Override
    public String getSheetContentDescription(Context context) {
        return context.getString(R.string.pwa_restore_bottom_sheet_accessibility);
    }

    @Override
    public @StringRes int getSheetHalfHeightAccessibilityStringId() {
        return R.string.pwa_restore_bottom_sheet_accessibility;
    }

    @Override
    public @StringRes int getSheetFullHeightAccessibilityStringId() {
        return R.string.pwa_restore_bottom_sheet_accessibility;
    }

    @Override
    public @StringRes int getSheetClosedAccessibilityStringId() {
        return R.string.pwa_restore_bottom_sheet_accessibility;
    }
}
