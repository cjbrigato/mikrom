// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.ui.insets;

import android.graphics.Rect;
import android.graphics.Region;
import android.graphics.RegionIterator;
import android.util.Size;
import android.view.WindowInsets;

import androidx.core.graphics.Insets;
import androidx.core.view.WindowInsetsCompat.Type.InsetsType;

import org.chromium.base.Callback;
import org.chromium.base.Log;
import org.chromium.base.ResettersForTesting;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

import java.util.List;

/** Helper functions for working with WindowInsets and Rects. */
@NullMarked
public final class WindowInsetsUtils {
    private static final String TAG = "WindowInsetsUtils";

    private static final Size DEFAULT_INSETS_FRAME = new Size(0, 0);
    private static final List<Rect> DEFAULT_INSETS_BOUNDING_RECTS = List.of();

    private static boolean sGetFrameMethodNotFound;
    private static boolean sGetBoundingRectsMethodNotFound;

    private static @Nullable Size sFrameForTesting;
    private static @Nullable Rect sWidestUnoccludedRectForTesting;

    /**
     * Class to encapsulate information about the uncoccluded region determined by {@link
     * #getUnoccludedRegion(Rect, List)}.
     */
    public static class UnoccludedRegion {
        private final Rect mWidestUnoccludedRect;
        private final boolean mIsRegionComplex;

        /* Constructor to get the unoccluded region info. */
        public UnoccludedRegion(Rect widestUnoccludedRect, boolean isRegionComplex) {
            mWidestUnoccludedRect = widestUnoccludedRect;
            mIsRegionComplex = isRegionComplex;
        }

        /**
         * @return The widest rect in the unoccluded region. See docs for {@link
         *     #getUnoccludedRegion(Rect, List)} for more details.
         */
        public Rect getWidestUnoccludedRect() {
            return mWidestUnoccludedRect;
        }

        /**
         * @return {@code true} if the unoccluded region is complex and contains multiple unoccluded
         *     rects, {@code false} if it is empty or contains a single unoccluded rect.
         */
        public boolean isRegionComplex() {
            return mIsRegionComplex;
        }
    }

    /** Private constructor to stop instantiation. */
    private WindowInsetsUtils() {}

    /**
     * Return the rect represented by the input {@link Insets}. Return an empty Rect if the input
     * |insets| inset more than one edge from the |windowRect| (i.e. has more than one non-zero
     * value over the four sides: left / top / right / bottom).
     *
     * <p><b>Example 1: </b><br>
     * windowRect = Rect(0, 0, 100, 50) // left: 0, top: 0, right: 100, bottom: 50 <br>
     * insets = (0, 20, 0, 0) // Inset 20 from the top. <br>
     * Output: Rect(0, 0, 100, 20) // Insets represent 20 from the top of the windowRect.
     *
     * <p><b>Example 2:</b> <br>
     * windowRect = Rect(0, 0, 100, 50) // left: 0, top: 0, right: 100, bottom: 50 <br>
     * insets = (0, 0, 30, 0) // Inset 30 from the right. <br>
     * Output: Rect(30, 0, 100, 50) // Insets represent 30 from the right of the windowRect,
     * starting from left=70
     *
     * <p><b>Example 3:</b> <br>
     * windowRect = Rect(0, 0, 100, 50) // left: 0, top: 0, right: 100, bottom: 50 <br>
     * insets = (0, 0, 10, 10) // Inset 10 from the right and 10 from bottom <br>
     * Output: Rect(0, 0, 0, 0) // Insets represent more than one edge and it's not an Rect.
     *
     * <p><b>Example 4:</b> <br>
     * windowRect = Rect(0, 0, 100, 50) // left: 0, top: 0, right: 100, bottom: 50 <br>
     * insets = (0, 0, 0, 0) // No insets from thw windowRect <br>
     * Output: Rect(0, 0, 0, 0) // Insets does not represent any rect.
     *
     * @param windowRect The rect representing the root view of the window.
     * @param insets Insets describing a certain type of a WindowInsts.
     * @return Rect that the insets represent in the windowRect. Empty rect if insets represent more
     *     than one edge.
     */
    public static Rect toRectInWindow(Rect windowRect, Insets insets) {
        int sides = 0;
        Rect res = new Rect(windowRect);

        if (insets.left != 0) {
            res.right = windowRect.left + insets.left;
            sides++;
        }

        if (insets.top != 0) {
            if (sides > 0) return new Rect();
            res.bottom = windowRect.top + insets.top;
            sides++;
        }

        if (insets.right != 0) {
            if (sides > 0) return new Rect();
            res.left = windowRect.right - insets.right;
            sides++;
        }

        if (insets.bottom != 0) {
            if (sides > 0) return new Rect();
            res.top = windowRect.bottom - insets.bottom;
            sides++;
        }

        return sides == 1 ? res : new Rect();
    }

    /**
     * Get the {@link UnoccludedRegion} within the |regionRect|. This includes the rect with the
     * maximum width that is not blocked by any rects within the |blockedRects|. This algorithm only
     * prioritizes the width of the returned rects, so the returned area does not necessarily have
     * the maximum area. If there are multiple rects with the same width, this method will bias the
     * first rect found in the region. If |blockedRects| is empty, this method will return an empty
     * rect.
     *
     * @see Region
     * @see RegionIterator
     * @param regionRect The un-blocked rect area.
     * @param blockedRects Areas within the regionRect that are blocked.
     * @return The {@link UnoccludedRegion} in |regionRect| that is not blocked by any
     *     |blockedRects|.
     */
    public static UnoccludedRegion getUnoccludedRegion(Rect regionRect, List<Rect> blockedRects) {
        if (sWidestUnoccludedRectForTesting != null) {
            return new UnoccludedRegion(
                    sWidestUnoccludedRectForTesting, /* isRegionComplex= */ false);
        }
        if (blockedRects.isEmpty()) {
            return new UnoccludedRegion(new Rect(), /* isRegionComplex= */ false);
        }

        Region region = new Region(regionRect);
        for (Rect rect : blockedRects) {
            region.op(rect, Region.Op.DIFFERENCE);
        }

        if (region.isEmpty()) {
            return new UnoccludedRegion(new Rect(), /* isRegionComplex= */ false);
        }

        Rect widestUnoccludedRect = new Rect();
        forEachRect(
                region,
                (rect) -> {
                    if (widestUnoccludedRect.width() < rect.width()) {
                        widestUnoccludedRect.set(rect);
                    }
                });
        return new UnoccludedRegion(widestUnoccludedRect, region.isComplex());
    }

    /** See {@link WindowInsets#getFrame()} for details. */
    @SuppressWarnings("NewApi")
    public static Size getFrameFromInsets(@Nullable WindowInsets windowInsets) {
        if (sFrameForTesting != null) return sFrameForTesting;

        // This invocation is wrapped in a try-catch block to allow backporting of the #getFrame()
        // API on pre-V devices. On pre-V devices not supporting this API, a default value will be
        // cached on the first failure and returned subsequently.
        if (sGetFrameMethodNotFound) return DEFAULT_INSETS_FRAME;
        try {
            return windowInsets == null ? DEFAULT_INSETS_FRAME : windowInsets.getFrame();
        } catch (NoSuchMethodError e) {
            Log.w(TAG, e.toString());
            sGetFrameMethodNotFound = true;
            return DEFAULT_INSETS_FRAME;
        }
    }

    /** See {@link WindowInsets#getBoundingRects(int)} for details. */
    @SuppressWarnings("NewApi")
    public static List<Rect> getBoundingRectsFromInsets(
            @Nullable WindowInsets windowInsets, @InsetsType int insetType) {
        // This invocation is wrapped in a try-catch block to allow backporting of the
        // #getBoundingRects() API on pre-V devices. On pre-V devices not supporting this API, a
        // default value will be cached on the first failure and returned subsequently.
        if (sGetBoundingRectsMethodNotFound) return DEFAULT_INSETS_BOUNDING_RECTS;
        try {
            return windowInsets == null
                    ? DEFAULT_INSETS_BOUNDING_RECTS
                    : windowInsets.getBoundingRects(insetType);
        } catch (NoSuchMethodError e) {
            Log.w(TAG, e.toString());
            sGetBoundingRectsMethodNotFound = true;
            return DEFAULT_INSETS_BOUNDING_RECTS;
        }
    }

    /** Sets the window frame size for testing purposes. */
    public static void setFrameForTesting(Size frame) {
        sFrameForTesting = frame;
        ResettersForTesting.register(() -> sFrameForTesting = DEFAULT_INSETS_FRAME);
    }

    /** Sets a rect to be returned by {@code #getWidestUnoccludedRect()} for testing purposes. */
    public static void setWidestUnoccludedRectForTesting(Rect widestUnoccludedRect) {
        sWidestUnoccludedRectForTesting = widestUnoccludedRect;
        ResettersForTesting.register(() -> sWidestUnoccludedRectForTesting = new Rect());
    }

    private static void forEachRect(Region region, Callback<Rect> rectConsumer) {
        final RegionIterator it = new RegionIterator(region);
        final Rect rect = new Rect();
        while (it.next(rect)) {
            rectConsumer.onResult(rect);
        }
    }
}
