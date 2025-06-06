// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox.status;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.verify;

import android.graphics.Rect;
import android.view.View;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.ContextUtils;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.feature_engagement.TrackerFactory;
import org.chromium.chrome.browser.omnibox.R;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.user_education.IphCommand;
import org.chromium.chrome.browser.user_education.UserEducationHelper;
import org.chromium.components.feature_engagement.FeatureConstants;
import org.chromium.components.feature_engagement.Tracker;

/** Unit tests for the PageInfoIphController. */
@RunWith(BaseRobolectricTestRunner.class)
public class PageInfoIphControllerUnitTest {
    private static final Rect STATUS_INSETS = new Rect(0, 0, 0, 0);
    private static final int IPH_RES_ID = R.string.accessibility_omnibox_btn_refine;
    private static final int TIMEOUT = 12345;

    public @Rule MockitoRule mMockitoRule = MockitoJUnit.rule();
    private @Mock UserEducationHelper mHelper;
    private @Mock Profile mProfile;
    private @Mock Tracker mTracker;
    private View mView;
    private PageInfoIphController mController;
    private final ArgumentCaptor<IphCommand> mIphCmdCaptor =
            ArgumentCaptor.forClass(IphCommand.class);

    @Before
    public void setUp() {
        TrackerFactory.setTrackerForTests(mTracker);

        mView = new View(ContextUtils.getApplicationContext());
        mController = new PageInfoIphController(mHelper, mView);
    }

    @After
    public void tearDown() {
        TrackerFactory.setTrackerForTests(null);
    }

    @Test
    public void onPermissionDialogShown() {
        mController.onPermissionDialogShown(mProfile, TIMEOUT);
        verify(mHelper).requestShowIph(mIphCmdCaptor.capture());
        var cmd = mIphCmdCaptor.getValue();
        cmd.fetchFromResources();

        assertEquals(FeatureConstants.PAGE_INFO_FEATURE, cmd.featureName);
        assertEquals(R.string.page_info_iph, cmd.stringId);
        assertEquals(STATUS_INSETS, cmd.insetRect);
        assertTrue(cmd.dismissOnTouch);
        assertEquals(TIMEOUT, cmd.autoDismissTimeout);
        assertNull(cmd.anchorRect);
        assertEquals(mView, cmd.anchorView);
    }

    @Test
    public void showStoreIconIph() {
        mController.showStoreIconIph(TIMEOUT, IPH_RES_ID);
        verify(mHelper).requestShowIph(mIphCmdCaptor.capture());
        var cmd = mIphCmdCaptor.getValue();
        cmd.fetchFromResources();

        assertEquals(TIMEOUT, cmd.autoDismissTimeout);
        assertEquals(IPH_RES_ID, cmd.stringId);
        assertEquals(FeatureConstants.PAGE_INFO_STORE_INFO_FEATURE, cmd.featureName);
        assertEquals(STATUS_INSETS, cmd.insetRect);
        assertTrue(cmd.dismissOnTouch);
        assertNull(cmd.anchorRect);
        assertEquals(mView, cmd.anchorView);
    }

    @Test
    public void showCookieControlsIph() {
        mController.showCookieControlsIph(TIMEOUT, IPH_RES_ID);
        verify(mHelper).requestShowIph(mIphCmdCaptor.capture());
        var cmd = mIphCmdCaptor.getValue();
        cmd.fetchFromResources();

        assertEquals(TIMEOUT, cmd.autoDismissTimeout);
        assertEquals(IPH_RES_ID, cmd.stringId);
        assertEquals(FeatureConstants.COOKIE_CONTROLS_FEATURE, cmd.featureName);
        assertEquals(STATUS_INSETS, cmd.insetRect);
        assertTrue(cmd.dismissOnTouch);
        assertNull(cmd.anchorRect);
        assertEquals(mView, cmd.anchorView);
    }
}
