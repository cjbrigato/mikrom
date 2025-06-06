// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.autofill.vcn;

import android.graphics.Bitmap;
import android.graphics.drawable.Drawable;

import androidx.annotation.DrawableRes;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.components.autofill.AutofillFeatures;
import org.chromium.components.autofill.VirtualCardEnrollmentLinkType;
import org.chromium.components.autofill.payments.LegalMessageLine;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel.ReadableObjectPropertyKey;
import org.chromium.ui.modelutil.PropertyModel.WritableBooleanPropertyKey;
import org.chromium.url.GURL;

import java.util.List;
import java.util.function.Function;

/** The model of the autofill virtual card number (VCN) enrollment bottom sheet UI. */
@NullMarked
/*package*/ abstract class AutofillVcnEnrollBottomSheetProperties {
    /** Opens links. */
    static interface LinkOpener {
        /**
         * Opens a link and records the metric for opening it.
         *
         * @param url The link to open.
         * @param linkType the type of link being opened. Used for logging metrics.
         */
        void openLink(String url, @VirtualCardEnrollmentLinkType int linkType);
    }

    /** The description for the VCN enrollment bottom sheet. */
    static class Description {
        /** The text that describes what a virtual card does, including a "learn more" link text. */
        final String mText;

        /** The "learn more" link text. */
        final String mLearnMoreLinkText;

        /** The URL to open when the "learn more" link is tapped. */
        final String mLearnMoreLinkUrl;

        /** The type of link to record in metrics when the "learn more" link is tapped. */
        @VirtualCardEnrollmentLinkType final int mLearnMoreLinkType;

        /** The opener for the "learn more" link. */
        final LinkOpener mLinkOpener;

        /**
         * Constructs a description for the VCN enrollment bottom sheet.
         *
         * @param text The text that describes what a virtual card does, including a "learn more"
         *             link text.
         * @param learnMoreLinkText The "learn more" link text.
         * @param learnMoreLinkUrl The URL to open when the "learn more" link is tapped.
         * @param learnMoreLinkType The type of link to record in metrics when the "learn more" link
         *                          is tapped.
         * @param linkOpener The link opener.
         */
        Description(
                String text,
                String learnMoreLinkText,
                String learnMoreLinkUrl,
                @VirtualCardEnrollmentLinkType int learnMoreLinkType,
                LinkOpener linkOpener) {
            mText = text;
            mLearnMoreLinkText = learnMoreLinkText;
            mLearnMoreLinkUrl = learnMoreLinkUrl;
            mLearnMoreLinkType = learnMoreLinkType;
            mLinkOpener = linkOpener;
        }
    }

    /** Legal messages. */
    static class LegalMessages {
        /** Legal message lines. */
        final List<LegalMessageLine> mLines;

        /** The type of link to record in metrics when a link is tapped. */
        @VirtualCardEnrollmentLinkType final int mLinkType;

        /** The opener for the links in the legal messages. */
        final LinkOpener mLinkOpener;

        /**
         * Constructs legal messages.
         *
         * @param lines The legal message lines.
         * @param linkType the type of link to record in metrics when link is tapped.
         */
        LegalMessages(
                List<LegalMessageLine> lines,
                @VirtualCardEnrollmentLinkType int linkType,
                LinkOpener linkOpener) {
            mLines = lines;
            mLinkType = linkType;
            mLinkOpener = linkOpener;
        }
    }

    /** Issuer icon. */
    static class IssuerIcon {
        /** The bitmap for the issuer icon. */
        final @Nullable Bitmap mBitmap;

        /** The resource id for the issuer icon. */
        final @DrawableRes int mIconResource;

        /** The url for an issuer icon. */
        final @Nullable GURL mIconUrl;

        /** The width of the issuer icon. */
        final int mWidth;

        /** The height of the issuer icon. */
        final int mHeight;

        /** Constructs an issuer icon. */
        IssuerIcon(Bitmap bitmap, int width, int height) {
            assert !ChromeFeatureList.isEnabled(
                    AutofillFeatures.AUTOFILL_ENABLE_VIRTUAL_CARD_JAVA_PAYMENTS_DATA_MANAGER);
            mBitmap = bitmap;
            mIconResource = 0;
            mIconUrl = null;
            mWidth = width;
            mHeight = height;
        }

        /* Constructs an issuer icon given a default icon resource and an optional custom url. */
        IssuerIcon(@DrawableRes int iconResource, GURL iconUrl) {
            assert ChromeFeatureList.isEnabled(
                    AutofillFeatures.AUTOFILL_ENABLE_VIRTUAL_CARD_JAVA_PAYMENTS_DATA_MANAGER);
            mBitmap = null;
            mIconResource = iconResource;
            mIconUrl = iconUrl;
            mWidth = 0;
            mHeight = 0;
        }
    }

    /**
     * A call back to retrieve the drawable for the given issuer icon when the issuer icon.
     *
     * <p>This callback does not apply to IssuerIcons initialized with a bitmap, constructed via
     * {@link IssuerIcon#IssuerIcon(Bitmap, int, int)}.
     */
    static final ReadableObjectPropertyKey<Function<IssuerIcon, Drawable>>
            ISSUER_ICON_FETCH_CALLBACK = new ReadableObjectPropertyKey<>();

    /** The prompt message for the bottom sheet. */
    static final ReadableObjectPropertyKey<String> MESSAGE_TEXT = new ReadableObjectPropertyKey<>();

    /** The description text with a "learn more" link. */
    static final ReadableObjectPropertyKey<Description> DESCRIPTION =
            new ReadableObjectPropertyKey<>();

    /** The icon for the card. */
    static final ReadableObjectPropertyKey<IssuerIcon> ISSUER_ICON =
            new ReadableObjectPropertyKey<>();

    /** The label for the card. */
    static final ReadableObjectPropertyKey<String> CARD_LABEL = new ReadableObjectPropertyKey<>();

    /** Legal messages from Google Pay. */
    static final ReadableObjectPropertyKey<LegalMessages> GOOGLE_LEGAL_MESSAGES =
            new ReadableObjectPropertyKey<>();

    /** Legal messages from the issuer bank. */
    static final ReadableObjectPropertyKey<LegalMessages> ISSUER_LEGAL_MESSAGES =
            new ReadableObjectPropertyKey<>();

    /** The label for the button that enrolls a virtual card. */
    static final ReadableObjectPropertyKey<String> ACCEPT_BUTTON_LABEL =
            new ReadableObjectPropertyKey<>();

    /** The label for the button that cancels enrollment. */
    static final ReadableObjectPropertyKey<String> CANCEL_BUTTON_LABEL =
            new ReadableObjectPropertyKey<>();

    /** Indicates whether the bottom sheet is in a loading state. */
    static final WritableBooleanPropertyKey SHOW_LOADING_STATE = new WritableBooleanPropertyKey();

    static final PropertyKey[] ALL_KEYS = {
        MESSAGE_TEXT,
        DESCRIPTION,
        ISSUER_ICON,
        ISSUER_ICON_FETCH_CALLBACK,
        CARD_LABEL,
        GOOGLE_LEGAL_MESSAGES,
        ISSUER_LEGAL_MESSAGES,
        ACCEPT_BUTTON_LABEL,
        CANCEL_BUTTON_LABEL,
        SHOW_LOADING_STATE
    };

    /** Do not instantiate. */
    private AutofillVcnEnrollBottomSheetProperties() {}
}
