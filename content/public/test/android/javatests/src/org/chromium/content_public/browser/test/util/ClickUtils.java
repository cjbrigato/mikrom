// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content_public.browser.test.util;

import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static org.chromium.ui.test.util.ViewUtils.onViewWaiting;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.is;

import android.app.Instrumentation;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;

import org.chromium.base.ThreadUtils;

/** Test utils for clicking and other mouse actions. */
public class ClickUtils {
    /**
     * Click a button. Unlike {@link ClickUtils#mouseSingleClickView} this directly accesses the
     * view and does not send motion events though the message queue. As such it doesn't require the
     * view to have been created by the instrumented activity, but gives less flexibility than
     * mouseSingleClickView. For example, if the view is hierarchical, then clickButton will always
     * act on specified view, whereas mouseSingleClickView will send the events to the appropriate
     * child view. It is hence only really appropriate for simple views such as buttons.
     *
     * @param button the button to be clicked.
     */
    public static void clickButton(final View button) {
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    // Post the actual click to the button's message queue, to ensure that it has
                    // been inflated before the click is received.
                    button.post(
                            () -> {
                                button.performClick();
                            });
                });
    }

    /**
     * Sends (synchronously) a single mouse click to the View at the specified coordinates.
     *
     * @param instrumentation Instrumentation object used by the test.
     * @param v The view the coordinates are relative to.
     * @param x Relative x location to the view.
     * @param y Relative y location to the view.
     */
    public static void mouseSingleClickView(Instrumentation instrumentation, View v, int x, int y) {
        int location[] = TestTouchUtils.getAbsoluteLocationFromRelative(v, x, y);
        int absoluteX = location[0];
        int absoluteY = location[1];
        mouseSingleClick(instrumentation, absoluteX, absoluteY);
    }

    /**
     * Sends (synchronously) a single mouse click to the center of the View.
     *
     * @param instrumentation Instrumentation object used by the test.
     * @param v The view the coordinates are relative to.
     */
    public static void mouseSingleClickView(Instrumentation instrumentation, View v) {
        int x = v.getWidth() / 2;
        int y = v.getHeight() / 2;
        mouseSingleClickView(instrumentation, v, x, y);
    }

    /**
     * Sends (synchronously) a single mouse context i.e. right click to the View at the specified
     * coordinates.
     *
     * @param instrumentation Instrumentation object used by the test.
     * @param v The view the coordinates are relative to.
     * @param x Relative x location to the view.
     * @param y Relative y location to the view.
     */
    public static void mouseContextClickView(
            Instrumentation instrumentation, View v, int x, int y) {
        onViewWaiting(allOf(is(v), isDisplayed()));
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    // Post the actual click to the button's message queue, to ensure that it has
                    // been inflated before the click is received.
                    long downTime = SystemClock.uptimeMillis();
                    v.dispatchGenericMotionEvent(
                            createMouseClickEvent(
                                    instrumentation,
                                    MotionEvent.ACTION_BUTTON_PRESS,
                                    downTime,
                                    x,
                                    y,
                                    MotionEvent.BUTTON_SECONDARY));
                    v.dispatchGenericMotionEvent(
                            createMouseClickEvent(
                                    instrumentation,
                                    MotionEvent.ACTION_BUTTON_RELEASE,
                                    downTime,
                                    x,
                                    y,
                                    MotionEvent.BUTTON_SECONDARY));
                });
    }

    private static MotionEvent createMouseClickEvent(
            Instrumentation instrumentation,
            int action,
            long downTime,
            float x,
            float y,
            int buttonState) {
        long eventTime = SystemClock.uptimeMillis();
        MotionEvent.PointerCoords coords[] = new MotionEvent.PointerCoords[1];
        coords[0] = new MotionEvent.PointerCoords();
        coords[0].x = x;
        coords[0].y = y;
        MotionEvent.PointerProperties properties[] = new MotionEvent.PointerProperties[1];
        properties[0] = new MotionEvent.PointerProperties();
        properties[0].id = 0;
        properties[0].toolType = MotionEvent.TOOL_TYPE_FINGER;
        MotionEvent event =
                MotionEvent.obtain(
                        downTime,
                        eventTime,
                        action,
                        1,
                        properties,
                        coords,
                        0,
                        buttonState,
                        0.0f,
                        0.0f,
                        0,
                        0,
                        InputDevice.SOURCE_MOUSE,
                        buttonState);
        return event;
    }

    private static void sendMouseAction(
            Instrumentation instrumentation,
            int action,
            long downTime,
            float x,
            float y,
            int buttonState) {
        MotionEvent event =
                createMouseClickEvent(instrumentation, action, downTime, x, y, buttonState);
        instrumentation.sendPointerSync(event);
        instrumentation.waitForIdleSync();
    }

    /**
     * Sends (synchronously) a single mouse click to an absolute screen coordinates.
     *
     * @param instrumentation Instrumentation object used by the test.
     * @param x Screen absolute x location.
     * @param y Screen absolute y location.
     */
    private static void mouseSingleClick(Instrumentation instrumentation, float x, float y) {
        long downTime = SystemClock.uptimeMillis();
        sendMouseAction(instrumentation, MotionEvent.ACTION_DOWN, downTime, x, y, 0);
        sendMouseAction(instrumentation, MotionEvent.ACTION_UP, downTime, x, y, 0);
    }
}
