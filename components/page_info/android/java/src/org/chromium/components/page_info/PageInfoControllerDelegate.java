// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.page_info;

import android.app.Activity;
import android.graphics.drawable.Drawable;
import android.view.ViewGroup;

import androidx.annotation.IntDef;
import androidx.fragment.app.FragmentManager;

import org.chromium.base.Callback;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.components.browser_ui.site_settings.SiteSettingsDelegate;
import org.chromium.components.browser_ui.site_settings.Website;
import org.chromium.components.content_settings.CookieControlsBridge;
import org.chromium.components.content_settings.CookieControlsObserver;
import org.chromium.components.omnibox.AutocompleteSchemeClassifier;
import org.chromium.content_public.browser.BrowserContextHandle;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.url.GURL;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.Collection;
import java.util.function.Consumer;

/** Provides embedder-level information to PageInfoController. */
@NullMarked
public abstract class PageInfoControllerDelegate {
    @IntDef({
        OfflinePageState.NOT_OFFLINE_PAGE,
        OfflinePageState.TRUSTED_OFFLINE_PAGE,
        OfflinePageState.UNTRUSTED_OFFLINE_PAGE
    })
    @Retention(RetentionPolicy.SOURCE)
    public @interface OfflinePageState {
        int NOT_OFFLINE_PAGE = 1;
        int TRUSTED_OFFLINE_PAGE = 2;
        int UNTRUSTED_OFFLINE_PAGE = 3;
    }

    private final AutocompleteSchemeClassifier mAutocompleteSchemeClassifier;
    private final boolean mIsSiteSettingsAvailable;
    private final boolean mCookieControlsShown;
    protected @OfflinePageState int mOfflinePageState;
    protected boolean mIsHttpsImageCompressionApplied;
    protected @Nullable String mOfflinePageUrl;

    public PageInfoControllerDelegate(
            AutocompleteSchemeClassifier autocompleteSchemeClassifier,
            boolean isSiteSettingsAvailable,
            boolean cookieControlsShown) {
        mAutocompleteSchemeClassifier = autocompleteSchemeClassifier;
        mIsSiteSettingsAvailable = isSiteSettingsAvailable;
        mCookieControlsShown = cookieControlsShown;
        mIsHttpsImageCompressionApplied = false;

        // These sometimes get overwritten by derived classes.
        mOfflinePageState = OfflinePageState.NOT_OFFLINE_PAGE;
        mOfflinePageUrl = null;
    }

    /** Creates an AutoCompleteClassifier. */
    public AutocompleteSchemeClassifier createAutocompleteSchemeClassifier() {
        return mAutocompleteSchemeClassifier;
    }

    /** Whether cookie controls should be shown in Page Info UI. */
    public boolean cookieControlsShown() {
        return mCookieControlsShown;
    }

    /** Return the ModalDialogManager to be used. */
    public abstract ModalDialogManager getModalDialogManager();

    /** Returns whether LiteMode https image compression was applied on this page */
    public boolean isHttpsImageCompressionApplied() {
        return mIsHttpsImageCompressionApplied;
    }

    /** Gets the Url of the offline page being shown if any. Returns null otherwise. */
    public @Nullable String getOfflinePageUrl() {
        return mOfflinePageUrl;
    }

    /** Whether the page being shown is an offline page. */
    public boolean isShowingOfflinePage() {
        return mOfflinePageState != OfflinePageState.NOT_OFFLINE_PAGE;
    }

    /** Whether the page being shown is a paint preview. */
    public boolean isShowingPaintPreviewPage() {
        return false;
    }

    /** Return the type of the pdf page. Return 0 if not a pdf page. */
    public int getPdfPageType() {
        return 0;
    }

    /**
     * Initialize viewParams with Offline Page UI info, if any.
     *
     * @param viewParams The PageInfoView.Params to set state on.
     * @param runAfterDismiss Used to set "open Online" button callback for offline page.
     */
    public void initOfflinePageUiParams(
            PageInfoView.Params viewParams, Consumer<Runnable> runAfterDismiss) {
        viewParams.openOnlineButtonShown = false;
    }

    /**
     * Return the connection message shown for an offline page, if appropriate. Returns null if
     * there's no offline page.
     */
    public @Nullable String getOfflinePageConnectionMessage() {
        return null;
    }

    /**
     * Return the connection message shown for a paint preview page, if appropriate. Returns null if
     * there's no paint preview page.
     */
    public @Nullable String getPaintPreviewPageConnectionMessage() {
        return null;
    }

    /**
     * Return the connection message shown for a pdf page, if appropriate. Returns null if there's
     * no pdf page.
     */
    public @Nullable String getPdfPageConnectionMessage() {
        return null;
    }

    /** Whether Site settings are available. */
    public boolean isSiteSettingsAvailable() {
        return mIsSiteSettingsAvailable;
    }

    /** Show cookie settings. */
    public abstract void showCookieSettings();

    /** Show Tracking Protection settings. */
    public abstract void showTrackingProtectionSettings();

    /** Show Incognito tracking protections settings. */
    public abstract void showIncognitoTrackingProtectionsSettings();

    /**
     * Show site settings for the current page.
     *
     * @param currentSite Website containing data about the site the PageInfo bubble is shown for.
     */
    public abstract void showSiteSettings(Website currentSite);

    /**
     * Shows cookie feedback UI.
     *
     * @param activity The Activity where the feedback is shown.
     */
    public abstract void showCookieFeedback(Activity activity);

    /** Show ad personalization settings. */
    public abstract void showAdPersonalizationSettings();

    /**
     * Creates Cookie Controls Bridge.
     *
     * @param observer The CookieControlsObserver to create the bridge with.
     * @return the object that facilitates interfacing with native code.
     */
    public abstract CookieControlsBridge createCookieControlsBridge(
            CookieControlsObserver observer);

    /**
     * Allows the delegate to insert additional {@link PageInfoRowView} views.
     *
     * @return a collection of controllers corresponding to these views.
     */
    public abstract Collection<PageInfoSubpageController> createAdditionalRowViews(
            PageInfoMainController mainController, @Nullable ViewGroup rowWrapper);

    /**
     * @return Returns the browser context associated with this dialog.
     */
    public abstract BrowserContextHandle getBrowserContext();

    /**
     * @return Returns the SiteSettingsDelegate for this page info.
     */
    public abstract SiteSettingsDelegate getSiteSettingsDelegate();

    /**
     * Fetches a favicon for the current page and passes it to callback.
     * The UI will use a fallback icon if null is supplied.
     */
    public abstract void getFavicon(GURL url, Callback<Drawable> callback);

    /**
     * Checks to see that touch exploration or an accessibility service that can perform gestures
     * is enabled.
     * @return Whether or not accessibility and touch exploration are enabled.
     */
    public abstract boolean isAccessibilityEnabled();

    public abstract FragmentManager getFragmentManager();

    public abstract boolean isIncognito();

    /**
     * @return Whether the Tracking Protection UI should be shown instead of the cookies one.
     */
    public abstract boolean showTrackingProtectionUi();

    /**
     * @return Whether all 3PCs are blocked when Tracking Protection is on.
     */
    public abstract boolean allThirdPartyCookiesBlockedTrackingProtection();
}
