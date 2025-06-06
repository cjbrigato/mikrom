// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.customtabs.features.toolbar;

import android.content.Context;
import android.content.res.ColorStateList;

import androidx.annotation.ColorInt;
import androidx.annotation.VisibleForTesting;

import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.browserservices.intents.BrowserServicesIntentDataProvider;
import org.chromium.chrome.browser.browserservices.intents.BrowserServicesIntentDataProvider.CustomTabProfileType;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.lifecycle.TopResumedActivityChangedObserver;
import org.chromium.chrome.browser.theme.SurfaceColorUpdateUtils;
import org.chromium.chrome.browser.theme.ThemeColorProvider;
import org.chromium.chrome.browser.theme.ThemeUtils;
import org.chromium.chrome.browser.toolbar.ToolbarManager;
import org.chromium.chrome.browser.ui.theme.BrandedColorScheme;
import org.chromium.chrome.browser.ui.web_app_header.WebAppHeaderUtils;
import org.chromium.components.browser_ui.desktop_windowing.DesktopWindowStateManager;
import org.chromium.ui.util.ColorUtils;

/** Maintains the toolbar color for {@link CustomTabActivity}. */
public class CustomTabToolbarColorController
        implements ThemeColorProvider.ThemeColorObserver,
                ThemeColorProvider.TintObserver,
                TopResumedActivityChangedObserver {
    private final BrowserServicesThemeColorProvider mBrowserServicesThemeColorProvider;
    private final @Nullable DesktopWindowStateManager mDesktopWindowStateManager;
    private final Context mContext;
    private final BrowserServicesIntentDataProvider mIntentDataProvider;
    private final ActivityLifecycleDispatcher mActivityLifecycleDispatcher;

    private DesktopWindowStateManager.AppHeaderObserver mHeaderObserver;
    private ToolbarManager mToolbarManager;

    /** Whether the current activity is in a focused window, the top resumed activity. */
    private boolean mIsTopResumedActivity;

    public CustomTabToolbarColorController(
            Context context,
            BrowserServicesThemeColorProvider browserServicesThemeColorProvider,
            DesktopWindowStateManager desktopWindowStateManager,
            BrowserServicesIntentDataProvider intentDataProvider,
            ActivityLifecycleDispatcher activityLifecycleDispatcher) {
        mContext = context;
        mIntentDataProvider = intentDataProvider;
        mActivityLifecycleDispatcher = activityLifecycleDispatcher;
        mActivityLifecycleDispatcher.register(this);

        mDesktopWindowStateManager = desktopWindowStateManager;
        if (mDesktopWindowStateManager != null) {
            mHeaderObserver = createAppHeaderObserver();
            mDesktopWindowStateManager.addObserver(mHeaderObserver);
        }

        // TODO(crbug.com/419860074): Use an IntDef enum to solidify desktop windowing focus state
        // definitions.
        mIsTopResumedActivity =
                mDesktopWindowStateManager == null
                        || !mDesktopWindowStateManager.isInUnfocusedDesktopWindow();

        mBrowserServicesThemeColorProvider = browserServicesThemeColorProvider;
        mBrowserServicesThemeColorProvider.addThemeColorObserver(this);
        mBrowserServicesThemeColorProvider.addTintObserver(this);
    }

    private DesktopWindowStateManager.AppHeaderObserver createAppHeaderObserver() {
        return new DesktopWindowStateManager.AppHeaderObserver() {
            @Override
            public void onDesktopWindowingModeChanged(boolean isInDesktopWindow) {
                updateBackgroundColor();
                updateTint();
            }
        };
    }

    @Override
    public void onThemeColorChanged(int color, boolean shouldAnimate) {
        updateBackgroundColor();
    }

    @Override
    public void onTintChanged(
            @Nullable ColorStateList tint,
            @Nullable ColorStateList activityFocusTint,
            int brandedColorScheme) {
        updateTint();
    }

    /**
     * Notifies the ColorController that the ToolbarManager has been created and is ready for use.
     * ToolbarManager isn't passed directly to the constructor because it's not guaranteed to be
     * initialized yet.
     */
    public void onToolbarInitialized(ToolbarManager manager) {
        mToolbarManager = manager;
        assert manager != null : "Toolbar manager not initialized";

        updateBackgroundColor();
        updateTint();
    }

    private void updateBackgroundColor() {
        if (mToolbarManager == null) return;

        @ColorInt int themeColor = resolveThemeColor();
        mToolbarManager.setShouldUpdateToolbarPrimaryColor(true);
        mToolbarManager.onThemeColorChanged(themeColor, false);
        mToolbarManager.setShouldUpdateToolbarPrimaryColor(false);
    }

    private void updateTint() {
        if (mToolbarManager == null) return;

        @ColorInt int themeColor = resolveThemeColor();
        @BrandedColorScheme int scheme = getColorScheme(themeColor);
        ColorStateList tint = resolveTint(scheme);
        ColorStateList focusTint = calculateActivityFocusTint(mContext, scheme);
        mToolbarManager.setShouldUpdateToolbarPrimaryColor(true);
        mToolbarManager.onTintChanged(tint, focusTint, scheme);
        mToolbarManager.setShouldUpdateToolbarPrimaryColor(false);
    }

    private ColorStateList calculateActivityFocusTint(
            Context context, @BrandedColorScheme int brandedColorScheme) {
        var iconTint = ThemeUtils.getThemedToolbarIconTint(context, brandedColorScheme);

        // Only consider activity state when in desktop windowing mode.
        return shouldUseDefaultThemeForWebApp()
                ? ThemeUtils.getThemedToolbarIconTintForActivityState(
                        context, brandedColorScheme, mIsTopResumedActivity)
                : iconTint;
    }

    private @ColorInt int resolveThemeColor() {
        if (shouldUseDefaultThemeForWebApp()) {
            return getDefaultColor();
        }

        return mBrowserServicesThemeColorProvider.getThemeColor();
    }

    private @BrandedColorScheme int getColorScheme(@ColorInt int color) {
        if (shouldUseDefaultThemeForWebApp()) {
            return ColorUtils.shouldUseLightForegroundOnBackground(color)
                    ? BrandedColorScheme.DARK_BRANDED_THEME
                    : BrandedColorScheme.LIGHT_BRANDED_THEME;
        }

        return mBrowserServicesThemeColorProvider.getBrandedColorScheme();
    }

    private ColorStateList resolveTint(@BrandedColorScheme int brandedColorScheme) {
        if (shouldUseDefaultThemeForWebApp()) {
            return ThemeUtils.getThemedToolbarIconTint(mContext, brandedColorScheme);
        }

        return mBrowserServicesThemeColorProvider.getTint();
    }

    private int getDefaultColor() {
        return SurfaceColorUpdateUtils.getDefaultThemeColor(
                mContext, mIntentDataProvider.getCustomTabMode() == CustomTabProfileType.INCOGNITO);
    }

    private boolean shouldUseDefaultThemeForWebApp() {
        // In desktop windowing CCT toolbar ideally should be visible only when web app is out of
        // scope. In such case web app header is a main customizable element and CCT toolbar
        // should follow default system to not merge with header.
        return WebAppHeaderUtils.isMinimalUiVisible(
                mIntentDataProvider, mDesktopWindowStateManager);
    }

    @Override
    public void onTopResumedActivityChanged(boolean isTopResumedActivity) {
        mIsTopResumedActivity = isTopResumedActivity;
        updateTint();
    }

    @VisibleForTesting
    DesktopWindowStateManager.AppHeaderObserver getAppHeaderObserver() {
        return mHeaderObserver;
    }
}
