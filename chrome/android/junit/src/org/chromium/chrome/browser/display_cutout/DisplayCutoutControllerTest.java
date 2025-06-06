// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.display_cutout;

import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.clearInvocations;
import static org.mockito.Mockito.description;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.reset;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import android.app.Activity;
import android.view.Window;
import android.view.WindowManager.LayoutParams;

import androidx.test.filters.SmallTest;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.annotation.Config;

import org.chromium.base.UserDataHost;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.blink.mojom.ViewportFit;
import org.chromium.chrome.browser.app.ChromeActivity;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabObserver;
import org.chromium.chrome.browser.tab.TabSelectionType;
import org.chromium.components.browser_ui.display_cutout.DisplayCutoutController;
import org.chromium.content_public.browser.WebContentsObserver;
import org.chromium.content_public.browser.test.mock.MockWebContents;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.ui.insets.InsetObserver;

import java.lang.ref.WeakReference;

/** Tests for {@link DisplayCutoutController} class. */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class DisplayCutoutControllerTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private Tab mTab;

    @Mock private MockWebContents mWebContents;

    @Mock private WindowAndroid mWindowAndroid;

    @Mock private Window mWindow;

    @Captor private ArgumentCaptor<TabObserver> mTabObserverCaptor;
    @Captor private ArgumentCaptor<WebContentsObserver> mWebContentObserverCaptor;

    @Mock private ChromeActivity mChromeActivity;

    @Mock private InsetObserver mInsetObserver;

    private DisplayCutoutTabHelper mDisplayCutoutTabHelper;
    private DisplayCutoutController mController;

    private WeakReference<Activity> mActivityRef;

    private final UserDataHost mTabDataHost = new UserDataHost();

    @Before
    public void setUp() {
        mActivityRef = new WeakReference<>(mChromeActivity);

        when(mChromeActivity.getWindow()).thenReturn(mWindow);
        when(mWindow.getAttributes()).thenReturn(new LayoutParams());
        when(mTab.getWindowAndroid()).thenReturn(mWindowAndroid);
        when(mTab.getWebContents()).thenReturn(mWebContents);
        when(mTab.getUserDataHost()).thenReturn(mTabDataHost);
        when(mWebContents.isFullscreenForCurrentTab()).thenReturn(true);
        when(mWindowAndroid.getActivity()).thenReturn(mActivityRef);
        when(mWindowAndroid.getInsetObserver()).thenReturn(mInsetObserver);

        ActivityDisplayCutoutModeSupplier.setInstanceForTesting(0);

        mDisplayCutoutTabHelper = spy(new DisplayCutoutTabHelper(mTab));
        mController = spy(mDisplayCutoutTabHelper.mCutoutController);
        mDisplayCutoutTabHelper.mCutoutController = mController;
    }

    @Test
    @SmallTest
    public void testViewportFitUpdate() {
        verify(mController, never()).maybeUpdateLayout();

        mDisplayCutoutTabHelper.setViewportFit(ViewportFit.COVER);
        verify(mController).maybeUpdateLayout();
    }

    @Test
    @SmallTest
    public void testViewportFitUpdateOnFullscreen() {
        // Re-adding observers; otherwise, the internal observers are bound to un-mocked
        // mController.
        mController.destroy();
        mController.maybeAddObservers();

        verify(mWebContents, times(2)).addObserver(mWebContentObserverCaptor.capture());
        WebContentsObserver webContentsObserver = mWebContentObserverCaptor.getValue();
        webContentsObserver.didToggleFullscreenModeForTab(true, false);
        verify(mController, description("Should update layout when entering fullscreen"))
                .maybeUpdateLayout();

        webContentsObserver.didToggleFullscreenModeForTab(false, false);
        verify(mController, times(2).description("Should update layout when exiting fullscreen"))
                .maybeUpdateLayout();
    }

    @Test
    @SmallTest
    public void testViewportFitUpdateNotChanged() {
        verify(mController, never()).maybeUpdateLayout();

        mDisplayCutoutTabHelper.setViewportFit(ViewportFit.AUTO);
        verify(mController, never()).maybeUpdateLayout();
    }

    @Test
    @SmallTest
    public void testCutoutModeWhenAutoAndInteractable() {
        when(mTab.isUserInteractable()).thenReturn(true);

        mDisplayCutoutTabHelper.setViewportFit(ViewportFit.AUTO);
        Assert.assertEquals(
                LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT,
                mController.computeDisplayCutoutMode());
    }

    @Test
    @SmallTest
    public void testCutoutModeWhenCoverAndInteractable() {
        when(mTab.isUserInteractable()).thenReturn(true);

        mDisplayCutoutTabHelper.setViewportFit(ViewportFit.COVER);
        Assert.assertEquals(
                LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES,
                mController.computeDisplayCutoutMode());
    }

    @Test
    @SmallTest
    public void testCutoutModeWhenCoverForcedAndInteractable() {
        when(mTab.isUserInteractable()).thenReturn(true);

        mDisplayCutoutTabHelper.setViewportFit(ViewportFit.COVER_FORCED_BY_USER_AGENT);
        Assert.assertEquals(
                LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES,
                mController.computeDisplayCutoutMode());
    }

    @Test
    @SmallTest
    public void testCutoutModeWhenContainAndInteractable() {
        when(mTab.isUserInteractable()).thenReturn(true);

        mDisplayCutoutTabHelper.setViewportFit(ViewportFit.CONTAIN);
        Assert.assertEquals(
                LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER,
                mController.computeDisplayCutoutMode());
    }

    @Test
    @SmallTest
    public void testCutoutModeWhenAutoAndNotInteractable() {
        mDisplayCutoutTabHelper.setViewportFit(ViewportFit.AUTO);
        Assert.assertEquals(
                LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT,
                mController.computeDisplayCutoutMode());
    }

    @Test
    @SmallTest
    public void testCutoutModeWhenCoverAndNotInteractable() {
        mDisplayCutoutTabHelper.setViewportFit(ViewportFit.COVER);
        Assert.assertEquals(
                LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT,
                mController.computeDisplayCutoutMode());
    }

    @Test
    @SmallTest
    public void testCutoutModeWhenCoverForcedAndNotInteractable() {
        mDisplayCutoutTabHelper.setViewportFit(ViewportFit.COVER_FORCED_BY_USER_AGENT);
        Assert.assertEquals(
                LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT,
                mController.computeDisplayCutoutMode());
    }

    @Test
    @SmallTest
    public void testCutoutModeWhenContainAndNotInteractable() {
        mDisplayCutoutTabHelper.setViewportFit(ViewportFit.CONTAIN);
        Assert.assertEquals(
                LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT,
                mController.computeDisplayCutoutMode());
    }

    @Test
    @SmallTest
    public void testLayoutOnInteractability_True() {
        // In this test we are checking for a side effect of maybeUpdateLayout.
        // This is because the tab observer holds a reference to the original
        // mDisplayCutoutTabHelper and not the spied one.
        verify(mTab).addObserver(mTabObserverCaptor.capture());
        reset(mTab);

        mTabObserverCaptor.getValue().onInteractabilityChanged(mTab, true);
        verify(mWindow).getAttributes();
    }

    @Test
    @SmallTest
    public void testLayoutOnInteractability_False() {
        // In this test we are checking for a side effect of maybeUpdateLayout.
        // This is because the tab observer holds a reference to the original
        // mDisplayCutoutTabHelper and not the spied one.
        verify(mTab).addObserver(mTabObserverCaptor.capture());
        reset(mTab);

        mTabObserverCaptor.getValue().onInteractabilityChanged(mTab, false);
        verify(mWindow).getAttributes();
    }

    @Test
    @SmallTest
    public void testLayout_NoWindow() {
        // Verify there's no crash when the tab's interactability changes after activity detachment.
        verify(mTab).addObserver(mTabObserverCaptor.capture());
        reset(mTab);

        mTabObserverCaptor.getValue().onActivityAttachmentChanged(mTab, null);
        mTabObserverCaptor.getValue().onInteractabilityChanged(mTab, false);
        verify(mWindow, never()).getAttributes();
    }

    @Test
    @SmallTest
    public void testLayoutOnShown() {
        // In this test we are checking for a side effect of maybeUpdateLayout.
        // This is because the tab observer holds a reference to the original
        // mDisplayCutoutTabHelper and not the spied one.
        verify(mTab).addObserver(mTabObserverCaptor.capture());
        reset(mTab);

        mTabObserverCaptor.getValue().onShown(mTab, TabSelectionType.FROM_NEW);
        verify(mWindow).getAttributes();
    }

    @Test
    @SmallTest
    public void testGetIsViewportFitCover() {
        // Go through the live creation of DisplayCutoutTabHelper.from(Tab) with our mock Tab.
        UserDataHost tabDataHost = new UserDataHost();
        when(mTab.getUserDataHost()).thenReturn(tabDataHost);
        DisplayCutoutTabHelper tabHelper = DisplayCutoutTabHelper.from(mTab);

        // TODO(crbug.com/40279791) Fix: We cannot access DisplayCutoutController#from(Tab)
        // because it's in a different package from this test. Code copied here.
        UserDataHost host = mTab.getUserDataHost();
        DisplayCutoutController liveController = host.getUserData(DisplayCutoutController.class);

        Assert.assertEquals(
                "Something went wrong with DisplayCutoutController construction or fetching an"
                        + " existing one via from().",
                tabHelper.getDisplayCutoutController(),
                liveController);

        liveController.setViewportFit(ViewportFit.AUTO);
        Assert.assertFalse(
                "SafeAreaInsets should have reported isViewportFitCover() false after the"
                        + " controller's setViewportFit to Auto was called.",
                DisplayCutoutController.getSafeAreaInsetsTracker(mTab).isViewportFitCover());

        liveController.setViewportFit(ViewportFit.COVER);
        Assert.assertTrue(
                "DisplayCutoutController.setViewportFit(cover) did not update the SafeAreaInsets"
                        + " isViewportFitCover to true!",
                DisplayCutoutController.getSafeAreaInsetsTracker(mTab).isViewportFitCover());

        liveController.setViewportFit(ViewportFit.COVER_FORCED_BY_USER_AGENT);
        Assert.assertTrue(
                "DisplayCutoutController.setViewportFit(COVER_FORCED_BY_USER_AGENT) did not update"
                        + " the SafeAreaInsets isViewportFitCover to true!",
                DisplayCutoutController.getSafeAreaInsetsTracker(mTab).isViewportFitCover());

        reset(mTab);
    }

    @Test
    public void testSafeAreaConstraint() {
        mDisplayCutoutTabHelper.setSafeAreaConstraint(true);
        DisplayCutoutController.SafeAreaInsetsTracker tracker =
                DisplayCutoutController.getSafeAreaInsetsTracker(mTab);
        Assert.assertNotNull(tracker);
        Assert.assertTrue(
                "SafeAreaConstrain did not pass through to the safe area insets tracker.",
                tracker.hasSafeAreaConstraint());

        mDisplayCutoutTabHelper.setSafeAreaConstraint(false);
        Assert.assertFalse(
                "SafeAreaConstrain did not pass through to the safe area insets tracker.",
                tracker.hasSafeAreaConstraint());
    }

    @Test
    public void testObserverUpdateOnContentChange() {
        // First, make sure observer is attached at the beginning.
        verify(mWebContents, atLeastOnce()).addObserver(mWebContentObserverCaptor.capture());
        WebContentsObserver observer = mWebContentObserverCaptor.getValue();
        Assert.assertEquals(observer, mController.getWebContentObserverForTesting());

        when(mTab.getWebContents()).thenReturn(null);
        mController.onContentChanged();
        verify(mWebContents).removeObserver(observer);
        Assert.assertNull(mController.getWebContentObserverForTesting());

        clearInvocations(mWebContents);
        when(mTab.getWebContents()).thenReturn(mWebContents);
        mController.onContentChanged();
        verify(mWebContents, atLeastOnce()).addObserver(mWebContentObserverCaptor.capture());
        WebContentsObserver observer2 = mWebContentObserverCaptor.getValue();
        Assert.assertEquals(observer2, mController.getWebContentObserverForTesting());
    }

    @Test
    public void testCreateWithNullWebContent() {
        when(mTab.getWebContents()).thenReturn(null);

        // Reset the controller so we'll need to create a new one.
        mTabDataHost.removeUserData(DisplayCutoutController.class);
        mDisplayCutoutTabHelper = new DisplayCutoutTabHelper(mTab);
        Assert.assertNull(
                mDisplayCutoutTabHelper.mCutoutController.getWebContentObserverForTesting());
    }
}
