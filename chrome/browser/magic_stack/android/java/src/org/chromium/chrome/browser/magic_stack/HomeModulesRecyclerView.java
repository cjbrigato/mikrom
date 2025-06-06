// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.magic_stack;

import static android.view.ViewGroup.LayoutParams.MATCH_PARENT;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.content.Context;
import android.content.res.Resources;
import android.graphics.Canvas;
import android.util.AttributeSet;
import android.view.View;

import androidx.annotation.VisibleForTesting;
import androidx.recyclerview.widget.RecyclerView;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.ui.base.ViewUtils;

/** A custom RecyclerView implementation for the home modules. */
@NullMarked
public class HomeModulesRecyclerView extends RecyclerView {

    /* Whether the activity is running on a tablet.*/
    private boolean mIsTablet;

    /** The value is updated for tablets when displayStyle is changed. */
    private int mItemPerScreen;

    /** The start margin of the recyclerview in pixel. */
    private int mStartMarginPx;

    /* The internal padding between two modules in pixel. */
    private int mModuleInternalPaddingPx;

    /** The minimal module height. */
    private int mModuleMiniHeightPx;

    /** The last height of the RecyclerView. */
    private int mPreviousHeight;

    /** The last children count of the RecyclerView. */
    private int mPreviousChildCount;

    public HomeModulesRecyclerView(Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
    }

    /**
     * Initializes the recyclerview.
     *
     * @param isTablet Whether the activity is running on a tablet.
     * @param startMarginPx The start margin of the recyclerview in pixel.
     * @param itemPerScreen The number of modules are shown per screen.
     */
    void initialize(boolean isTablet, int startMarginPx, int itemPerScreen) {
        mIsTablet = isTablet;
        mStartMarginPx = startMarginPx;

        mItemPerScreen = itemPerScreen;
        Resources resources = getContext().getResources();
        mModuleInternalPaddingPx = resources.getDimensionPixelSize(R.dimen.module_internal_padding);
        mModuleMiniHeightPx = resources.getDimensionPixelSize(R.dimen.home_module_height);
    }

    @Override
    public void onDraw(Canvas c) {
        super.onDraw(c);

        int height = getMeasuredHeight();
        int childCount = getChildCount();
        // Updates Children' heights if the RecyclerView's height is changed or when the children
        // count changes.
        if (childCount != 0 && (height != mPreviousHeight || childCount != mPreviousChildCount)) {
            mPreviousHeight = height;
            mPreviousChildCount = childCount;
            updateHeight(childCount);
        }

        // Don't need to change the width of a child view on phones since there is only one item
        // shown per screen, and it never changes.
        if (!mIsTablet) return;

        assumeNonNull(getAdapter());
        int itemCount = getAdapter().getItemCount();
        int measuredWidth = getMeasuredWidth();
        for (int i = 0; i < getChildCount(); i++) {
            onDrawImplTablet(getChildAt(i), itemCount, measuredWidth);
        }
    }

    /**
     * Returns the maximum height of children of the RecyclerView if they grow higher than the
     * minimal module height.
     */
    @VisibleForTesting
    int getMaxHeight() {
        // A minimal height is set for all modules.
        int maxHeight = mModuleMiniHeightPx;

        for (int i = 0; i < getChildCount(); i++) {
            View child = getChildAt(i);
            maxHeight = Math.max(maxHeight, child.getHeight());
        }

        return maxHeight;
    }

    /**
     * Updates the heights of all children to make them heights equal.
     *
     * @param childCount The total count of children.
     */
    @VisibleForTesting
    void updateHeight(int childCount) {
        boolean isLayoutChanged = false;
        int maxHeight = getMaxHeight();
        for (int i = 0; i < childCount; i++) {
            View child = getChildAt(i);
            if (child.getHeight() < maxHeight) {
                child.setMinimumHeight(maxHeight);
                isLayoutChanged = true;
            }
        }

        // Request layout to apply the changes
        if (isLayoutChanged) {
            ViewUtils.requestLayout(this, "HomeModulesRecyclerView.updateHieght");
        }
    }

    /** Called when the DisplayStyle is changed. */
    void onDisplayStyleChanged(int startMarginPx, int itemPerScreen) {
        mStartMarginPx = startMarginPx;
        mItemPerScreen = itemPerScreen;
    }

    @VisibleForTesting
    // This function is only called on Tablets.
    void onDrawImplTablet(View view, int totalChildCount, int measuredWidth) {
        assert mIsTablet;

        MarginLayoutParams marginLayoutParams = (MarginLayoutParams) view.getLayoutParams();
        if (mItemPerScreen == 1 || totalChildCount == 1) {
            // If showing one item per screen, the view's width should match the parent
            // recyclerview.
            marginLayoutParams.width = MATCH_PARENT;
            // We should always update margins on tablets. This is because when there is only one
            // item to show, the margins could be different based on the width of the window.
            // See b/352583431.
            updateMargin(view, marginLayoutParams);
        } else {
            // On a wide screen, we will show 2 cards instead of 1 on the magic stack.
            // Updates the width of the view.
            int width =
                    (measuredWidth - mModuleInternalPaddingPx * (mItemPerScreen - 1))
                            / mItemPerScreen;
            if (marginLayoutParams.width == width) return;

            marginLayoutParams.width = width;
            updateMargin(view, marginLayoutParams);
        }
    }

    private void updateMargin(View view, MarginLayoutParams marginLayoutParams) {
        marginLayoutParams.setMarginEnd(mStartMarginPx);
        marginLayoutParams.setMarginStart(mStartMarginPx);
        view.setLayoutParams(marginLayoutParams);
    }

    void setStartMarginPxForTesting(int startMarginPx) {
        mStartMarginPx = startMarginPx;
    }
}
