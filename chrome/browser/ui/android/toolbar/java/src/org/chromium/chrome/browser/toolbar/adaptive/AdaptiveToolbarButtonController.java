// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.toolbar.adaptive;

import static org.chromium.build.NullUtil.assumeNonNull;
import static org.chromium.chrome.browser.preferences.ChromePreferenceKeys.ADAPTIVE_TOOLBAR_CUSTOMIZATION_ENABLED;
import static org.chromium.chrome.browser.preferences.ChromePreferenceKeys.ADAPTIVE_TOOLBAR_CUSTOMIZATION_SETTINGS;
import static org.chromium.chrome.browser.toolbar.adaptive.settings.AdaptiveToolbarSettingsFragment.ARG_UI_STATE_AUTO_BUTTON_CAPTION;
import static org.chromium.chrome.browser.toolbar.adaptive.settings.AdaptiveToolbarSettingsFragment.ARG_UI_STATE_CAN_SHOW_UI;
import static org.chromium.chrome.browser.toolbar.adaptive.settings.AdaptiveToolbarSettingsFragment.ARG_UI_STATE_PREFERENCE_SELECTION;
import static org.chromium.chrome.browser.toolbar.adaptive.settings.AdaptiveToolbarSettingsFragment.ARG_UI_STATE_TOOLBAR_BUTTON_STATE;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.os.Bundle;
import android.view.View;

import androidx.annotation.VisibleForTesting;

import org.chromium.base.Callback;
import org.chromium.base.CallbackController;
import org.chromium.base.ContextUtils;
import org.chromium.base.FeatureList;
import org.chromium.base.ObserverList;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.OneShotCallback;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.lifecycle.ConfigurationChangedObserver;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.settings.SettingsNavigationFactory;
import org.chromium.chrome.browser.tab.CurrentTabObserver;
import org.chromium.chrome.browser.tab.EmptyTabObserver;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.toolbar.R;
import org.chromium.chrome.browser.toolbar.adaptive.AdaptiveToolbarStatePredictor.UiState;
import org.chromium.chrome.browser.toolbar.adaptive.settings.AdaptiveToolbarSettingsFragment;
import org.chromium.chrome.browser.toolbar.optional_button.BaseButtonDataProvider;
import org.chromium.chrome.browser.toolbar.optional_button.ButtonData;
import org.chromium.chrome.browser.toolbar.optional_button.ButtonData.ButtonSpec;
import org.chromium.chrome.browser.toolbar.optional_button.ButtonDataImpl;
import org.chromium.chrome.browser.toolbar.optional_button.ButtonDataProvider;
import org.chromium.chrome.browser.toolbar.optional_button.ButtonDataProvider.ButtonDataObserver;
import org.chromium.components.browser_ui.settings.SettingsNavigation;
import org.chromium.content_public.browser.NavigationHandle;
import org.chromium.ui.permissions.AndroidPermissionDelegate;

import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.Objects;

/** Meta {@link ButtonDataProvider} which chooses the optional button variant that will be shown. */
@NullMarked
public class AdaptiveToolbarButtonController
        implements ButtonDataProvider,
                ButtonDataObserver,
                SharedPreferences.OnSharedPreferenceChangeListener,
                ConfigurationChangedObserver {

    private final Context mContext;
    private final ObserverList<ButtonDataObserver> mObservers = new ObserverList<>();
    private @Nullable ButtonDataProvider mSingleProvider;

    // Maps from {@link AdaptiveToolbarButtonVariant} to {@link ButtonDataProvider}.
    private final Map<Integer, ButtonDataProvider> mButtonDataProviderMap = new HashMap<>();

    /**
     * {@link ButtonData} instance returned by {@link AdaptiveToolbarButtonController#get(Tab)}
     * when wrapping {@code mOriginalButtonSpec}.
     */
    private final ButtonDataImpl mButtonData = new ButtonDataImpl();

    /** The last received {@link ButtonSpec}. */
    private @Nullable ButtonSpec mOriginalButtonSpec;

    /** {@code true} if the SessionVariant histogram value was already recorded. */
    private boolean mIsSessionVariantRecorded;

    private final ActivityLifecycleDispatcher mLifecycleDispatcher;
    private final AndroidPermissionDelegate mAndroidPermissionDelegate;
    private final CallbackController mCallbackController;
    private final Callback<AdaptiveToolbarStatePredictor.UiState> mUiStateCallback;
    private final AdaptiveToolbarBehavior mToolbarBehavior;

    private @Nullable AdaptiveToolbarStatePredictor mAdaptiveToolbarStatePredictor;
    private View.@Nullable OnLongClickListener mMenuHandler;
    private final Callback<Integer> mMenuClickListener;
    private final AdaptiveButtonActionMenuCoordinator mMenuCoordinator;
    private int mScreenWidthDp;

    private @AdaptiveToolbarButtonVariant int mSessionButtonVariant =
            AdaptiveToolbarButtonVariant.UNKNOWN;
    private @Nullable CurrentTabObserver mPageLoadMetricsRecorder;

    /**
     * Constructs the {@link AdaptiveToolbarButtonController}.
     *
     * @param context used in {@link SettingsNavigation}
     * @param lifecycleDispatcher notifies about native initialization
     * @param profileSupplier Allows access to the {@link Profile} for the current session.
     */
    // Suppress to observe SharedPreferences, which is discouraged; use another messaging channel
    // instead.
    @SuppressWarnings("UseSharedPreferencesManagerFromChromeCheck")
    public AdaptiveToolbarButtonController(
            Context context,
            ActivityLifecycleDispatcher lifecycleDispatcher,
            ObservableSupplier<Profile> profileSupplier,
            AdaptiveButtonActionMenuCoordinator menuCoordinator,
            AdaptiveToolbarBehavior toolbarBehavior,
            AndroidPermissionDelegate androidPermissionDelegate) {
        mContext = context;
        mMenuClickListener =
                id -> {
                    if (id == R.id.customize_adaptive_button_menu_id) {
                        RecordUserAction.record("MobileAdaptiveMenuCustomize");
                        assumeNonNull(mAdaptiveToolbarStatePredictor);
                        mAdaptiveToolbarStatePredictor.recomputeUiState(this::startSettings);
                        return;
                    }
                    assert false : "unknown adaptive button menu id: " + id;
                };
        mLifecycleDispatcher = lifecycleDispatcher;
        mLifecycleDispatcher.register(this);
        mMenuCoordinator = menuCoordinator;
        mToolbarBehavior = toolbarBehavior;
        mScreenWidthDp = context.getResources().getConfiguration().screenWidthDp;
        mAndroidPermissionDelegate = androidPermissionDelegate;
        mCallbackController = new CallbackController();
        mUiStateCallback =
                uiState -> {
                    mSessionButtonVariant =
                            uiState.canShowUi
                                    ? uiState.toolbarButtonState
                                    : AdaptiveToolbarButtonVariant.UNKNOWN;
                    setSingleProvider(mSessionButtonVariant);
                    notifyObservers(uiState.canShowUi);
                };

        new OneShotCallback<>(
                profileSupplier, mCallbackController.makeCancelable(this::setProfile));
    }

    private void startSettings(UiState uiState) {
        Bundle args = new Bundle();
        args.putBoolean(ARG_UI_STATE_CAN_SHOW_UI, uiState.canShowUi);
        args.putInt(ARG_UI_STATE_TOOLBAR_BUTTON_STATE, uiState.toolbarButtonState);
        args.putInt(ARG_UI_STATE_PREFERENCE_SELECTION, uiState.preferenceSelection);
        args.putInt(ARG_UI_STATE_AUTO_BUTTON_CAPTION, uiState.autoButtonCaption);
        SettingsNavigationFactory.createSettingsNavigation()
                .startSettings(mContext, AdaptiveToolbarSettingsFragment.class, args);
    }

    /**
     * Adds an instance of a button variant to the collection of buttons managed by {@code
     * AdaptiveToolbarButtonController}.
     *
     * @param variant The button variant of {@code buttonProvider}.
     * @param buttonProvider The provider implementing the button variant. {@code
     *     AdaptiveToolbarButtonController} takes ownership of the provider and will {@link
     *     #destroy()} it, once the provider is no longer needed.
     */
    public void addButtonVariant(
            @AdaptiveToolbarButtonVariant int variant, ButtonDataProvider buttonProvider) {
        assert variant >= 0 && variant <= AdaptiveToolbarButtonVariant.MAX_VALUE
                : "invalid adaptive button variant: " + variant;
        assert variant != AdaptiveToolbarButtonVariant.UNKNOWN
                : "must not provide UNKNOWN button provider";
        assert variant != AdaptiveToolbarButtonVariant.NONE
                : "must not provide NONE button provider";

        mButtonDataProviderMap.put(variant, buttonProvider);
    }

    /**
     * Invoke Price Insights UI. TODO(crbug.com/391931899): Consider making this method generic to
     * support other button variants.
     */
    public void runPriceInsightsAction() {
        var buttonDataProvider =
                mButtonDataProviderMap.get(AdaptiveToolbarButtonVariant.PRICE_INSIGHTS);
        if (buttonDataProvider instanceof BaseButtonDataProvider toolbarButtonProvider) {
            toolbarButtonProvider.onClick(new View(mContext)); // Param is not used.
        } else {
            assert false : "PriceInsightButtonController must inherit BaseButtonDataProvider!";
        }
    }

    @Override
    // Suppress to observe SharedPreferences, which is discouraged; use another messaging channel
    // instead.
    @SuppressWarnings("UseSharedPreferencesManagerFromChromeCheck")
    public void destroy() {
        setSingleProvider(AdaptiveToolbarButtonVariant.UNKNOWN);
        mObservers.clear();
        mCallbackController.destroy();
        ContextUtils.getAppSharedPreferences().unregisterOnSharedPreferenceChangeListener(this);
        mLifecycleDispatcher.unregister(this);

        Iterator<Map.Entry<Integer, ButtonDataProvider>> it =
                mButtonDataProviderMap.entrySet().iterator();
        while (it.hasNext()) {
            Map.Entry<Integer, ButtonDataProvider> entry = it.next();
            entry.getValue().destroy();
            it.remove();
        }
    }

    private void setSingleProvider(@AdaptiveToolbarButtonVariant int buttonVariant) {
        @Nullable ButtonDataProvider buttonProvider = mButtonDataProviderMap.get(buttonVariant);
        if (mSingleProvider != null) {
            mSingleProvider.removeObserver(this);
        }
        mSingleProvider = buttonProvider;
        if (mSingleProvider != null) {
            mSingleProvider.addObserver(this);
        }
    }

    @Override
    public void addObserver(ButtonDataObserver obs) {
        mObservers.addObserver(obs);
    }

    @Override
    public void removeObserver(ButtonDataObserver obs) {
        mObservers.removeObserver(obs);
    }

    @Override
    public @Nullable ButtonData get(@Nullable Tab tab) {
        final ButtonData receivedButtonData =
                mSingleProvider == null ? null : mSingleProvider.get(tab);
        if (receivedButtonData == null) {
            mOriginalButtonSpec = null;
            return null;
        }

        if (!mIsSessionVariantRecorded
                && receivedButtonData.canShow()
                && receivedButtonData.isEnabled()
                && !receivedButtonData.getButtonSpec().isDynamicAction()) {
            mIsSessionVariantRecorded = true;
            RecordHistogram.recordEnumeratedHistogram(
                    "Android.AdaptiveToolbarButton.SessionVariant",
                    receivedButtonData.getButtonSpec().getButtonVariant(),
                    AdaptiveToolbarButtonVariant.MAX_VALUE);
        }

        mButtonData.setCanShow(
                receivedButtonData.canShow() && shouldButtonShowBasedOnScreenWidth());
        mButtonData.setShouldShowTextBubble(mToolbarBehavior.shouldShowTextBubble());
        mButtonData.setEnabled(receivedButtonData.isEnabled());
        final ButtonSpec receivedButtonSpec = receivedButtonData.getButtonSpec();
        // ButtonSpec is immutable, so we keep the previous value when noting changes.
        if (!Objects.equals(receivedButtonSpec, mOriginalButtonSpec)) {
            assert receivedButtonSpec.getOnLongClickListener() == null
                    : "adaptive button variants are expected to not set a long click listener";
            if (mMenuHandler == null) mMenuHandler = createMenuHandler();
            mOriginalButtonSpec = receivedButtonSpec;
            mButtonData.setButtonSpec(
                    new ButtonSpec(
                            receivedButtonSpec.getDrawable(),
                            wrapClickListener(
                                    receivedButtonSpec.getOnClickListener(),
                                    receivedButtonSpec.getButtonVariant()),
                            // Use menu handler only with static actions.
                            receivedButtonSpec.isDynamicAction() ? null : mMenuHandler,
                            receivedButtonSpec.getContentDescription(),
                            receivedButtonSpec.getSupportsTinting(),
                            receivedButtonSpec.getIphCommandBuilder(),
                            receivedButtonSpec.getButtonVariant(),
                            receivedButtonSpec.getActionChipLabelResId(),
                            receivedButtonSpec.getHoverTooltipTextId(),
                            receivedButtonSpec.hasErrorBadge(),
                            receivedButtonSpec.isChecked()));
        }
        return mButtonData;
    }

    private boolean shouldButtonShowBasedOnScreenWidth() {
        return mToolbarBehavior.shouldShowTextBubble() || isScreenWideEnoughForButton();
    }

    private static View.OnClickListener wrapClickListener(
            View.OnClickListener receivedListener,
            @AdaptiveToolbarButtonVariant int buttonVariant) {
        return view -> {
            RecordHistogram.recordEnumeratedHistogram(
                    "Android.AdaptiveToolbarButton.Clicked",
                    buttonVariant,
                    AdaptiveToolbarButtonVariant.MAX_VALUE);
            receivedListener.onClick(view);
        };
    }

    private View.@Nullable OnLongClickListener createMenuHandler() {
        if (!FeatureList.isInitialized()) return null;
        return mMenuCoordinator.createOnLongClickListener(mMenuClickListener);
    }

    @Override
    public void buttonDataChanged(boolean canShowHint) {
        notifyObservers(canShowHint);
    }

    @VisibleForTesting
    void setProfile(Profile profile) {
        assert mAdaptiveToolbarStatePredictor == null;
        profile = profile.getOriginalProfile();
        mAdaptiveToolbarStatePredictor =
                new AdaptiveToolbarStatePredictor(
                        mContext, profile, mAndroidPermissionDelegate, mToolbarBehavior);
        ContextUtils.getAppSharedPreferences().registerOnSharedPreferenceChangeListener(this);

        if (!AdaptiveToolbarFeatures.isCustomizationEnabled()) return;
        mAdaptiveToolbarStatePredictor.recomputeUiState(mUiStateCallback);
        AdaptiveToolbarStats.recordSelectedSegmentFromSegmentationPlatformAsync(
                mContext, mAdaptiveToolbarStatePredictor);
        // We need the menu handler only if the customization feature is on.
        if (mMenuHandler != null) return;
        mMenuHandler = createMenuHandler();
        if (mMenuHandler == null) return;

        // Clearing mOriginalButtonSpec forces a refresh of mButtonData on the next get()
        mOriginalButtonSpec = null;
        notifyObservers(mButtonData.canShow());
    }

    private void notifyObservers(boolean canShowHint) {
        for (ButtonDataObserver observer : mObservers) {
            observer.buttonDataChanged(canShowHint);
        }
    }

    private boolean isScreenWideEnoughForButton() {
        return mScreenWidthDp >= AdaptiveToolbarFeatures.getDeviceMinimumWidthForShowingButton();
    }

    /** Returns the {@link ButtonDataProvider} used in a single-variant mode. */
    public @Nullable ButtonDataProvider getSingleProviderForTesting() {
        return mSingleProvider;
    }

    @Override
    public void onSharedPreferenceChanged(SharedPreferences sharedPrefs, @Nullable String key) {
        assert mAdaptiveToolbarStatePredictor != null;
        if (ADAPTIVE_TOOLBAR_CUSTOMIZATION_SETTINGS.equals(key)
                || ADAPTIVE_TOOLBAR_CUSTOMIZATION_ENABLED.equals(key)) {
            assert AdaptiveToolbarFeatures.isCustomizationEnabled();
            mAdaptiveToolbarStatePredictor.recomputeUiState(mUiStateCallback);
        }
    }

    /** Called to notify the controller that a dynamic action is available and should be shown. */
    public void showDynamicAction(@AdaptiveToolbarButtonVariant int action) {
        int actionToShow =
                action != AdaptiveToolbarButtonVariant.UNKNOWN ? action : mSessionButtonVariant;
        RecordHistogram.recordEnumeratedHistogram(
                "Android.AdaptiveToolbarButton.Variant.OnPageLoad",
                actionToShow,
                AdaptiveToolbarButtonVariant.MAX_VALUE);
        if (mOriginalButtonSpec != null && mOriginalButtonSpec.getButtonVariant() == actionToShow) {
            return;
        }
        setSingleProvider(actionToShow);
        notifyObservers(true);
    }

    /**
     * Creates a metrics recorder that records the button variant shown for every page load. The
     * metrics is recorded at the start of a new navigation for the old page being shown.
     *
     * @param tabSupplier Supplier of current tab.
     */
    public void initializePageLoadMetricsRecorder(ObservableSupplier<@Nullable Tab> tabSupplier) {
        if (mPageLoadMetricsRecorder != null) return;
        mPageLoadMetricsRecorder =
                new CurrentTabObserver(
                        tabSupplier,
                        new EmptyTabObserver() {
                            @Override
                            public void onDidStartNavigationInPrimaryMainFrame(
                                    Tab tab, NavigationHandle navigationHandle) {
                                Integer currentVariant = AdaptiveToolbarButtonVariant.UNKNOWN;
                                for (Integer variant : mButtonDataProviderMap.keySet()) {
                                    if (mSingleProvider == mButtonDataProviderMap.get(variant)) {
                                        currentVariant = variant;
                                        break;
                                    }
                                }

                                RecordHistogram.recordEnumeratedHistogram(
                                        "Android.AdaptiveToolbarButton.Variant.OnStartNavigation",
                                        currentVariant,
                                        AdaptiveToolbarButtonVariant.MAX_VALUE);
                            }
                        },
                        null);
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        if (!mLifecycleDispatcher.isNativeInitializationFinished()
                || mScreenWidthDp == newConfig.screenWidthDp) {
            return;
        }

        boolean wasOldScreenWideEnoughForButton = isScreenWideEnoughForButton();

        mScreenWidthDp = newConfig.screenWidthDp;

        if (wasOldScreenWideEnoughForButton != isScreenWideEnoughForButton()) {
            notifyObservers(mButtonData.canShow());
        }
    }
}
