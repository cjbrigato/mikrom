// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.toolbar;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.inOrder;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentMatcher;
import org.mockito.InOrder;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.annotation.Config;

import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.Supplier;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.common.ChromeUrlConstants;
import org.chromium.chrome.browser.feature_engagement.TrackerFactory;
import org.chromium.chrome.browser.homepage.HomepageManager;
import org.chromium.chrome.browser.multiwindow.MultiInstanceManager;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tabmodel.TabCreator;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager;
import org.chromium.chrome.browser.toolbar.bottom.BottomControlsCoordinator;
import org.chromium.chrome.browser.ui.native_page.NativePage;
import org.chromium.components.feature_engagement.Tracker;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.ui.base.PageTransition;
import org.chromium.url.GURL;

/** Unit tests for ToolbarTabControllerImpl. */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class ToolbarTabControllerImplTest {
    private static class LoadUrlParamsMatcher implements ArgumentMatcher<LoadUrlParams> {
        final LoadUrlParams mLoadUrlParams;

        public LoadUrlParamsMatcher(LoadUrlParams loadUrlParams) {
            mLoadUrlParams = loadUrlParams;
        }

        @Override
        public boolean matches(LoadUrlParams argument) {
            return argument.getUrl().equals(mLoadUrlParams.getUrl())
                    && argument.getTransitionType() == mLoadUrlParams.getTransitionType();
        }
    }

    @Rule public final MockitoRule mMockitoRule = MockitoJUnit.rule();
    @Mock private Supplier<Tab> mTabSupplier;
    @Mock private Tab mTab;
    @Mock private Tab mTab2;
    @Mock private ObservableSupplier<BottomControlsCoordinator> mBottomControlsCoordinatorSupplier;
    @Mock private BottomControlsCoordinator mBottomControlsCoordinator;
    @Mock private Tracker mTracker;
    @Mock private Supplier<Tracker> mTrackerSupplier;
    @Mock private Runnable mRunnable;
    @Mock private Profile mProfile;
    @Mock private NativePage mNativePage;
    @Mock private Supplier<Tab> mActivityTabProvider;
    @Mock private TabCreatorManager mTabCreatorManager;
    @Mock private TabCreator mTabCreator;
    @Mock private MultiInstanceManager mMultiInstanceManager;

    private final GURL mGURL = new GURL("https://example.com");
    private ToolbarTabControllerImpl mToolbarTabController;

    @Before
    public void setUp() {
        doReturn(mTab).when(mTabSupplier).get();
        doReturn(mTab).when(mActivityTabProvider).get();
        doReturn(mProfile).when(mTab).getProfile();
        doReturn(mNativePage).when(mTab).getNativePage();
        doReturn(mTabCreator).when(mTabCreatorManager).getTabCreator(anyBoolean());
        doReturn(mGURL).when(mTab).getUrl();
        TrackerFactory.setTrackerForTests(mTracker);
        initToolbarTabController();
    }

    @Test
    public void backForward_NotTriggeredWhenTabCannot() {
        doReturn(false).when(mTab).canGoForward();
        doReturn(false).when(mTab).canGoBack();

        assertFalse(mToolbarTabController.forward());
        assertFalse(mToolbarTabController.back());
        verify(mNativePage, never()).notifyHidingWithBack();
    }

    @Test
    public void backForward_correspondingTabActionsTriggered() {
        doReturn(true).when(mTab).canGoForward();
        doReturn(true).when(mTab).canGoBack();

        assertTrue(mToolbarTabController.forward());
        assertTrue(mToolbarTabController.back());
        verify(mRunnable, times(2)).run();
        verify(mTab).goForward();
        verify(mTab).goBack();
    }

    @Test
    public void back_handledByBottomControls() {
        doReturn(mBottomControlsCoordinator).when(mBottomControlsCoordinatorSupplier).get();
        doReturn(true).when(mBottomControlsCoordinator).onBackPressed();
        Assert.assertTrue(mToolbarTabController.back());

        verify(mBottomControlsCoordinator).onBackPressed();
        verify(mRunnable, never()).run();
        verify(mTab, never()).goBack();
    }

    @Test
    public void back_notifyNativePageHiding() {
        doReturn(null).when(mBottomControlsCoordinatorSupplier).get();
        doReturn(true).when(mTab).canGoBack();

        mToolbarTabController.back();
        verify(mNativePage).notifyHidingWithBack();
    }

    @Test
    public void stopOrReloadCurrentTab() {
        doReturn(false).when(mTab).isLoading();
        mToolbarTabController.stopOrReloadCurrentTab(/* ignoreCache= */ false);

        verify(mTab).reload();
        verify(mRunnable).run();

        doReturn(true).when(mTab).isLoading();
        mToolbarTabController.stopOrReloadCurrentTab(/* ignoreCache= */ false);

        verify(mTab).stopLoading();
        verify(mRunnable, times(2)).run();
    }

    @Test
    public void stopOrReloadCurrentTab_ignoreCache() {
        doReturn(false).when(mTab).isLoading();

        mToolbarTabController.stopOrReloadCurrentTab(/* ignoreCache= */ true);

        verify(mTab).reloadIgnoringCache();
    }

    @Test
    public void openHomepage_loadsHomePage() {
        mToolbarTabController.openHomepage();
        GURL homePageGurl = HomepageManager.getInstance().getHomepageGurl();
        if (homePageGurl.isEmpty()) {
            homePageGurl = ChromeUrlConstants.nativeNtpGurl();
        }
        verify(mTab)
                .loadUrl(
                        argThat(
                                new LoadUrlParamsMatcher(
                                        new LoadUrlParams(
                                                homePageGurl, PageTransition.HOME_PAGE))));
    }

    @Test
    public void testUsingCorrectTabSupplier_doesNotUseRegularTabSupplier() {
        setUpUsingCorrectTabSupplier();

        Assert.assertFalse(mToolbarTabController.back());
        Assert.assertFalse(mToolbarTabController.canGoBack());
    }

    @Test
    public void testBackInForegroundTab() {
        // Set up.
        doReturn(true).when(mTab).canGoBack();
        doReturn(mTab2)
                .when(mTabCreator)
                .createTabWithHistory(mTab, TabLaunchType.FROM_HISTORY_NAVIGATION_FOREGROUND);
        InOrder inOrder = inOrder(mTabCreator, mTab2, mMultiInstanceManager);

        // Call backInNewTab with foregroundNewTab = true.
        mToolbarTabController.backInNewTab(/* foregroundNewTab= */ true);

        // Verify correctness.
        inOrder.verify(mTabCreator)
                .createTabWithHistory(mTab, TabLaunchType.FROM_HISTORY_NAVIGATION_FOREGROUND);
        inOrder.verify(mTab2).goBack();
        inOrder.verifyNoMoreInteractions();
    }

    @Test
    public void testBackInBackgroundTab() {
        // Set up.
        doReturn(true).when(mTab).canGoBack();
        doReturn(mTab2)
                .when(mTabCreator)
                .createTabWithHistory(mTab, TabLaunchType.FROM_HISTORY_NAVIGATION_BACKGROUND);
        InOrder inOrder = inOrder(mTabCreator, mTab2, mMultiInstanceManager);

        // Call backInNewTab with foregroundNewTab = false.
        mToolbarTabController.backInNewTab(/* foregroundNewTab= */ false);

        // Verify correctness.
        inOrder.verify(mTabCreator)
                .createTabWithHistory(mTab, TabLaunchType.FROM_HISTORY_NAVIGATION_BACKGROUND);
        inOrder.verify(mTab2).goBack();
        inOrder.verifyNoMoreInteractions();
    }

    @Test
    public void testBackInNewWindow() {
        // Set up.
        doReturn(true).when(mTab).canGoBack();
        doReturn(mTab2)
                .when(mTabCreator)
                .createTabWithHistory(mTab, TabLaunchType.FROM_HISTORY_NAVIGATION_BACKGROUND);
        InOrder inOrder = inOrder(mTabCreator, mTab2, mMultiInstanceManager);

        // Call backInNewWindow.
        mToolbarTabController.backInNewWindow();

        // Verify correctness.
        inOrder.verify(mTabCreator)
                .createTabWithHistory(mTab, TabLaunchType.FROM_HISTORY_NAVIGATION_BACKGROUND);
        inOrder.verify(mTab2).goBack();
        inOrder.verify(mMultiInstanceManager).moveTabToNewWindow(mTab2);
        inOrder.verifyNoMoreInteractions();
    }

    @Test
    public void testForwardInForegroundTab() {
        // Set up.
        doReturn(true).when(mTab).canGoForward();
        doReturn(mTab2)
                .when(mTabCreator)
                .createTabWithHistory(mTab, TabLaunchType.FROM_HISTORY_NAVIGATION_FOREGROUND);
        InOrder inOrder = inOrder(mTabCreator, mTab2, mMultiInstanceManager);

        // Call forwardInNewTab with foregroundNewTab = true.
        mToolbarTabController.forwardInNewTab(/* foregroundNewTab= */ true);

        // Verify correctness.
        inOrder.verify(mTabCreator)
                .createTabWithHistory(mTab, TabLaunchType.FROM_HISTORY_NAVIGATION_FOREGROUND);
        inOrder.verify(mTab2).goForward();
        inOrder.verifyNoMoreInteractions();
    }

    @Test
    public void testForwardInBackgroundTab() {
        // Set up.
        doReturn(true).when(mTab).canGoForward();
        doReturn(mTab2)
                .when(mTabCreator)
                .createTabWithHistory(mTab, TabLaunchType.FROM_HISTORY_NAVIGATION_BACKGROUND);
        InOrder inOrder = inOrder(mTabCreator, mTab2, mMultiInstanceManager);

        // Call forwardInNewTab with foregroundNewTab = false.
        mToolbarTabController.forwardInNewTab(/* foregroundNewTab= */ false);

        // Verify correctness.
        inOrder.verify(mTabCreator)
                .createTabWithHistory(mTab, TabLaunchType.FROM_HISTORY_NAVIGATION_BACKGROUND);
        inOrder.verify(mTab2).goForward();
        inOrder.verifyNoMoreInteractions();
    }

    @Test
    public void testForwardInNewWindow() {
        // Set up.
        doReturn(true).when(mTab).canGoForward();
        doReturn(mTab2)
                .when(mTabCreator)
                .createTabWithHistory(mTab, TabLaunchType.FROM_HISTORY_NAVIGATION_BACKGROUND);
        InOrder inOrder = inOrder(mTabCreator, mTab2, mMultiInstanceManager);

        // Call forwardInNewWindow.
        mToolbarTabController.forwardInNewWindow();

        // Verify correctness.
        inOrder.verify(mTabCreator)
                .createTabWithHistory(mTab, TabLaunchType.FROM_HISTORY_NAVIGATION_BACKGROUND);
        inOrder.verify(mTab2).goForward();
        inOrder.verify(mMultiInstanceManager).moveTabToNewWindow(mTab2);
        inOrder.verifyNoMoreInteractions();
    }

    private void initToolbarTabController() {
        mToolbarTabController =
                new ToolbarTabControllerImpl(
                        mTabSupplier,
                        mTrackerSupplier,
                        mBottomControlsCoordinatorSupplier,
                        ToolbarManager::homepageUrl,
                        mRunnable,
                        mActivityTabProvider,
                        mTabCreatorManager,
                        mMultiInstanceManager);
    }

    private void setUpUsingCorrectTabSupplier() {
        doReturn(mTab2).when(mActivityTabProvider).get();
        doReturn(false).when(mTab2).canGoBack();
        doReturn(true).when(mTab).canGoBack();
    }
}
