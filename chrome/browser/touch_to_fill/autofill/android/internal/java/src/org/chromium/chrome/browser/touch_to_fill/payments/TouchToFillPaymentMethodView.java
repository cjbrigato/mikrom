// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.touch_to_fill.payments;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.RelativeLayout;

import androidx.annotation.NonNull;
import androidx.annotation.Px;
import androidx.annotation.StringRes;
import androidx.recyclerview.widget.RecyclerView;

import org.chromium.chrome.browser.touch_to_fill.common.ItemDividerBase;
import org.chromium.chrome.browser.touch_to_fill.common.TouchToFillViewBase;
import org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.ItemType;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;

import java.util.Set;

/**
 * This class is responsible for rendering the bottom sheet which displays the
 * TouchToFillPaymentMethod. It is a View in this Model-View-Controller component and doesn't inherit
 * but holds Android Views.
 */
class TouchToFillPaymentMethodView extends TouchToFillViewBase {
    private static class HorizontalDividerItemDecoration extends ItemDividerBase {
        HorizontalDividerItemDecoration(Context context) {
            super(context);
        }

        @Override
        protected int selectBackgroundDrawable(
                int position, boolean containsFillButton, int itemCount) {
            return super.selectBackgroundDrawable(position, containsFillButton, itemCount);
        }

        @Override
        protected boolean shouldSkipItemType(@ItemType int type) {
            switch (type) {
                case ItemType.HEADER: // Fallthrough.
                case ItemType.FOOTER: // Fallthrough.
                case ItemType.FILL_BUTTON:
                case ItemType.TERMS_LABEL:
                    return true;
                case ItemType.CREDIT_CARD:
                case ItemType.IBAN:
                case ItemType.LOYALTY_CARD:
                    return false;
            }
            assert false : "Undefined whether to skip setting background for item of type: " + type;
            return true; // Should never be reached. But if, skip to not change anything.
        }

        @Override
        protected boolean containsFillButton(RecyclerView parent) {
            int itemCount = parent.getAdapter().getItemCount();
            // The button will be above the footer if it's present.
            return itemCount > 1
                    && parent.getAdapter().getItemViewType(itemCount - 2) == ItemType.FILL_BUTTON;
        }
    }

    /**
     * Constructs a TouchToFillPaymentMethodView which creates, modifies, and shows the bottom sheet.
     *
     * @param context A {@link Context} used to load resources and inflate the sheet.
     * @param bottomSheetController The {@link BottomSheetController} used to show/hide the sheet.
     */
    TouchToFillPaymentMethodView(Context context, BottomSheetController bottomSheetController) {
        super(
                bottomSheetController,
                (RelativeLayout)
                        LayoutInflater.from(context).inflate(R.layout.touch_to_fill_sheet, null),
                true);

        getSheetItemListView().addItemDecoration(new HorizontalDividerItemDecoration(context));
    }

    @Override
    public int getVerticalScrollOffset() {
        return getSheetItemListView().computeVerticalScrollOffset();
    }

    @Override
    public @NonNull String getSheetContentDescription(Context context) {
        // TODO - crbug.com/: Update for loyalty cards.
        return context.getString(R.string.autofill_payment_method_bottom_sheet_content_description);
    }

    @Override
    public @StringRes int getSheetHalfHeightAccessibilityStringId() {
        // TODO - crbug.com/: Update for loyalty cards.
        return R.string.autofill_payment_method_bottom_sheet_half_height;
    }

    @Override
    public @StringRes int getSheetFullHeightAccessibilityStringId() {
        // TODO - crbug.com/: Update for loyalty cards.
        return R.string.autofill_payment_method_bottom_sheet_full_height;
    }

    @Override
    public @StringRes int getSheetClosedAccessibilityStringId() {
        // TODO - crbug.com/: Update for loyalty cards.
        return R.string.autofill_payment_method_bottom_sheet_closed;
    }

    @Override
    protected View getHandlebar() {
        return getContentView().findViewById(R.id.drag_handlebar);
    }

    @Override
    protected int getConclusiveMarginHeightPx() {
        return getContentView().getResources().getDimensionPixelSize(R.dimen.ttf_sheet_padding);
    }

    @Override
    protected @Px int getSideMarginPx() {
        return getContentView().getResources().getDimensionPixelSize(R.dimen.ttf_sheet_padding);
    }

    @Override
    protected Set<Integer> listedItemTypes() {
        return Set.of(
                TouchToFillPaymentMethodProperties.ItemType.CREDIT_CARD,
                TouchToFillPaymentMethodProperties.ItemType.IBAN,
                TouchToFillPaymentMethodProperties.ItemType.LOYALTY_CARD);
    }

    @Override
    protected int footerItemType() {
        return TouchToFillPaymentMethodProperties.ItemType.FOOTER;
    }
}
