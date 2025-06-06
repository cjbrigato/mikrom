// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.facilitated_payments;

import android.graphics.Bitmap;
import android.graphics.drawable.Drawable;
import android.view.View.OnClickListener;

import org.chromium.base.Callback;
import org.chromium.build.annotations.NullMarked;
import org.chromium.ui.modelutil.MVCListAdapter.ModelList;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.modelutil.PropertyModel.ReadableIntPropertyKey;
import org.chromium.ui.modelutil.PropertyModel.ReadableObjectPropertyKey;
import org.chromium.ui.modelutil.PropertyModel.WritableBooleanPropertyKey;
import org.chromium.ui.modelutil.PropertyModel.WritableIntPropertyKey;
import org.chromium.ui.modelutil.PropertyModel.WritableObjectPropertyKey;

/**
 * Properties defined here reflect the visible state of the facilitated payments bottom sheet
 * component.
 */
@NullMarked
class FacilitatedPaymentsPaymentMethodsProperties {
    static final WritableIntPropertyKey VISIBLE_STATE = new WritableIntPropertyKey("visible_state");
    static final WritableIntPropertyKey SCREEN = new WritableIntPropertyKey("screen");
    static final WritableObjectPropertyKey<PropertyModel> SCREEN_VIEW_MODEL =
            new WritableObjectPropertyKey("screen_view_model");
    static final ReadableObjectPropertyKey<Callback<Integer>> UI_EVENT_LISTENER =
            new ReadableObjectPropertyKey<>("ui_event_listener");
    static final WritableBooleanPropertyKey SURVIVES_NAVIGATION =
            new WritableBooleanPropertyKey("survives_navigation");

    static final PropertyKey[] ALL_KEYS = {
        VISIBLE_STATE, SCREEN, SCREEN_VIEW_MODEL, UI_EVENT_LISTENER, SURVIVES_NAVIGATION
    };

    // TODO: b/348595414 - Rename to FopSelectorItemType and move to a separate directory.
    @interface ItemType {
        // The header at the top of the FacilitatedPayments bottom sheet.
        int HEADER = 0;

        // A section containing the bank account data.
        int BANK_ACCOUNT = 1;

        // Additional info to users making payments through bottom sheet.
        int ADDITIONAL_INFO = 2;

        // A "Continue" button, which is shown when there is only one payment
        // method available.
        int CONTINUE_BUTTON = 3;

        // A footer section containing additional actions.
        int FOOTER = 4;

        int EWALLET = 5;
    }

    // The visible state of the Facilitated Payments bottom sheet.
    @interface VisibleState {
        // The bottom sheet is not open. This is the default state.
        int HIDDEN = 0;
        // The bottom sheet is open and showing a screen.
        int SHOWN = 1;
        // The bottom sheet is in a temporary transition state before showing a new screen. The
        // bottom sheet can come to this temporary state from either of other 2 states, but will
        // always transition to the {@link SHOWN} state from here. Before showing a new screen, the
        // controller has to set this state.
        int SWAPPING_SCREEN = 2;
    }

    // The type of user visible screens that can be shown in the Facilitated Payments bottom sheet.
    @interface SequenceScreen {
        // No screen should be assigned 0 because {@link WritableIntPropertyKey} defaults to 0.
        int UNINITIALIZED = 0;
        // The screen showing the user's payment instruments.
        int FOP_SELECTOR = 1;
        // The screen showing a progress spinner.
        int PROGRESS_SCREEN = 2;
        // The screen showing an error message.
        int ERROR_SCREEN = 3;
        // The screen showing the PIX account linking prompt.
        int PIX_ACCOUNT_LINKING_PROMPT = 4;
    }

    /**
     * Properties defined here reflect the visible state of the FOP selector view shown in a bottom
     * sheet.
     */
    static class FopSelectorProperties {
        /** A list containing all the view items. They will be shown in a {@link RecyclerView}. */
        static final ReadableObjectPropertyKey<ModelList> SCREEN_ITEMS =
                new ReadableObjectPropertyKey("screen_items");

        /** All the properties of FOP selector screen. */
        static final PropertyKey[] ALL_KEYS = {SCREEN_ITEMS};

        private FopSelectorProperties() {}
    }

    /** Properties for a payment instrument entry in the facilitated payments bottom sheet. */
    static class BankAccountProperties {
        static final ReadableObjectPropertyKey<String> BANK_NAME =
                new ReadableObjectPropertyKey("bank_name");
        static final ReadableObjectPropertyKey<String> BANK_ACCOUNT_PAYMENT_RAIL =
                new ReadableObjectPropertyKey("bank_account_payment_rail");
        static final ReadableObjectPropertyKey<String> BANK_ACCOUNT_TYPE =
                new ReadableObjectPropertyKey("bank_account_type");
        static final ReadableObjectPropertyKey<String> BANK_ACCOUNT_NUMBER =
                new ReadableObjectPropertyKey("bank_account_number");
        static final ReadableObjectPropertyKey<String> BANK_ACCOUNT_TRANSACTION_LIMIT =
                new ReadableObjectPropertyKey("bank_account_transaction_limit");
        static final ReadableObjectPropertyKey<Drawable> BANK_ACCOUNT_ICON =
                new ReadableObjectPropertyKey<>("bank_account_icon");
        static final ReadableObjectPropertyKey<Runnable> ON_BANK_ACCOUNT_CLICK_ACTION =
                new ReadableObjectPropertyKey<>("on_bank_account_click_action");
        static final PropertyKey[] NON_TRANSFORMING_KEYS = {
            BANK_NAME,
            BANK_ACCOUNT_PAYMENT_RAIL,
            BANK_ACCOUNT_TYPE,
            BANK_ACCOUNT_NUMBER,
            BANK_ACCOUNT_TRANSACTION_LIMIT,
            BANK_ACCOUNT_ICON,
            ON_BANK_ACCOUNT_CLICK_ACTION
        };

        private BankAccountProperties() {}
    }

    static class EwalletProperties {
        static final ReadableObjectPropertyKey<String> EWALLET_NAME =
                new ReadableObjectPropertyKey("ewallet_name");
        static final ReadableObjectPropertyKey<String> ACCOUNT_DISPLAY_NAME =
                new ReadableObjectPropertyKey("account_display_name");
        static final ReadableIntPropertyKey EWALLET_DRAWABLE_ID =
                new ReadableIntPropertyKey("ewallet_drawable_id");
        static final ReadableObjectPropertyKey<Runnable> ON_EWALLET_CLICK_ACTION =
                new ReadableObjectPropertyKey<>("on_ewallet_click_action");
        static final ReadableObjectPropertyKey<Bitmap> EWALLET_ICON_BITMAP =
                new ReadableObjectPropertyKey<>("ewallet_icon_bitmap");
        static final PropertyKey[] NON_TRANSFORMING_KEYS = {
            EWALLET_NAME,
            ACCOUNT_DISPLAY_NAME,
            EWALLET_DRAWABLE_ID,
            ON_EWALLET_CLICK_ACTION,
            EWALLET_ICON_BITMAP
        };

        private EwalletProperties() {}
    }

    /**
     * Properties defined here reflect the visible state of the header in the facilitated payments
     * bottom sheet for payments.
     */
    static class HeaderProperties {
        static final ReadableIntPropertyKey PRODUCT_ICON_DRAWABLE_ID =
                new ReadableIntPropertyKey("product_icon_drawable_id");
        static final ReadableIntPropertyKey PRODUCT_ICON_HEIGHT =
                new ReadableIntPropertyKey("product_icon_height");
        static final ReadableIntPropertyKey PRODUCT_ICON_CONTENT_DESCRIPTION_ID =
                new ReadableIntPropertyKey("product_icon_content_description_id");
        static final ReadableIntPropertyKey SECURITY_CHECK_DRAWABLE_ID =
                new ReadableIntPropertyKey("security_check_drawable_id");
        static final ReadableObjectPropertyKey<String> TITLE =
                new ReadableObjectPropertyKey("title");
        static final ReadableIntPropertyKey DESCRIPTION_ID =
                new ReadableIntPropertyKey("description_id");

        static final PropertyKey[] ALL_KEYS = {
            PRODUCT_ICON_DRAWABLE_ID,
            PRODUCT_ICON_HEIGHT,
            PRODUCT_ICON_CONTENT_DESCRIPTION_ID,
            SECURITY_CHECK_DRAWABLE_ID,
            TITLE,
            DESCRIPTION_ID
        };

        private HeaderProperties() {}
    }

    /**
     * Properties defined here reflect the visible state of the additional info in the facilitated
     * payments bottom sheet for payments.
     */
    static class AdditionalInfoProperties {
        static final ReadableIntPropertyKey DESCRIPTION_ID =
                new ReadableIntPropertyKey("additional_info_description_id");
        static final ReadableObjectPropertyKey<Runnable> SHOW_PAYMENT_METHOD_SETTINGS_CALLBACK =
                new ReadableObjectPropertyKey<>("show_payment_method_settings_callback");

        static final PropertyKey[] ALL_KEYS = {
            DESCRIPTION_ID, SHOW_PAYMENT_METHOD_SETTINGS_CALLBACK
        };

        private AdditionalInfoProperties() {}
    }

    /**
     * Properties defined here reflect the visible state of the footer in the facilitated payments
     * bottom sheet for payments.
     */
    static class FooterProperties {
        static final PropertyModel.ReadableObjectPropertyKey<Runnable>
                SHOW_PAYMENT_METHOD_SETTINGS_CALLBACK =
                        new ReadableObjectPropertyKey<>("show_payment_method_settings_callback");

        static final PropertyKey[] ALL_KEYS = {SHOW_PAYMENT_METHOD_SETTINGS_CALLBACK};

        private FooterProperties() {}
    }

    /**
     * Properties defined here reflect the visible state of the error screen shown in a bottom
     * sheet.
     */
    static class ErrorScreenProperties {
        /** Primary button callback. */
        static final WritableObjectPropertyKey<OnClickListener> PRIMARY_BUTTON_CALLBACK =
                new WritableObjectPropertyKey<>("primary_button_callback");

        /** All the properties of error screen. */
        static final PropertyKey[] ALL_KEYS = {PRIMARY_BUTTON_CALLBACK};

        private ErrorScreenProperties() {}
    }

    /**
     * Properties defined here reflect the visible state of the Pix account linking prompt shown in
     * a bottom sheet.
     */
    static class PixAccountLinkingPromptProperties {
        static final WritableObjectPropertyKey<OnClickListener> ACCEPT_BUTTON_CALLBACK =
                new WritableObjectPropertyKey<>("accept_button_callback");
        static final WritableObjectPropertyKey<OnClickListener> DECLINE_BUTTON_CALLBACK =
                new WritableObjectPropertyKey<>("decline_button_callback");

        /** All the properties of Pix account linking prompt. */
        static final PropertyKey[] ALL_KEYS = {ACCEPT_BUTTON_CALLBACK, DECLINE_BUTTON_CALLBACK};
    }

    private FacilitatedPaymentsPaymentMethodsProperties() {}
}
