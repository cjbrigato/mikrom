// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.toolbar.optional_button;

import android.content.res.Resources;
import android.graphics.drawable.Drawable;
import android.view.View;
import android.view.View.OnClickListener;

import androidx.annotation.CallSuper;
import androidx.annotation.StringRes;

import org.chromium.base.FeatureList;
import org.chromium.base.ObserverList;
import org.chromium.base.supplier.Supplier;
import org.chromium.build.annotations.Contract;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.toolbar.adaptive.AdaptiveToolbarButtonVariant;
import org.chromium.chrome.browser.toolbar.adaptive.AdaptiveToolbarFeatures;
import org.chromium.chrome.browser.user_education.IphCommandBuilder;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modaldialog.ModalDialogManager.ModalDialogManagerObserver;
import org.chromium.ui.modelutil.PropertyModel;

/** Base class for button data providers used on the adaptive toolbar. */
@NullMarked
public abstract class BaseButtonDataProvider implements ButtonDataProvider, OnClickListener {
    protected final ButtonDataImpl mButtonData;
    protected final Supplier<@Nullable Tab> mActiveTabSupplier;

    private final ObserverList<ButtonDataObserver> mObservers = new ObserverList<>();
    private final @Nullable ModalDialogManager mModalDialogManager;
    private @Nullable ModalDialogManagerObserver mModalDialogObserver;

    private boolean mShouldShowOnIncognitoTabs;

    /**
     * Creates a new instance of {@code BaseButtonDataProvider}.
     *
     * @param activeTabSupplier Supplier for the current active tab.
     * @param modalDialogManager Modal dialog manager, used to disable the button when a dialog is
     *     visible. Can be null to disable this behavior.
     * @param buttonDrawable Drawable for the button icon.
     * @param contentDescription String for the button's content description.
     * @param actionChipLabelResId String for the button's action chip label, can be
     *     Resources.ID_NULL is the button doesn't support action chip.
     * @param supportsTinting Whether the button's icon should be tinted.
     * @param iphCommandBuilder An IPH command builder instance to show when the button is
     *     displayed, can be null.
     * @param adaptiveButtonVariant Enum value of {@link AdaptiveToolbarButtonVariant}, used for
     *     metrics.
     * @param tooltipTextResId String to show as a tooltip when the button is hovered over.
     */
    public BaseButtonDataProvider(
            Supplier<@Nullable Tab> activeTabSupplier,
            @Nullable ModalDialogManager modalDialogManager,
            Drawable buttonDrawable,
            String contentDescription,
            @StringRes int actionChipLabelResId,
            boolean supportsTinting,
            @Nullable IphCommandBuilder iphCommandBuilder,
            @AdaptiveToolbarButtonVariant int adaptiveButtonVariant,
            @StringRes int tooltipTextResId) {
        mActiveTabSupplier = activeTabSupplier;
        mModalDialogManager = modalDialogManager;
        if (mModalDialogManager != null) {
            mModalDialogObserver =
                    new ModalDialogManagerObserver() {
                        @Override
                        public void onDialogAdded(PropertyModel model) {
                            mButtonData.setEnabled(false);
                            notifyObservers(mButtonData.canShow());
                        }

                        @Override
                        public void onLastDialogDismissed() {
                            mButtonData.setEnabled(true);
                            notifyObservers(mButtonData.canShow());
                        }
                    };
            mModalDialogManager.addObserver(mModalDialogObserver);
        }

        if (!AdaptiveToolbarFeatures.isDynamicAction(adaptiveButtonVariant)) {
            assert actionChipLabelResId == Resources.ID_NULL
                    : "Action chip should only be used on dynamic actions";
        }

        mButtonData =
                new ButtonDataImpl(
                        /* canShow= */ false,
                        buttonDrawable,
                        /* onClickListener= */ this,
                        contentDescription,
                        actionChipLabelResId,
                        supportsTinting,
                        /* iphCommandBuilder= */ iphCommandBuilder,
                        /* isEnabled= */ true,
                        adaptiveButtonVariant,
                        tooltipTextResId);
    }

    /**
     * Checks if the button should be shown for the current tab. Called every time {@code get()} is
     * invoked.
     *
     * @param tab Current tab.
     * @return whether the button should be shown for the current tab.
     */
    @CallSuper
    @Contract("null -> false")
    protected boolean shouldShowButton(@Nullable Tab tab) {
        if (tab == null) return false;

        if (tab.isIncognito() && !mShouldShowOnIncognitoTabs) return false;

        return true;
    }

    protected void notifyObservers(boolean hint) {
        for (ButtonDataObserver observer : mObservers) {
            observer.buttonDataChanged(hint);
        }
    }

    /**
     * Sets the button's {@link IphCommandBuilder} if needed, called every time {@code get()} is
     * invoked.
     *
     * @param tab Current tab.
     */
    private void maybeSetIphCommandBuilder(@Nullable Tab tab) {
        if (mButtonData.getButtonSpec().getIphCommandBuilder() != null
                || tab == null
                || !FeatureList.isInitialized()
                || !AdaptiveToolbarFeatures.isCustomizationEnabled()
                || AdaptiveToolbarFeatures.shouldShowActionChip(
                        mButtonData.getButtonSpec().getButtonVariant())) {
            return;
        }

        mButtonData.updateIphCommandBuilder(getIphCommandBuilder(tab));
    }

    /** Sets whether the button should be shown on incognito tabs, default is false. */
    protected void setShouldShowOnIncognitoTabs(boolean shouldShowOnIncognitoTabs) {
        mShouldShowOnIncognitoTabs = shouldShowOnIncognitoTabs;
    }

    /**
     * Gets an {@link IphCommandBuilder} builder instance to use on this button. Only called when
     * native is initialized and when there's no IphCommandBuilder set.
     *
     * @param tab Current tab.
     * @return An {@link org.chromium.chrome.browser.user_education.IphCommand} instance to set on
     *     this button, or null if no IPH should be used.
     */
    protected @Nullable IphCommandBuilder getIphCommandBuilder(Tab tab) {
        return null;
    }

    /** ButtonDataProvider implementation. */
    @Override
    public void addObserver(ButtonDataObserver obs) {
        mObservers.addObserver(obs);
    }

    @Override
    public void removeObserver(ButtonDataObserver obs) {
        mObservers.removeObserver(obs);
    }

    @Override
    public ButtonData get(@Nullable Tab tab) {
        mButtonData.setCanShow(shouldShowButton(tab));
        maybeSetIphCommandBuilder(tab);

        return mButtonData;
    }

    @Override
    @CallSuper
    @SuppressWarnings("NullAway")
    public void destroy() {
        mObservers.clear();
        if (mModalDialogManager != null) {
            mModalDialogManager.removeObserver(mModalDialogObserver);
        }
    }

    /* OnClickListener implementation. */
    @Override
    public abstract void onClick(View view);
}
