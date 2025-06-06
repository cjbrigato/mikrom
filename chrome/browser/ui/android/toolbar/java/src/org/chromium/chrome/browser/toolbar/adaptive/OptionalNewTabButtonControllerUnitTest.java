// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.toolbar.adaptive;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import android.content.Context;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.graphics.drawable.Drawable;
import android.view.View;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

import org.chromium.base.supplier.Supplier;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.base.test.util.Features.EnableFeatures;
import org.chromium.chrome.browser.feature_engagement.TrackerFactory;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabCreator;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager;
import org.chromium.chrome.browser.user_education.IphCommandBuilder;
import org.chromium.components.feature_engagement.EventConstants;
import org.chromium.components.feature_engagement.FeatureConstants;
import org.chromium.components.feature_engagement.Tracker;
import org.chromium.url.JUnitTestGURLs;

/** Unit tests for {@link OptionalNewTabButtonController}. */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public final class OptionalNewTabButtonControllerUnitTest {
    private static final int WIDTH_DELTA = 50;

    @Rule public final MockitoRule mMockitoRule = MockitoJUnit.rule();
    private Context mContext;

    @Mock private Resources mResources;
    @Mock private Tab mTab;
    @Mock private Drawable mDrawable;
    @Mock private ActivityLifecycleDispatcher mActivityLifecycleDispatcher;
    @Mock private TabCreatorManager mTabCreatorManager;
    @Mock TabCreator mTabCreator;
    @Mock private Supplier<Tab> mTabSupplier;
    @Mock private Tracker mTracker;

    private final Configuration mConfiguration = new Configuration();
    private OptionalNewTabButtonController mOptionalNewTabButtonController;

    @Before
    public void setUp() {
        mContext = RuntimeEnvironment.application;

        doReturn(JUnitTestGURLs.EXAMPLE_URL).when(mTab).getUrl();
        doReturn(mContext).when(mTab).getContext();

        doReturn(mTab).when(mTabSupplier).get();

        mConfiguration.screenWidthDp = AdaptiveToolbarFeatures.DEFAULT_MIN_WIDTH_DP + WIDTH_DELTA;
        doReturn(mConfiguration).when(mResources).getConfiguration();

        doReturn(mTabCreator).when(mTabCreatorManager).getTabCreator(anyBoolean());

        mOptionalNewTabButtonController =
                new OptionalNewTabButtonController(
                        mContext,
                        mDrawable,
                        mActivityLifecycleDispatcher,
                        () -> mTabCreatorManager,
                        mTabSupplier,
                        () -> mTracker);

        TrackerFactory.setTrackerForTests(mTracker);
    }

    @EnableFeatures(ChromeFeatureList.ADAPTIVE_BUTTON_IN_TOP_TOOLBAR_CUSTOMIZATION_V2)
    @Test
    public void testIphCommandHelper() {
        assertNull(
                mOptionalNewTabButtonController
                        .get(/* tab= */ null)
                        .getButtonSpec()
                        .getIphCommandBuilder());

        // Verify that IphCommandBuilder is set just once;
        IphCommandBuilder builder =
                mOptionalNewTabButtonController.get(mTab).getButtonSpec().getIphCommandBuilder();

        assertNotNull(
                mOptionalNewTabButtonController.get(mTab).getButtonSpec().getIphCommandBuilder());

        // Verify that IphCommandBuilder is same as before, get(Tab) did not create a new one.
        assertEquals(
                builder,
                mOptionalNewTabButtonController.get(mTab).getButtonSpec().getIphCommandBuilder());
    }

    @Test
    @EnableFeatures(ChromeFeatureList.ADAPTIVE_BUTTON_IN_TOP_TOOLBAR_CUSTOMIZATION_V2)
    public void testIphEvent() {
        doReturn(true)
                .when(mTracker)
                .shouldTriggerHelpUi(
                        FeatureConstants
                                .ADAPTIVE_BUTTON_IN_TOP_TOOLBAR_CUSTOMIZATION_NEW_TAB_FEATURE);

        View view = Mockito.mock(View.class);
        mOptionalNewTabButtonController
                .get(mTab)
                .getButtonSpec()
                .getOnClickListener()
                .onClick(view);

        verify(mTracker, times(1))
                .notifyEvent(EventConstants.ADAPTIVE_TOOLBAR_CUSTOMIZATION_NEW_TAB_OPENED);
    }
}
