// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.hub;

import android.animation.AnimatorSet;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

/**
 * A class for holding an animator that animates {@link HubLayout} transitions. This class is
 * supplied by an implementation of {@link HubLayoutAnimatorSupplier}.
 */
@NullMarked
public class HubLayoutAnimator {
    private final @HubLayoutAnimationType int mAnimationType;
    private final AnimatorSet mAnimatorSet;
    private final @Nullable HubLayoutAnimationListener mListener;

    /**
     * Creates a {@link HubLayoutAnimator} for holding animator data.
     *
     * @param animationType The {@link HubLayoutAnimationType} of the animation.
     * @param animatorSet The {@link AnimatorSet} containing the animation to run.
     * @param listener The {@link HubLayoutAnimationListener} to listen for animation progress.
     */
    HubLayoutAnimator(
            @HubLayoutAnimationType int animationType,
            AnimatorSet animatorSet,
            @Nullable HubLayoutAnimationListener listener) {
        mAnimationType = animationType;
        mAnimatorSet = animatorSet;
        mListener = listener;
    }

    public @HubLayoutAnimationType int getAnimationType() {
        return mAnimationType;
    }

    public AnimatorSet getAnimatorSet() {
        return mAnimatorSet;
    }

    public @Nullable HubLayoutAnimationListener getListener() {
        return mListener;
    }
}
