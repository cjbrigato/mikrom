// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.toolbar;

import android.app.Activity;
import android.os.Handler;
import android.view.View;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import org.chromium.base.BuildInfo;
import org.chromium.base.TraceEvent;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.Supplier;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.bookmarks.PowerBookmarkUtils;
import org.chromium.chrome.browser.commerce.ShoppingServiceFactory;
import org.chromium.chrome.browser.download.DownloadUtils;
import org.chromium.chrome.browser.feature_engagement.TrackerFactory;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.lifecycle.PauseResumeWithNativeObserver;
import org.chromium.chrome.browser.offlinepages.OfflinePageBridge;
import org.chromium.chrome.browser.pdf.PdfPage;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.screenshot_monitor.ScreenshotMonitor;
import org.chromium.chrome.browser.screenshot_monitor.ScreenshotMonitorDelegate;
import org.chromium.chrome.browser.screenshot_monitor.ScreenshotMonitorImpl;
import org.chromium.chrome.browser.screenshot_monitor.ScreenshotTabObserver;
import org.chromium.chrome.browser.tab.CurrentTabObserver;
import org.chromium.chrome.browser.tab.EmptyTabObserver;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.toolbar.adaptive.AdaptiveToolbarFeatures;
import org.chromium.chrome.browser.translate.TranslateBridge;
import org.chromium.chrome.browser.translate.TranslateUtils;
import org.chromium.chrome.browser.ui.appmenu.AppMenuCoordinator;
import org.chromium.chrome.browser.ui.appmenu.AppMenuHandler;
import org.chromium.chrome.browser.user_education.IphCommandBuilder;
import org.chromium.chrome.browser.user_education.UserEducationHelper;
import org.chromium.components.commerce.core.CommerceFeatureUtils;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.components.feature_engagement.EventConstants;
import org.chromium.components.feature_engagement.FeatureConstants;
import org.chromium.components.feature_engagement.Tracker;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.url.GURL;

/**
 * A helper class for IPH shown on the toolbar. TODO(crbug.com/40585866): Remove feature-specific
 * IPH from here.
 */
public class ToolbarButtonInProductHelpController
        implements ScreenshotMonitorDelegate, PauseResumeWithNativeObserver {
    private final CurrentTabObserver mPageLoadObserver;
    private final Activity mActivity;
    private final WindowAndroid mWindowAndroid;
    private final ActivityLifecycleDispatcher mLifecycleDispatcher;
    @Nullable private ScreenshotMonitor mScreenshotMonitor;
    private final View mMenuButtonAnchorView;
    private final AppMenuHandler mAppMenuHandler;
    private final UserEducationHelper mUserEducationHelper;
    private final Profile mProfile;
    private final Supplier<Tab> mCurrentTabSupplier;
    private final Supplier<Boolean> mIsInOverviewModeSupplier;

    /**
     * @param activity {@link Activity} on which this class runs.
     * @param windowAndroid {@link WindowAndroid} for the current Activity.
     * @param appMenuCoordinator {@link AppMenuCoordinator} whose visual state is to be updated
     *     accordingly.
     * @param lifecycleDispatcher {@link LifecycleDispatcher} that helps observe activity lifecycle.
     * @param profile The current Profile.
     * @param tabSupplier An observable supplier of the current {@link Tab}.
     * @param isInOverviewModeSupplier Supplies whether the app is in overview mode.
     * @param menuButtonAnchorView The menu button view to serve as an anchor.
     */
    public ToolbarButtonInProductHelpController(
            @NonNull Activity activity,
            @NonNull WindowAndroid windowAndroid,
            @NonNull AppMenuCoordinator appMenuCoordinator,
            @NonNull ActivityLifecycleDispatcher lifecycleDispatcher,
            @NonNull Profile profile,
            @NonNull ObservableSupplier<Tab> tabSupplier,
            @NonNull Supplier<Boolean> isInOverviewModeSupplier,
            @NonNull View menuButtonAnchorView) {
        mActivity = activity;
        mWindowAndroid = windowAndroid;
        mAppMenuHandler = appMenuCoordinator.getAppMenuHandler();
        mMenuButtonAnchorView = menuButtonAnchorView;
        mIsInOverviewModeSupplier = isInOverviewModeSupplier;
        mUserEducationHelper = new UserEducationHelper(mActivity, profile, new Handler());
        if (!BuildInfo.getInstance().isAutomotive) {
            mScreenshotMonitor = new ScreenshotMonitorImpl(this, mActivity);
        }
        mLifecycleDispatcher = lifecycleDispatcher;
        mLifecycleDispatcher.register(this);
        mProfile = profile;
        mCurrentTabSupplier = tabSupplier;
        mPageLoadObserver =
                new CurrentTabObserver(
                        tabSupplier,
                        new EmptyTabObserver() {
                            @Override
                            public void onPageLoadFinished(Tab tab, GURL url) {
                                // Part of scroll jank investigation http://crbug.com/1311003. Will
                                // remove TraceEvent after the investigation is complete.
                                try (TraceEvent te =
                                        TraceEvent.scoped(
                                                "ToolbarButtonInProductHelpController::onPageLoadFinished")) {
                                    if (tab.isShowingErrorPage()) {
                                        handleIphForErrorPageShown(tab);
                                        return;
                                    }

                                    handleIphForSuccessfulPageLoad(tab);
                                }
                            }

                            private void handleIphForSuccessfulPageLoad(final Tab tab) {
                                showDownloadPageTextBubble(
                                        tab, FeatureConstants.DOWNLOAD_PAGE_FEATURE);
                                showTranslateMenuButtonTextBubble(tab);
                                showPriceTrackingIph(tab);
                                showPageSummaryIph(tab);
                            }

                            private void handleIphForErrorPageShown(Tab tab) {
                                if (DeviceFormFactor.isWindowOnTablet(mWindowAndroid)) {
                                    return;
                                }

                                OfflinePageBridge bridge =
                                        OfflinePageBridge.getForProfile(tab.getProfile());
                                if (bridge == null
                                        || !bridge.isShowingDownloadButtonInErrorPage(
                                                tab.getWebContents())) {
                                    return;
                                }

                                Tracker tracker =
                                        TrackerFactory.getTrackerForProfile(
                                                Profile.fromWebContents(tab.getWebContents()));
                                tracker.notifyEvent(EventConstants.USER_HAS_SEEN_DINO);
                            }
                        },
                        /* swapCallback= */ null);
    }

    public void destroy() {
        mPageLoadObserver.destroy();
        mLifecycleDispatcher.unregister(this);
    }

    /**
     * Attempt to show the IPH for price tracking.
     *
     * @param tab The tab currently being displayed to the user.
     */
    private void showPriceTrackingIph(Tab tab) {
        if (tab == null || tab.getWebContents() == null) return;

        if (!CommerceFeatureUtils.isShoppingListEligible(
                        ShoppingServiceFactory.getForProfile(tab.getProfile()))
                || !PowerBookmarkUtils.isPriceTrackingEligible(tab)) {
            return;
        }

        mUserEducationHelper.requestShowIph(
                new IphCommandBuilder(
                                mActivity.getResources(),
                                FeatureConstants.SHOPPING_LIST_MENU_ITEM_FEATURE,
                                R.string.iph_price_tracking_menu_item,
                                R.string.iph_price_tracking_menu_item_accessibility)
                        .setAnchorView(mMenuButtonAnchorView)
                        .setOnShowCallback(
                                () ->
                                        turnOnHighlightForMenuItem(
                                                R.id.enable_price_tracking_menu_id))
                        .setOnDismissCallback(this::turnOffHighlightForMenuItem)
                        .build());
    }

    /** Attempts to show an IPH text bubble for download continuing. */
    public void showDownloadContinuingIph() {
        mUserEducationHelper.requestShowIph(
                new IphCommandBuilder(
                                mActivity.getResources(),
                                FeatureConstants.DOWNLOAD_INFOBAR_DOWNLOAD_CONTINUING_FEATURE,
                                R.string.iph_download_infobar_download_continuing_text,
                                R.string.iph_download_infobar_download_continuing_text)
                        .setAnchorView(mMenuButtonAnchorView)
                        .setOnShowCallback(() -> turnOnHighlightForMenuItem(R.id.downloads_menu_id))
                        .setOnDismissCallback(this::turnOffHighlightForMenuItem)
                        .build());
    }

    private void showPageSummaryIph(Tab tab) {
        if (tab == null || tab.getWebContents() == null || tab.getUrl() == null) return;

        if (!AdaptiveToolbarFeatures.isAdaptiveToolbarPageSummaryEnabled()) return;
        Profile currentProfile = tab.getProfile();
        Tracker tracker = TrackerFactory.getTrackerForProfile(currentProfile);
        if (!tracker.isInitialized()) return;

        var isTabPdf = tab.getNativePage() != null && tab.getNativePage() instanceof PdfPage;
        var isTabHttp = UrlUtilities.isHttpOrHttps(tab.getUrl());

        if (!isTabHttp && !isTabPdf) return;

        String menuItemIphFeature =
                isTabPdf
                        ? FeatureConstants.PAGE_SUMMARY_PDF_MENU_FEATURE
                        : FeatureConstants.PAGE_SUMMARY_WEB_MENU_FEATURE;
        String toolbarIphFeature =
                isTabPdf
                        ? FeatureConstants
                                .ADAPTIVE_BUTTON_IN_TOP_TOOLBAR_CUSTOMIZATION_PAGE_SUMMARY_PDF_FEATURE
                        : FeatureConstants
                                .ADAPTIVE_BUTTON_IN_TOP_TOOLBAR_CUSTOMIZATION_PAGE_SUMMARY_WEB_FEATURE;
        var stringId =
                isTabPdf
                        ? R.string.adaptive_toolbar_button_review_pdf_iph
                        : R.string.adaptive_toolbar_button_page_summary_iph;
        var menuItemId = isTabPdf ? R.id.ai_pdf_menu_id : R.id.ai_web_menu_id;

        if (tracker.hasEverTriggered(toolbarIphFeature, false)) return;

        mUserEducationHelper.requestShowIph(
                new IphCommandBuilder(
                                mActivity.getResources(), menuItemIphFeature, stringId, stringId)
                        .setAnchorView(mMenuButtonAnchorView)
                        .setOnShowCallback(() -> turnOnHighlightForMenuItem(menuItemId))
                        .setOnDismissCallback(this::turnOffHighlightForMenuItem)
                        .build());
    }

    /** Attempts to show an IPH text bubble for those that trigger on a cold start. */
    public void showColdStartIph() {
        showAddToGroupIph();
        showDownloadHomeIph();
    }

    // Overridden public methods.
    @Override
    public void onResumeWithNative() {
        // Part of the (more runtime-related) check to determine whether to trigger help UI is
        // left until onScreenshotTaken() since it is less expensive to keep monitoring on and
        // check when the help UI is accessed than it is to start/stop monitoring per tab change
        // (e.g. tab switch or in overview mode).
        if (DeviceFormFactor.isWindowOnTablet(mWindowAndroid)) return;
        if (mScreenshotMonitor != null) mScreenshotMonitor.startMonitoring();
    }

    @Override
    public void onPauseWithNative() {
        if (mScreenshotMonitor != null) mScreenshotMonitor.stopMonitoring();
    }

    @Override
    public void onScreenshotTaken() {
        Tab currentTab = mCurrentTabSupplier.get();
        Profile currentProfile = currentTab != null ? currentTab.getProfile() : mProfile;

        Tracker tracker = TrackerFactory.getTrackerForProfile(currentProfile);
        tracker.notifyEvent(EventConstants.SCREENSHOT_TAKEN_CHROME_IN_FOREGROUND);

        if (currentTab == null) return;

        PostTask.postTask(
                TaskTraits.UI_DEFAULT,
                () -> {
                    if (currentTab != mCurrentTabSupplier.get()) return;
                    showDownloadPageTextBubble(
                            currentTab, FeatureConstants.DOWNLOAD_PAGE_SCREENSHOT_FEATURE);
                    ScreenshotTabObserver tabObserver = ScreenshotTabObserver.from(currentTab);
                    if (tabObserver != null) tabObserver.onScreenshotTaken();
                });
    }

    private void showDownloadHomeIph() {
        mUserEducationHelper.requestShowIph(
                new IphCommandBuilder(
                                mActivity.getResources(),
                                FeatureConstants.DOWNLOAD_HOME_FEATURE,
                                R.string.iph_download_home_text,
                                R.string.iph_download_home_accessibility_text)
                        .setAnchorView(mMenuButtonAnchorView)
                        .setOnShowCallback(() -> turnOnHighlightForMenuItem(R.id.downloads_menu_id))
                        .setOnDismissCallback(this::turnOffHighlightForMenuItem)
                        .build());
    }

    private void showAddToGroupIph() {
        if (ChromeFeatureList.sTabGroupParityBottomSheetAndroid.isEnabled()) {
            mUserEducationHelper.requestShowIph(
                    new IphCommandBuilder(
                                    mActivity.getResources(),
                                    FeatureConstants.MENU_ADD_TO_GROUP,
                                    R.string.tab_switcher_add_to_group_iph,
                                    R.string.tab_switcher_add_to_group_iph)
                            .setAnchorView(mMenuButtonAnchorView)
                            .setOnShowCallback(
                                    () -> turnOnHighlightForMenuItem(R.id.add_to_group_menu_id))
                            .setOnDismissCallback(this::turnOffHighlightForMenuItem)
                            .build());
        }
    }

    /**
     * Show the download page in-product-help bubble. Also used by download page screenshot IPH.
     *
     * @param tab The current tab.
     */
    private void showDownloadPageTextBubble(final Tab tab, String featureName) {
        if (tab == null) return;
        if (DeviceFormFactor.isWindowOnTablet(mWindowAndroid)
                || (mIsInOverviewModeSupplier.get() != null && mIsInOverviewModeSupplier.get())
                || !DownloadUtils.isAllowedToDownloadPage(tab)) {
            return;
        }

        mUserEducationHelper.requestShowIph(
                new IphCommandBuilder(
                                mActivity.getResources(),
                                featureName,
                                R.string.iph_download_page_for_offline_usage_text,
                                R.string.iph_download_page_for_offline_usage_accessibility_text)
                        .setOnShowCallback(() -> turnOnHighlightForMenuItem(R.id.offline_page_id))
                        .setOnDismissCallback(this::turnOffHighlightForMenuItem)
                        .setAnchorView(mMenuButtonAnchorView)
                        .build());
        // Record metrics if we show Download IPH after a screenshot of the page.
        ScreenshotTabObserver tabObserver = ScreenshotTabObserver.from(tab);
        if (tabObserver != null) {
            tabObserver.onActionPerformedAfterScreenshot(
                    ScreenshotTabObserver.SCREENSHOT_ACTION_DOWNLOAD_IPH);
        }
    }

    /**
     * Show the translate manual trigger in-product-help bubble.
     * @param tab The current tab.
     */
    private void showTranslateMenuButtonTextBubble(final Tab tab) {
        if (tab == null) return;
        if (!TranslateUtils.canTranslateCurrentTab(tab)
                || !TranslateBridge.shouldShowManualTranslateIph(tab)) {
            return;
        }

        mUserEducationHelper.requestShowIph(
                new IphCommandBuilder(
                                mActivity.getResources(),
                                FeatureConstants.TRANSLATE_MENU_BUTTON_FEATURE,
                                R.string.iph_translate_menu_button_text,
                                R.string.iph_translate_menu_button_accessibility_text)
                        .setOnShowCallback(() -> turnOnHighlightForMenuItem(R.id.translate_id))
                        .setOnDismissCallback(this::turnOffHighlightForMenuItem)
                        .setAnchorView(mMenuButtonAnchorView)
                        .build());
    }

    private void turnOnHighlightForMenuItem(Integer highlightMenuItemId) {
        if (mAppMenuHandler != null) {
            mAppMenuHandler.setMenuHighlight(highlightMenuItemId);
        }
    }

    private void turnOffHighlightForMenuItem() {
        if (mAppMenuHandler != null) {
            mAppMenuHandler.clearMenuHighlight();
        }
    }
}
