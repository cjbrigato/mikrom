// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.browser_ui.widget.highlight;

import android.graphics.Canvas;
import android.view.View;

import org.chromium.base.test.util.CriteriaHelper;
import org.chromium.build.annotations.Nullable;
import org.chromium.components.browser_ui.widget.R;

/** Allows for testing of views which are highlightable via ViewHighlighter. */
public class ViewHighlighterTestUtils {

    /**
     * Returns the pulse drawable reference that is kept via tags.
     *
     * @param view The view that is being checked for highlighting.
     * @return The pulse drawable.
     */
    private static @Nullable PulseDrawable getPulseDrawable(View view) {
        Object tag = view.getTag(R.id.highlight_drawable);
        if (tag instanceof PulseDrawable) {
            return (PulseDrawable) tag;
        }
        return null;
    }

    /**
     * Returns true if the provided view is currently being highlighted. Please note that this
     * function may not be the same as !checkHighlightOff.
     *
     * @param view The view which you'd like to check for highlighting.
     * @return True if the view is currently being highlighted.
     */
    public static boolean checkHighlightOn(View view) {
        PulseDrawable pulse = getPulseDrawable(view);
        return pulse != null && pulse.isRunning() && pulse.isVisible();
    }

    /**
     * Returns true if the provided view is not currently being highlighted. Please note that this
     * function may not be the same as !checkHighlightOn.
     *
     * @param view The view which you'd like to check for highlighting.
     * @return True if view is not currently being highlighted.
     */
    public static boolean checkHighlightOff(View view) {
        return getPulseDrawable(view) == null;
    }

    /**
     * Checks that the view is highlighted with a pulse highlight.
     *
     * @param view The view of interest.
     * @param timeoutDuration The timeout duration (should be set depending on the number of pulses
     *         and the pulse duration).
     * @return True iff the view was highlighted, and then turned off.
     */
    public static boolean checkHighlightPulse(View view, long timeoutDuration) {
        try {
            CriteriaHelper.pollUiThread(
                    () -> checkHighlightOn(view),
                    "Expected highlight to pulse on!",
                    timeoutDuration,
                    CriteriaHelper.DEFAULT_POLLING_INTERVAL);
            CriteriaHelper.pollUiThread(
                    () -> checkHighlightOff(view),
                    "Expected highlight to turn off!",
                    timeoutDuration,
                    CriteriaHelper.DEFAULT_POLLING_INTERVAL);
        } catch (CriteriaHelper.TimeoutException e) {
            e.printStackTrace();
            return false;
        }
        return true;
    }

    /**
     * Checks that the view is highlighted with a pulse highlight.
     *
     * @param view The view of interest.
     * @return True iff the view was highlighted, and then turned off.
     */
    public static boolean checkHighlightPulse(View view) {
        return checkHighlightPulse(view, CriteriaHelper.DEFAULT_MAX_TIME_TO_POLL);
    }

    /** Draws the {@link PulseDrawable} attached to the {@link View} with the {@link Canvas}. */
    public static void drawPulseDrawable(View view, Canvas canvas) {
        if (!checkHighlightOn(view)) return;
        PulseDrawable pulse = getPulseDrawable(view);
        if (pulse != null) {
            pulse.draw(canvas);
        }
    }
}
