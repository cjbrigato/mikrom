// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.webapps.pwa_universal_install;

import android.content.Context;
import android.view.View;

import androidx.annotation.StringRes;

import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetContent;
import org.chromium.components.webapps.R;

/** The class handling the bottom sheet install for the PWA Universal Install UI. */
@NullMarked
public class PwaUniversalInstallBottomSheetContent implements BottomSheetContent {
    // The view for our bottom sheet.
    private final PwaUniversalInstallBottomSheetView mView;

    // This content is shown as a result of a user action (selecting from the Chrome menu), so the
    // priority should be high.
    private @ContentPriority int mPriority = ContentPriority.HIGH;

    // A callback to run when the Back button is pressed.
    private final Runnable mRecordBackButtonCallback;

    // Helps keep track of whether the Back button was pressed.
    private final ObservableSupplierImpl<Boolean> mBackPressStateChangedSupplier =
            new ObservableSupplierImpl<>();

    public PwaUniversalInstallBottomSheetContent(
            PwaUniversalInstallBottomSheetView view, Runnable recordBackButtonCallback) {
        mView = view;
        mRecordBackButtonCallback = recordBackButtonCallback;

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
        return HeightMode.DISABLED;
    }

    @Override
    public float getFullHeightRatio() {
        return HeightMode.WRAP_CONTENT;
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
        return true;
    }

    @Override
    public ObservableSupplierImpl<Boolean> getBackPressStateChangedSupplier() {
        return mBackPressStateChangedSupplier;
    }

    @Override
    public void onBackPressed() {
        mRecordBackButtonCallback.run();
    }

    @Override
    public String getSheetContentDescription(Context context) {
        return context.getString(R.string.pwa_uni_bottom_sheet_accessibility);
    }

    @Override
    public @StringRes int getSheetHalfHeightAccessibilityStringId() {
        return R.string.pwa_uni_bottom_sheet_accessibility;
    }

    @Override
    public @StringRes int getSheetFullHeightAccessibilityStringId() {
        return R.string.pwa_uni_bottom_sheet_accessibility;
    }

    @Override
    public @StringRes int getSheetClosedAccessibilityStringId() {
        return R.string.pwa_uni_bottom_sheet_accessibility;
    }
}
