// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.keyboard_accessory.sheet_tabs;

import android.graphics.drawable.Drawable;
import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.DrawableRes;
import androidx.appcompat.content.res.AppCompatResources;
import androidx.recyclerview.widget.RecyclerView;

import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.keyboard_accessory.R;
import org.chromium.chrome.browser.keyboard_accessory.data.KeyboardAccessoryData;
import org.chromium.chrome.browser.keyboard_accessory.data.UserInfoField;
import org.chromium.chrome.browser.keyboard_accessory.sheet_tabs.AccessorySheetTabItemsModel.AccessorySheetDataPiece;
import org.chromium.chrome.browser.keyboard_accessory.sheet_tabs.AccessorySheetTabViewBinder.ElementViewHolder;
import org.chromium.components.browser_ui.widget.chips.ChipView;
import org.chromium.ui.modelutil.RecyclerViewAdapter;
import org.chromium.ui.modelutil.SimpleRecyclerViewMcp;

import java.util.function.Function;

class CreditCardAccessorySheetViewBinder {
    /** Generic UI Configurations that help to transform specific model data. */
    static class UiConfiguration {
        /**
         * Converts an {@link KeyboardAccessoryData.UserInfo} associated with a credit card to the
         * appropriate drawable.
         */
        public Function<KeyboardAccessoryData.UserInfo, Drawable> cardDrawableFunction;

        /**
         * Converts an {@link KeyboardAccessoryData.LoyaltyCardInfo} to the appropriate drawable.
         */
        public Function<KeyboardAccessoryData.LoyaltyCardInfo, Drawable>
                loyaltyCardDrawableFunction;
    }

    static ElementViewHolder create(
            UiConfiguration uiConfiguration,
            ViewGroup parent,
            @AccessorySheetDataPiece.Type int viewType) {
        switch (viewType) {
            case AccessorySheetDataPiece.Type.WARNING: // Fallthrough to reuse title container.
            case AccessorySheetDataPiece.Type.TITLE:
                return new AccessorySheetTabViewBinder.TitleViewHolder(parent);
            case AccessorySheetDataPiece.Type.CREDIT_CARD_INFO:
                return new CreditCardInfoViewHolder(parent, uiConfiguration.cardDrawableFunction);
            case AccessorySheetDataPiece.Type.PROMO_CODE_INFO:
                return new PromoCodeInfoViewHolder(parent);
            case AccessorySheetDataPiece.Type.IBAN_INFO:
                return new IbanInfoViewHolder(parent);
            case AccessorySheetDataPiece.Type.LOYALTY_CARD_INFO:
                return new LoyaltyCardInfoViewHolder(
                        parent, uiConfiguration.loyaltyCardDrawableFunction);
            case AccessorySheetDataPiece.Type.FOOTER_COMMAND:
                return AccessorySheetTabViewBinder.create(parent, viewType);
        }
        assert false : "Unhandled type of data piece: " + viewType;
        return null;
    }

    private static void bindChipView(ChipView chip, UserInfoField field) {
        chip.getPrimaryTextView().setText(field.getDisplayText());
        chip.getPrimaryTextView().setContentDescription(field.getA11yDescription());
        chip.setVisibility(field.getDisplayText().isEmpty() ? View.GONE : View.VISIBLE);
        if (!field.isSelectable()) {
            chip.setEnabled(false);
        } else {
            chip.setOnClickListener(src -> field.triggerSelection());
            chip.setClickable(true);
            chip.setEnabled(true);
        }
    }

    /** View which represents a single credit card and its selectable fields. */
    static class CreditCardInfoViewHolder
            extends ElementViewHolder<KeyboardAccessoryData.UserInfo, CreditCardAccessoryInfoView> {
        private final Function<KeyboardAccessoryData.UserInfo, Drawable> mCardDrawableFunction;

        CreditCardInfoViewHolder(
                ViewGroup parent,
                Function<KeyboardAccessoryData.UserInfo, Drawable> cardDrawableFunction) {
            super(parent, R.layout.keyboard_accessory_sheet_tab_credit_card_info);
            mCardDrawableFunction = cardDrawableFunction;
        }

        @Override
        protected void bind(KeyboardAccessoryData.UserInfo info, CreditCardAccessoryInfoView view) {
            bindChipView(view.getCCNumber(), info.getFields().get(0));
            bindChipView(view.getExpMonth(), info.getFields().get(1));
            bindChipView(view.getExpYear(), info.getFields().get(2));
            bindChipView(view.getCardholder(), info.getFields().get(3));
            bindChipView(view.getCvc(), info.getFields().get(4));

            view.getExpiryGroup()
                    .setVisibility(
                            view.getExpYear().getVisibility() == View.VISIBLE
                                            || view.getExpMonth().getVisibility() == View.VISIBLE
                                    ? View.VISIBLE
                                    : View.GONE);
            view.setIcon(mCardDrawableFunction.apply(info));
        }
    }

    /** View which represents a single Promo Code Offer and its fields. */
    static class PromoCodeInfoViewHolder
            extends ElementViewHolder<
                    KeyboardAccessoryData.PromoCodeInfo, PromoCodeAccessoryInfoView> {
        PromoCodeInfoViewHolder(ViewGroup parent) {
            super(parent, R.layout.keyboard_accessory_sheet_tab_promo_code_info);
        }

        @Override
        protected void bind(
                KeyboardAccessoryData.PromoCodeInfo info, PromoCodeAccessoryInfoView view) {
            bindChipView(view.getPromoCode(), info.getPromoCode());
            view.getDetailsText().setText(info.getDetailsText());
            view.getDetailsText()
                    .setVisibility(info.getDetailsText().isEmpty() ? View.GONE : View.VISIBLE);

            view.setIcon(
                    AppCompatResources.getDrawable(
                            view.getContext(), R.drawable.ic_logo_googleg_24dp));
        }
    }

    /** View which represents a single IBAN and its fields. */
    static class IbanInfoViewHolder
            extends ElementViewHolder<KeyboardAccessoryData.IbanInfo, IbanAccessoryInfoView> {
        IbanInfoViewHolder(ViewGroup parent) {
            super(parent, R.layout.keyboard_accessory_sheet_tab_iban_info);
        }

        @Override
        protected void bind(KeyboardAccessoryData.IbanInfo info, IbanAccessoryInfoView view) {
            bindChipView(view.getValue(), info.getValue());

            view.setIcon(AppCompatResources.getDrawable(view.getContext(), R.drawable.iban_icon));
        }
    }

    /** View which represents a single Google Wallet loyalty card and its fields. */
    static class LoyaltyCardInfoViewHolder
            extends ElementViewHolder<KeyboardAccessoryData.LoyaltyCardInfo, LoyaltyCardInfoView> {
        private final Function<KeyboardAccessoryData.LoyaltyCardInfo, Drawable>
                mLoyaltyCardDrawableFunction;

        LoyaltyCardInfoViewHolder(
                ViewGroup parent,
                Function<KeyboardAccessoryData.LoyaltyCardInfo, Drawable>
                        loyaltyCardDrawableFunction) {
            super(parent, R.layout.keyboard_accessory_sheet_tab_loyalty_card_info);
            mLoyaltyCardDrawableFunction = loyaltyCardDrawableFunction;
        }

        @Override
        protected void bind(KeyboardAccessoryData.LoyaltyCardInfo info, LoyaltyCardInfoView view) {
            view.getMerchantName().setText(info.getMerchantName());
            bindChipView(view.getLoyaltyCardNumber(), info.getLoyaltyCardNumber());
            view.setIcon(mLoyaltyCardDrawableFunction.apply(info));
        }
    }

    static void initializeView(
            RecyclerView view, UiConfiguration uiConfiguration, AccessorySheetTabItemsModel model) {
        view.setAdapter(
                new RecyclerViewAdapter<>(
                        new SimpleRecyclerViewMcp<>(
                                model,
                                AccessorySheetDataPiece::getType,
                                AccessorySheetTabViewBinder.ElementViewHolder::bind),
                        (parent, viewType) -> create(uiConfiguration, parent, viewType)));
        view.addItemDecoration(new DynamicInfoViewBottomSpacer(CreditCardAccessoryInfoView.class));
        view.addItemDecoration(new DynamicInfoViewBottomSpacer(PromoCodeAccessoryInfoView.class));
        view.addItemDecoration(new DynamicInfoViewBottomSpacer(IbanAccessoryInfoView.class));
    }

    static @DrawableRes int getDrawableForOrigin(String origin) {
        switch (origin) {
            case "americanExpressCC":
                return R.drawable.amex_metadata_card;
            case "dinersCC":
                return R.drawable.diners_metadata_card;
            case "discoverCC":
                return R.drawable.discover_metadata_card;
            case "eloCC":
                return R.drawable.elo_metadata_card;
            case "jcbCC":
                return R.drawable.jcb_metadata_card;
            case "masterCardCC":
                return R.drawable.mc_metadata_card;
            case "mirCC":
                return R.drawable.mir_metadata_card;
            case "troyCC":
                return R.drawable.troy_metadata_card;
            case "unionPayCC":
                return R.drawable.unionpay_metadata_card;
            case "verveCC":
                if (ChromeFeatureList.isEnabled(
                        ChromeFeatureList.AUTOFILL_ENABLE_VERVE_CARD_SUPPORT)) {
                    return R.drawable.verve_metadata_card;
                }
                break;
            case "visaCC":
                return R.drawable.visa_metadata_card;
        }
        return R.drawable.infobar_autofill_cc;
    }
}
