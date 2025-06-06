// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ui.appmenu;

import android.graphics.Rect;
import android.view.Surface;
import android.view.View;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;
import org.mockito.invocation.InvocationOnMock;
import org.robolectric.annotation.Config;

import org.chromium.base.test.BaseRobolectricTestRunner;

/** Tests AppMenu#getPopupPosition. */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class AppMenuPopupPositionTest {

    private final int[] mTempLocation = new int[2];

    private static final int APP_WIDTH = 400;
    private static final int APP_HEIGHT = 1000;
    private static final int BG_PADDING = 10;
    private static final int POPUP_WIDTH = 300;
    private static final int ANCHOR_X = 100;
    private static final int ANCHOR_Y = 300;
    private static final int ANCHOR_WIDTH = 40;
    private static final int NEGATIVE_SOFTWARE_VERTICAL_OFFSET = 25;
    private final View mAnchorView = Mockito.mock(View.class);
    private final Rect mAppRect = new Rect(0, 0, APP_WIDTH, APP_HEIGHT);
    private final Rect mBgPaddingRect = new Rect(BG_PADDING, BG_PADDING, BG_PADDING, BG_PADDING);

    @Before
    public void setUp() {
        Mockito.doAnswer(
                        (InvocationOnMock invocation) -> {
                            mTempLocation[0] = ANCHOR_X;
                            mTempLocation[1] = ANCHOR_Y;
                            return null;
                        })
                .when(mAnchorView)
                .getLocationInWindow(mTempLocation);

        Mockito.doAnswer(
                        (InvocationOnMock invocation) -> {
                            mTempLocation[0] = ANCHOR_X;
                            mTempLocation[1] = ANCHOR_Y;
                            return null;
                        })
                .when(mAnchorView)
                .getLocationOnScreen(mTempLocation);

        Mockito.when(mAnchorView.getWidth()).thenReturn(ANCHOR_WIDTH);
    }

    @Test
    public void testPermanentButton_Portrait() {
        // Popup should should be in the middle of the screen, anchored near the anchor view.
        int expectedX = (APP_WIDTH - POPUP_WIDTH) / 2;
        int expectedY = ANCHOR_Y - BG_PADDING;

        int[] results = getPopupPosition(true, Surface.ROTATION_0, View.LAYOUT_DIRECTION_LTR);
        Assert.assertEquals("Incorrect popup x", expectedX, results[0]);
        Assert.assertEquals("Incorrect popup y", expectedY, results[1]);

        results = getPopupPosition(true, Surface.ROTATION_180, View.LAYOUT_DIRECTION_LTR);
        Assert.assertEquals("Incorrect popup x for rotation 180", expectedX, results[0]);
        Assert.assertEquals("Incorrect popup y for rotation 180", expectedY, results[1]);
    }

    @Test
    public void testPermanentButton_Landscape() {
        int[] results = getPopupPosition(true, Surface.ROTATION_90, View.LAYOUT_DIRECTION_LTR);

        // Popup should be positioned toward the right edge of the screen, anchored near the anchor
        // view.
        int expectedX = APP_WIDTH - POPUP_WIDTH;
        int expectedY = ANCHOR_Y - BG_PADDING;
        Assert.assertEquals("Incorrect popup x", expectedX, results[0]);
        Assert.assertEquals("Incorrect popup y", expectedY, results[1]);

        // Popup should be positioned toward the left edge of the screen, anchored near the anchor
        // view.
        expectedX = 0;
        results = getPopupPosition(true, Surface.ROTATION_270, View.LAYOUT_DIRECTION_LTR);
        Assert.assertEquals("Incorrect popup x for rotation 180", expectedX, results[0]);
        Assert.assertEquals("Incorrect popup y for rotation 180", expectedY, results[1]);
    }

    @Test
    public void testTopButton_LTR() {
        int[] results = getPopupPosition(false, Surface.ROTATION_0, View.LAYOUT_DIRECTION_LTR);

        // The top right edge of the popup should be aligned with the top right edge of the button.
        int expectedX = ANCHOR_X + ANCHOR_WIDTH - POPUP_WIDTH;
        int expectedY = ANCHOR_Y - NEGATIVE_SOFTWARE_VERTICAL_OFFSET;
        Assert.assertEquals("Incorrect popup x", expectedX, results[0]);
        Assert.assertEquals("Incorrect popup y", expectedY, results[1]);
    }

    @Test
    public void testTopButton_RTL() {
        int[] results = getPopupPosition(false, Surface.ROTATION_0, View.LAYOUT_DIRECTION_RTL);

        // The top left edge of the popup should be aligned with the top left edge of the button.
        int expectedX = ANCHOR_X;
        int expectedY = ANCHOR_Y - NEGATIVE_SOFTWARE_VERTICAL_OFFSET;
        Assert.assertEquals("Incorrect popup x", expectedX, results[0]);
        Assert.assertEquals("Incorrect popup y", expectedY, results[1]);
    }

    private int[] getPopupPosition(boolean isByPermanentButton, int rotation, int layoutDirection) {
        return AppMenu.getPopupPosition(
                mTempLocation,
                isByPermanentButton,
                NEGATIVE_SOFTWARE_VERTICAL_OFFSET,
                rotation,
                mAppRect,
                mBgPaddingRect,
                mAnchorView,
                POPUP_WIDTH,
                layoutDirection);
    }
}
