// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.customtabs;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ResolveInfo;
import android.net.Uri;
import android.text.TextUtils;

import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.CallbackUtils;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.supplier.Supplier;
import org.chromium.blink.mojom.DisplayMode;
import org.chromium.blink.mojom.DisplayMode.EnumType;
import org.chromium.cc.input.BrowserControlsState;
import org.chromium.chrome.browser.app.tab_activity_glue.ActivityTabWebContentsDelegateAndroid;
import org.chromium.chrome.browser.browser_controls.BrowserControlsStateProvider;
import org.chromium.chrome.browser.browserservices.intents.BrowserServicesIntentDataProvider;
import org.chromium.chrome.browser.browserservices.intents.WebappExtras;
import org.chromium.chrome.browser.browserservices.permissiondelegation.InstalledWebappPermissionManager;
import org.chromium.chrome.browser.browserservices.ui.controller.AuthTabVerifier;
import org.chromium.chrome.browser.browserservices.ui.controller.Verifier;
import org.chromium.chrome.browser.compositor.CompositorViewHolder;
import org.chromium.chrome.browser.contextmenu.ChromeContextMenuPopulator;
import org.chromium.chrome.browser.contextmenu.ChromeContextMenuPopulatorFactory;
import org.chromium.chrome.browser.ephemeraltab.EphemeralTabCoordinator;
import org.chromium.chrome.browser.externalnav.ExternalNavigationDelegateImpl;
import org.chromium.chrome.browser.flags.ActivityType;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.fullscreen.BrowserControlsManager;
import org.chromium.chrome.browser.fullscreen.FullscreenManager;
import org.chromium.chrome.browser.init.ChromeActivityNativeDelegate;
import org.chromium.chrome.browser.native_page.NativePageFactory;
import org.chromium.chrome.browser.pdf.PdfInfo;
import org.chromium.chrome.browser.share.ShareDelegate;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabAssociatedApp;
import org.chromium.chrome.browser.tab.TabContextMenuItemDelegate;
import org.chromium.chrome.browser.tab.TabDelegateFactory;
import org.chromium.chrome.browser.tab.TabStateBrowserControlsVisibilityDelegate;
import org.chromium.chrome.browser.tab.TabWebContentsDelegateAndroid;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;
import org.chromium.chrome.browser.ui.native_page.NativePage;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.components.browser_ui.util.BrowserControlsVisibilityDelegate;
import org.chromium.components.browser_ui.util.ComposedBrowserControlsVisibilityDelegate;
import org.chromium.components.embedder_support.contextmenu.ContextMenuPopulatorFactory;
import org.chromium.components.embedder_support.delegate.WebContentsDelegateAndroid;
import org.chromium.components.embedder_support.util.Origin;
import org.chromium.components.embedder_support.util.UrlConstants;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.components.external_intents.ExternalNavigationHandler;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.url.GURL;

import java.util.List;

/**
 * A {@link TabDelegateFactory} class to be used in all {@link Tab} owned by a {@link
 * CustomTabActivity}.
 */
public class CustomTabDelegateFactory implements TabDelegateFactory {

    /** A custom external navigation delegate that forbids the intent picker from showing up. */
    static class CustomTabNavigationDelegate extends ExternalNavigationDelegateImpl {
        private static final String TAG = "customtabs";
        private final String mClientPackageName;
        private final Verifier mVerifier;
        private final @ActivityType int mActivityType;
        private final BrowserServicesIntentDataProvider mIntentDataProvider;
        private final AuthTabVerifier mAuthTabVerifier;

        /** Constructs a new instance of {@link CustomTabNavigationDelegate}. */
        CustomTabNavigationDelegate(
                Tab tab,
                Verifier verifier,
                @ActivityType int activityType,
                BrowserServicesIntentDataProvider intentDataProvider,
                AuthTabVerifier authTabVerifier) {
            super(tab);
            mClientPackageName = TabAssociatedApp.from(tab).getAppId();
            mVerifier = verifier;
            mActivityType = activityType;
            mIntentDataProvider = intentDataProvider;
            mAuthTabVerifier = authTabVerifier;
        }

        @Override
        public void setPackageForTrustedCallingApp(Intent intent) {
            assert !TextUtils.isEmpty(mClientPackageName);
            intent.setPackage(mClientPackageName);
        }

        @Override
        public boolean shouldAvoidDisambiguationDialog(GURL intentDataUrl) {
            // Don't show the disambiguation dialog if Chrome could handle the intent.
            return UrlUtilities.isAcceptedScheme(intentDataUrl);
        }

        @Override
        public boolean isForTrustedCallingApp(Supplier<List<ResolveInfo>> resolveInfoSupplier) {
            if (TextUtils.isEmpty(mClientPackageName)) return false;

            return ExternalNavigationHandler.resolveInfoContainsPackage(
                    resolveInfoSupplier.get(), mClientPackageName);
        }

        @Override
        public boolean shouldDisableExternalIntentRequestsForUrl(GURL url) {
            // http://crbug.com/647569 : Do not forward URL requests to external intents for URLs
            // within the Webapp/TWA's scope.
            // TODO(crbug.com/40549331): Migrate verifier hierarchy to GURL.
            return mVerifier != null && mVerifier.shouldIgnoreExternalIntentHandlers(url.getSpec());
        }

        @Override
        public boolean shouldDisableAllExternalIntents() {
            return mActivityType == ActivityType.AUTH_TAB
                    && ChromeFeatureList.sCctAuthTabDisableAllExternalIntents.isEnabled();
        }

        @Override
        public boolean shouldReturnAsActivityResult(GURL url) {
            if (mActivityType != ActivityType.AUTH_TAB) return false;

            var authTabVerifier = mAuthTabVerifier;
            return authTabVerifier.isCustomScheme(url)
                    || authTabVerifier.shouldRedirectHttpsAuthUrl(url);
        }

        @Override
        public void returnAsActivityResult(GURL url) {
            assert mIntentDataProvider.isAuthTab();
            mAuthTabVerifier.returnAsActivityResult(url);
        }

        @Override
        public void maybeRecordExternalNavigationSchemeHistogram(GURL url) {
            if (mIntentDataProvider.isAuthTab()) return;

            // Only record for Custom Tabs that we think are launched for auth purposes.
            Uri urlToLoad = Uri.parse(mIntentDataProvider.getUrlToLoad());
            if (!urlToLoad.isHierarchical()) return;

            String redirectUri = urlToLoad.getQueryParameter("redirect_uri");
            if (TextUtils.isEmpty(redirectUri)) return;

            int schemeEnum = CustomTabAuthUrlHeuristics.getAuthSchemeEnum(url.getScheme());
            RecordHistogram.recordEnumeratedHistogram(
                    "CustomTabs.AuthTab.ExternalNavigationScheme",
                    schemeEnum,
                    CustomTabAuthUrlHeuristics.AuthScheme.COUNT);
        }

        public void resumeDelayedVerificationForTesting() {
            mAuthTabVerifier.onFinishNativeInitialization();
        }
    }

    private static class CustomTabWebContentsDelegate
            extends ActivityTabWebContentsDelegateAndroid {
        private static final String TAG = "CustomTabWebContentsDelegate";

        private final Activity mActivity;
        private final @ActivityType int mActivityType;
        private final @Nullable String mWebApkScopeUrl;
        private final @Nullable BrowserServicesIntentDataProvider mIntentDataProvider;
        private final @DisplayMode.EnumType int mDisplayMode;
        private final boolean mShouldEnableEmbeddedMediaExperience;
        private final Supplier<Boolean> mHeaderControlsVisibilitySupplier;

        /** See {@link TabWebContentsDelegateAndroid}. */
        public CustomTabWebContentsDelegate(
                Tab tab,
                Activity activity,
                @ActivityType int activityType,
                @Nullable String webApkScopeUrl,
                @Nullable BrowserServicesIntentDataProvider intentDataProvider,
                @EnumType int displayMode,
                boolean shouldEnableEmbeddedMediaExperience,
                ChromeActivityNativeDelegate chromeActivityNativeDelegate,
                boolean isCustomTab,
                BrowserControlsStateProvider browserControlsStateProvider,
                FullscreenManager fullscreenManager,
                TabCreatorManager tabCreatorManager,
                Supplier<TabModelSelector> tabModelSelectorSupplier,
                Supplier<CompositorViewHolder> compositorViewHolderSupplier,
                Supplier<ModalDialogManager> modalDialogManagerSupplier,
                Supplier<Boolean> headerControlsVisibilitySupplier) {
            super(
                    tab,
                    activity,
                    chromeActivityNativeDelegate,
                    isCustomTab,
                    browserControlsStateProvider,
                    fullscreenManager,
                    tabCreatorManager,
                    tabModelSelectorSupplier,
                    compositorViewHolderSupplier,
                    modalDialogManagerSupplier);
            mActivity = activity;
            mActivityType = activityType;
            mWebApkScopeUrl = webApkScopeUrl;
            mIntentDataProvider = intentDataProvider;
            mDisplayMode = displayMode;
            mShouldEnableEmbeddedMediaExperience = shouldEnableEmbeddedMediaExperience;
            mHeaderControlsVisibilitySupplier = headerControlsVisibilitySupplier;
        }

        @Override
        public boolean shouldResumeRequestsForCreatedWindow() {
            return true;
        }

        @Override
        protected void bringActivityToForeground() {
            ((ActivityManager) mActivity.getSystemService(Context.ACTIVITY_SERVICE))
                    .moveTaskToFront(mActivity.getTaskId(), 0);
        }

        @Override
        protected boolean shouldEnableEmbeddedMediaExperience() {
            return mShouldEnableEmbeddedMediaExperience;
        }

        @Override
        public boolean isTrustedWebActivity(WebContents webContents) {
            // Note that `shouldIgnore` simply checks if `webContents` has an origin that the TWA
            // considers to be trusted.  This is a weaker check than `verify()`, but is also
            // synchronous.  For now, this is a good trade since we're only used for deciding if
            // unmuted autoplay should be allowed without a user gesture.
            GURL url = webContents.getLastCommittedUrl();
            return url != null
                    && mIntentDataProvider != null
                    && mIntentDataProvider.isTrustedWebActivity()
                    && mIntentDataProvider
                            .getAllTrustedWebActivityOrigins()
                            .contains(Origin.create(url.getSpec()));
        }

        @Override
        public @DisplayMode.EnumType int getDisplayMode() {
            // Depending on the customizable caption bar area, minimal UI controls may not
            // be supported even if the resolved display mode is `minimal-ui`. If the controls
            // are not visible it is inaccurate for the display mode to be `minimal-ui` so
            // we need to use `standalone`.
            if (mDisplayMode == DisplayMode.MINIMAL_UI
                    && !mHeaderControlsVisibilitySupplier.get()) {
                return DisplayMode.STANDALONE;
            }

            return mDisplayMode;
        }

        @Override
        protected String getManifestScope() {
            return mWebApkScopeUrl;
        }

        @Override
        public boolean canShowAppBanners() {
            return mActivityType == ActivityType.CUSTOM_TAB;
        }

        @Override
        protected boolean isInstalledWebappDelegateGeolocation() {
            if ((mActivity instanceof CustomTabActivity cctActivity) && cctActivity.isInTwaMode()) {
                // Whether the corresponding TWA client app enrolled in location delegation.
                return InstalledWebappPermissionManager.hasAndroidLocationPermission(
                                cctActivity.getTwaPackage())
                        != null;
            }
            return false;
        }
    }

    private final Activity mActivity;
    private final boolean mShouldHideBrowserControls;
    private final boolean mIsOpenedByChrome;
    private final @ActivityType int mActivityType;
    @Nullable private final String mWebApkScopeUrl;
    private final @Nullable BrowserServicesIntentDataProvider mIntentDataProvider;
    private final @DisplayMode.EnumType int mDisplayMode;
    private final boolean mShouldEnableEmbeddedMediaExperience;
    private final BrowserControlsVisibilityDelegate mBrowserStateVisibilityDelegate;
    private final Verifier mVerifier;
    private final ChromeActivityNativeDelegate mChromeActivityNativeDelegate;
    private final BrowserControlsStateProvider mBrowserControlsStateProvider;
    private final FullscreenManager mFullscreenManager;
    private final TabCreatorManager mTabCreatorManager;
    private final BrowserControlsManager mBrowserControlsManager;
    private final Supplier<TabModelSelector> mTabModelSelectorSupplier;
    private final Supplier<CompositorViewHolder> mCompositorViewHolderSupplier;
    private final Supplier<ModalDialogManager> mModalDialogManagerSupplier;
    // Should only be used after inflation.
    private final Supplier<SnackbarManager> mSnackbarManager;
    private final Supplier<ShareDelegate> mShareDelegateSupplier;
    // Should only be used after inflation.
    private final Supplier<BottomSheetController> mBottomSheetController;
    private final AuthTabVerifier mAuthTabVerifier;
    private final boolean mContextMenuEnabled;
    private final Supplier<Boolean> mHeaderControlsVisibilitySupplier;

    private TabWebContentsDelegateAndroid mWebContentsDelegateAndroid;
    private ExternalNavigationDelegateImpl mNavigationDelegate;
    private Supplier<EphemeralTabCoordinator> mEphemeralTabCoordinatorSupplier;

    /**
     * @param activity {@link Activity} instance.
     * @param shouldHideBrowserControls Whether or not the browser controls may auto-hide.
     * @param isOpenedByChrome Whether the CustomTab was originally opened by Chrome.
     * @param webApkScopeUrl The URL of the WebAPK web manifest scope. Null if the delegate is not
     *     for a WebAPK.
     * @param intentDataProvider Used to verify if an origin is trusted for TWAs.
     * @param displayMode The activity's display mode.
     * @param shouldEnableEmbeddedMediaExperience Whether embedded media experience is enabled.
     * @param visibilityDelegate The delegate that handles browser control visibility associated
     *     with browser actions (as opposed to tab state).
     * @param authUtils To determine whether apps are Google signed.
     * @param verifier Decides how to handle navigation to a new URL.
     * @param chromeActivityNativeDelegate Delegate for native initialziation.
     * @param browserControlsStateProvider Provides state of the browser controls.
     * @param fullscreenManager Manages the fullscreen state.
     * @param tabCreatorManager Manages the tab creators.
     * @param tabModelSelectorSupplier Supplies the tab model selector.
     * @param compositorViewHolderSupplier Supplies the compositor view holder.
     * @param modalDialogManagerSupplier Supplies the modal dialogManager.
     * @param snackbarManager Manages the snackbar.
     * @param shareDelegateSupplier Supplies the share delegate.
     * @param activityType The type of the current activity.
     * @param bottomSheetController Controls the bottom sheet.
     * @param contextMenuEnabled Whether the context menu will be enabled.
     * @param browserControlsManager Manages the browser controls.
     */
    public CustomTabDelegateFactory(
            Activity activity,
            @Nullable BrowserServicesIntentDataProvider intentDataProvider,
            BrowserControlsVisibilityDelegate visibilityDelegate,
            Verifier verifier,
            ChromeActivityNativeDelegate chromeActivityNativeDelegate,
            BrowserControlsStateProvider browserControlsStateProvider,
            FullscreenManager fullscreenManager,
            TabCreatorManager tabCreatorManager,
            Supplier<TabModelSelector> tabModelSelectorSupplier,
            Supplier<CompositorViewHolder> compositorViewHolderSupplier,
            Supplier<ModalDialogManager> modalDialogManagerSupplier,
            Supplier<SnackbarManager> snackbarManager,
            Supplier<ShareDelegate> shareDelegateSupplier,
            @ActivityType int activityType,
            Supplier<BottomSheetController> bottomSheetController,
            AuthTabVerifier authTabVerifier,
            BrowserControlsManager browserControlsManager,
            Supplier<Boolean> headerControlsVisibilitySupplier) {
        mIntentDataProvider = intentDataProvider;
        if (mIntentDataProvider != null) {
            mShouldHideBrowserControls = mIntentDataProvider.shouldEnableUrlBarHiding();
            mIsOpenedByChrome = mIntentDataProvider.isOpenedByChrome();
            mWebApkScopeUrl = getWebApkScopeUrl(mIntentDataProvider);
            mDisplayMode = mIntentDataProvider.getResolvedDisplayMode();
            mShouldEnableEmbeddedMediaExperience =
                    mIntentDataProvider.shouldEnableEmbeddedMediaExperience();
            mContextMenuEnabled = !mIntentDataProvider.isAuthTab();
        } else {
            // Default values for empty CustomTabDelegateFactory that won't be used.
            mShouldHideBrowserControls = false;
            mIsOpenedByChrome = false;
            mWebApkScopeUrl = null;
            mDisplayMode = DisplayMode.BROWSER;
            mShouldEnableEmbeddedMediaExperience = false;
            mContextMenuEnabled = false;
        }
        mActivity = activity;
        mBrowserStateVisibilityDelegate = visibilityDelegate;
        mVerifier = verifier;
        mChromeActivityNativeDelegate = chromeActivityNativeDelegate;
        mBrowserControlsStateProvider = browserControlsStateProvider;
        mFullscreenManager = fullscreenManager;
        mTabCreatorManager = tabCreatorManager;
        mTabModelSelectorSupplier = tabModelSelectorSupplier;
        mCompositorViewHolderSupplier = compositorViewHolderSupplier;
        mModalDialogManagerSupplier = modalDialogManagerSupplier;
        mSnackbarManager = snackbarManager;
        mShareDelegateSupplier = shareDelegateSupplier;
        mActivityType = activityType;
        mBottomSheetController = bottomSheetController;
        mAuthTabVerifier = authTabVerifier;
        mBrowserControlsManager = browserControlsManager;
        mHeaderControlsVisibilitySupplier = headerControlsVisibilitySupplier;
    }

    /**
     * Creates a basic/empty {@link TabDelegateFactory} for use when creating a hidden tab. It will
     * be replaced when the hidden Tab becomes shown.
     */
    public static CustomTabDelegateFactory createEmpty() {
        return new CustomTabDelegateFactory(
                null,
                null,
                null,
                null,
                null,
                null,
                null,
                null,
                () -> null,
                () -> null,
                () -> null,
                null,
                null,
                ActivityType.CUSTOM_TAB,
                null,
                null,
                null,
                () -> false);
    }

    @Override
    public BrowserControlsVisibilityDelegate createBrowserControlsVisibilityDelegate(Tab tab) {
        TabStateBrowserControlsVisibilityDelegate tabDelegate =
                new TabStateBrowserControlsVisibilityDelegate(tab) {
                    @Override
                    protected int calculateVisibilityConstraints() {
                        @BrowserControlsState
                        int constraints = super.calculateVisibilityConstraints();
                        if (constraints == BrowserControlsState.BOTH
                                && !mShouldHideBrowserControls) {
                            return BrowserControlsState.SHOWN;
                        }
                        return constraints;
                    }
                };

        // mBrowserStateVisibilityDelegate == null for background tabs for which
        // fullscreen state info from BrowserStateVisibilityDelegate is not available.
        return mBrowserStateVisibilityDelegate == null
                ? tabDelegate
                : new ComposedBrowserControlsVisibilityDelegate(
                        tabDelegate, mBrowserStateVisibilityDelegate);
    }

    @Override
    public TabWebContentsDelegateAndroid createWebContentsDelegate(Tab tab) {
        mWebContentsDelegateAndroid =
                new CustomTabWebContentsDelegate(
                        tab,
                        mActivity,
                        mActivityType,
                        mWebApkScopeUrl,
                        mIntentDataProvider,
                        mDisplayMode,
                        mShouldEnableEmbeddedMediaExperience,
                        mChromeActivityNativeDelegate,
                        /* isCustomTab= */ true,
                        mBrowserControlsStateProvider,
                        mFullscreenManager,
                        mTabCreatorManager,
                        mTabModelSelectorSupplier,
                        mCompositorViewHolderSupplier,
                        mModalDialogManagerSupplier,
                        mHeaderControlsVisibilitySupplier);
        return mWebContentsDelegateAndroid;
    }

    @Override
    public ExternalNavigationHandler createExternalNavigationHandler(Tab tab) {
        if (mIsOpenedByChrome) {
            mNavigationDelegate = new ExternalNavigationDelegateImpl(tab);
        } else {
            mNavigationDelegate =
                    new CustomTabNavigationDelegate(
                            tab, mVerifier, mActivityType, mIntentDataProvider, mAuthTabVerifier);
        }
        return new ExternalNavigationHandler(mNavigationDelegate);
    }

    @VisibleForTesting
    TabContextMenuItemDelegate createTabContextMenuItemDelegate(Tab tab) {
        TabModelSelector tabModelSelector = mTabModelSelectorSupplier.get();
        return new TabContextMenuItemDelegate(
                mActivity,
                tab,
                tabModelSelector,
                mEphemeralTabCoordinatorSupplier,
                CallbackUtils.emptyRunnable(),
                () -> mSnackbarManager.get(),
                () -> mBottomSheetController.get());
    }

    @Override
    public ContextMenuPopulatorFactory createContextMenuPopulatorFactory(Tab tab) {
        if (!mContextMenuEnabled) return null;

        @ChromeContextMenuPopulator.ContextMenuMode
        int contextMenuMode = getContextMenuMode(mIntentDataProvider, mActivityType);
        return new ChromeContextMenuPopulatorFactory(
                createTabContextMenuItemDelegate(tab), mShareDelegateSupplier, contextMenuMode);
    }

    @Override
    public NativePage createNativePage(
            String url, NativePage candidatePage, Tab tab, PdfInfo pdfInfo) {
        // Navigation comes from user pressing "Back to safety" on an interstitial so close the tab.
        // See crbug.com/1270695
        if (UrlConstants.NTP_URL.equals(url) && tab.isShowingErrorPage()) {
            mActivity.finish();
        }

        return NativePageFactory.createNativePageForCustomTab(
                url,
                candidatePage,
                tab,
                pdfInfo,
                mBrowserControlsManager,
                mTabModelSelectorSupplier.get(),
                mActivity);
    }

    public void setEphemeralTabCoordinatorSupplier(Supplier<EphemeralTabCoordinator> supplier) {
        mEphemeralTabCoordinatorSupplier = supplier;
    }

    /**
     * @return The {@link CustomTabNavigationDelegate} in this tab. For test purpose only.
     */
    @VisibleForTesting
    ExternalNavigationDelegateImpl getExternalNavigationDelegate() {
        return mNavigationDelegate;
    }

    public WebContentsDelegateAndroid getWebContentsDelegate() {
        return mWebContentsDelegateAndroid;
    }

    /**
     * Gets the scope URL if the {@link BrowserServicesIntentDataProvider} is for a WebAPK. Returns
     * null otherwise.
     */
    private static @Nullable String getWebApkScopeUrl(
            BrowserServicesIntentDataProvider intentDataProvider) {
        if (!intentDataProvider.isWebApkActivity()) return null;

        WebappExtras webappExtras = intentDataProvider.getWebappExtras();
        return (webappExtras != null) ? webappExtras.scopeUrl : null;
    }

    private static boolean isWebappOrWebApk(@ActivityType int activityType) {
        return activityType == ActivityType.WEBAPP || activityType == ActivityType.WEB_APK;
    }

    private static @ChromeContextMenuPopulator.ContextMenuMode int getContextMenuMode(
            BrowserServicesIntentDataProvider intentDataProvider, @ActivityType int activityType) {
        if (intentDataProvider.hasTargetNetwork()) {
            return ChromeContextMenuPopulator.ContextMenuMode.NETWORK_BOUND_TAB;
        }
        return isWebappOrWebApk(activityType)
                ? ChromeContextMenuPopulator.ContextMenuMode.WEB_APP
                : ChromeContextMenuPopulator.ContextMenuMode.CUSTOM_TAB;
    }
}
