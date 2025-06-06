// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.util;

import android.view.KeyEvent;

import org.chromium.build.annotations.NullMarked;
import org.chromium.ui.base.LocalizationUtils;

/** This is a helper class to handle navigation related checks for key events. */
@NullMarked
public class KeyNavigationUtil {
    /** This is a helper class with no instance. */
    private KeyNavigationUtil() {}

    /**
     * Checks whether the given event is any of DPAD down or NUMPAD down.
     * @param event Event to be checked.
     * @return Whether the event should be processed as a navigation down.
     */
    public static boolean isGoDown(KeyEvent event) {
        return isActionDown(event)
                && (event.getKeyCode() == KeyEvent.KEYCODE_DPAD_DOWN
                        || (!event.isNumLockOn()
                                && event.getKeyCode() == KeyEvent.KEYCODE_NUMPAD_2));
    }

    /**
     * Checks whether the given event is any of DPAD up or NUMPAD up.
     * @param event Event to be checked.
     * @return Whether the event should be processed as a navigation up.
     */
    public static boolean isGoUp(KeyEvent event) {
        return isActionDown(event)
                && (event.getKeyCode() == KeyEvent.KEYCODE_DPAD_UP
                        || (!event.isNumLockOn()
                                && event.getKeyCode() == KeyEvent.KEYCODE_NUMPAD_8));
    }

    /**
     * Checks whether the given event is any of DPAD right or NUMPAD right.
     * @param event Event to be checked.
     * @return Whether the event should be processed as a navigation right.
     */
    public static boolean isGoRight(KeyEvent event) {
        return isActionDown(event)
                && (event.getKeyCode() == KeyEvent.KEYCODE_DPAD_RIGHT
                        || (!event.isNumLockOn()
                                && event.getKeyCode() == KeyEvent.KEYCODE_NUMPAD_6));
    }

    /**
     * Checks whether the given event is any of DPAD left or NUMPAD left.
     * @param event Event to be checked.
     * @return Whether the event should be processed as a navigation left.
     */
    public static boolean isGoLeft(KeyEvent event) {
        return isActionDown(event)
                && (event.getKeyCode() == KeyEvent.KEYCODE_DPAD_LEFT
                        || (!event.isNumLockOn()
                                && event.getKeyCode() == KeyEvent.KEYCODE_NUMPAD_4));
    }

    /**
     * Whether this event should move keyboard focus in the forward direction.
     *
     * @param event The {@link KeyEvent} to analyze.
     * @return Whether this event should move keyboard focus in the forward direction.
     */
    public static boolean isMoveFocusForward(KeyEvent event) {
        return isActionDown(event)
                && ((isGoRight(event) && !LocalizationUtils.isLayoutRtl())
                        || (isGoLeft(event) && LocalizationUtils.isLayoutRtl())
                        || (event.getKeyCode() == KeyEvent.KEYCODE_TAB && !event.isShiftPressed()));
    }

    /**
     * Whether this event should move keyboard focus in the backward direction.
     *
     * @param event The {@link KeyEvent} to analyze.
     * @return Whether this event should move keyboard focus in the backward direction.
     */
    public static boolean isMoveFocusBackward(KeyEvent event) {
        return isActionDown(event)
                && ((isGoLeft(event) && !LocalizationUtils.isLayoutRtl())
                        || (isGoRight(event) && LocalizationUtils.isLayoutRtl())
                        || (event.getKeyCode() == KeyEvent.KEYCODE_TAB && event.isShiftPressed()));
    }

    /**
     * Whether this event should trigger the keyboard-focused button.
     *
     * @param event The {@link KeyEvent} to analyze.
     * @return Whether this event should trigger the keyboard-focused button.
     */
    public static boolean isButtonActivate(KeyEvent event) {
        return isActionDown(event)
                && (isEnter(event) || event.getKeyCode() == KeyEvent.KEYCODE_SPACE);
    }

    /**
     * Checks whether the given event is any of DPAD down, DPAD up, NUMPAD down or NUMPAD up.
     *
     * @param event Event to be checked.
     * @return Whether the event should be processed as any of navigation up or navigation down.
     */
    public static boolean isGoUpOrDown(KeyEvent event) {
        return isGoDown(event) || isGoUp(event);
    }

    /**
     * Checks whether the given event is any DPAD or NUMPAD direction.
     * @param event Event to be checked.
     * @return Whether the event should be processed as any of navigation direction.
     */
    public static boolean isGoAnyDirection(KeyEvent event) {
        return isGoDown(event) || isGoUp(event) || isGoLeft(event) || isGoRight(event);
    }

    /**
     * Checks whether the given event is any of ENTER or NUMPAD ENTER.
     * @param event Event to be checked.
     * @return Whether the event should be processed as ENTER.
     */
    public static boolean isEnter(KeyEvent event) {
        return (event.getKeyCode() == KeyEvent.KEYCODE_ENTER
                || event.getKeyCode() == KeyEvent.KEYCODE_NUMPAD_ENTER);
    }

    /**
     * Checks whether the given event is an ACTION_DOWN event.
     * @param event Event to be checked.
     * @return Whether the event is an ACTION_DOWN event.
     */
    public static boolean isActionDown(KeyEvent event) {
        return event.getAction() == KeyEvent.ACTION_DOWN;
    }

    /**
     * Checks whether the given event is an ACTION_UP event.
     * @param event Event to be checked.
     * @return Whether the event is an ACTION_UP event.
     */
    public static boolean isActionUp(KeyEvent event) {
        return event.getAction() == KeyEvent.ACTION_UP;
    }

    /**
     * Checks whether the given event is a TAB event.
     *
     * @param event Event to be checked.
     * @return Whether the event is a TAB event.
     */
    public static boolean isTab(KeyEvent event) {
        return isActionDown(event)
                && event.getKeyCode() == KeyEvent.KEYCODE_TAB
                && event.hasNoModifiers();
    }

    /**
     * Checks whether the given event is a TAB+SHIFT event.
     *
     * @param event Event to be checked.
     * @return Whether the event is a TAB +SHIFT event.
     */
    public static boolean isBackwardTab(KeyEvent event) {
        return isActionDown(event)
                && event.getKeyCode() == KeyEvent.KEYCODE_TAB
                && event.isShiftPressed();
    }
}
