// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.payments.secure_payment_confirmation;

import android.graphics.drawable.Drawable;
import android.text.SpannableString;
import android.util.Pair;

import org.chromium.build.annotations.NullMarked;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel.ReadableBooleanPropertyKey;
import org.chromium.ui.modelutil.PropertyModel.ReadableObjectPropertyKey;

/**
 * The properties of the SecurePaymentConfirmation Authn UI, which fully describe the state of the
 * UI.
 */
@NullMarked
/* package */ class SecurePaymentConfirmationAuthnProperties {
    /** The store value of the UI. */
    /* package */ static final ReadableObjectPropertyKey<String> STORE_LABEL =
            new ReadableObjectPropertyKey<>();

    /**
     * The payment icon for the UI. The second parameter indicates whether this is the default
     * payment icon or not.
     */
    /* package */ static final ReadableObjectPropertyKey<Pair<Drawable, Boolean>> PAYMENT_ICON =
            new ReadableObjectPropertyKey<>();

    /** The payment value of the UI. */
    /* package */ static final ReadableObjectPropertyKey<CharSequence> PAYMENT_INSTRUMENT_LABEL =
            new ReadableObjectPropertyKey<>();

    /** The total value of the UI. */
    /* package */ static final ReadableObjectPropertyKey<CharSequence> TOTAL =
            new ReadableObjectPropertyKey<>();

    /** The currency value of the UI. */
    /* package */ static final ReadableObjectPropertyKey<CharSequence> CURRENCY =
            new ReadableObjectPropertyKey<>();

    /** The opt out information of the UI, including whether or not to display it. */
    /* package */ static final ReadableObjectPropertyKey<
                    SecurePaymentConfirmationAuthnView.OptOutInfo>
            OPT_OUT_INFO = new ReadableObjectPropertyKey<>();

    /** The callback when the continue button is pressed. */
    /* package */ static final ReadableObjectPropertyKey<Runnable> CONTINUE_BUTTON_CALLBACK =
            new ReadableObjectPropertyKey<>();

    /** The callback when the cancel button is pressed. */
    /* package */ static final ReadableObjectPropertyKey<Runnable> CANCEL_BUTTON_CALLBACK =
            new ReadableObjectPropertyKey<>();

    /**
     * When true, shows the issuer and network icons (if provided) in the header instead of the
     * default header image.
     */
    /* package */ static final ReadableBooleanPropertyKey SHOWS_ISSUER_NETWORK_ICONS =
            new ReadableBooleanPropertyKey();

    /** The issuer icon for the UI. */
    /* package */ static final ReadableObjectPropertyKey<Drawable> ISSUER_ICON =
            new ReadableObjectPropertyKey<>();

    /** The network icon for the UI. */
    /* package */ static final ReadableObjectPropertyKey<Drawable> NETWORK_ICON =
            new ReadableObjectPropertyKey<>();

    /** The title text for the UI. */
    /* package */ static final ReadableObjectPropertyKey<String> TITLE =
            new ReadableObjectPropertyKey<>();

    /** The label for the continue button. */
    /* package */ static final ReadableObjectPropertyKey<String> CONTINUE_BUTTON_LABEL =
            new ReadableObjectPropertyKey<>();

    /** The footnote for the UI. */
    /* package */ static final ReadableObjectPropertyKey<SpannableString> FOOTNOTE =
            new ReadableObjectPropertyKey<>();

    /* package */ static final PropertyKey[] ALL_KEYS =
            new PropertyKey[] {
                STORE_LABEL,
                PAYMENT_ICON,
                PAYMENT_INSTRUMENT_LABEL,
                TOTAL,
                CURRENCY,
                OPT_OUT_INFO,
                CONTINUE_BUTTON_CALLBACK,
                CANCEL_BUTTON_CALLBACK,
                SHOWS_ISSUER_NETWORK_ICONS,
                ISSUER_ICON,
                NETWORK_ICON,
                TITLE,
                CONTINUE_BUTTON_LABEL,
                FOOTNOTE,
            };

    // Prevent instantiation.
    private SecurePaymentConfirmationAuthnProperties() {}
}
