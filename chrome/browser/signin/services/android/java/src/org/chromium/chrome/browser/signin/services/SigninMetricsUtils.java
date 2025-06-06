// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.signin.services;

import androidx.annotation.IntDef;
import androidx.annotation.VisibleForTesting;

import org.jni_zero.NativeMethods;

import org.chromium.base.metrics.RecordHistogram;
import org.chromium.build.annotations.NullMarked;
import org.chromium.components.signin.metrics.AccountConsistencyPromoAction;
import org.chromium.components.signin.metrics.SigninAccessPoint;
import org.chromium.components.signin.metrics.SigninPromoAction;
import org.chromium.components.signin.metrics.SyncButtonClicked;
import org.chromium.components.signin.metrics.SyncButtonsType;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/** Util methods for signin metrics logging. */
@NullMarked
public class SigninMetricsUtils {
    /** Used to record Signin.AddAccountState histogram. Do not change existing values. */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef({
        State.REQUESTED,
        State.STARTED,
        State.SUCCEEDED,
        State.FAILED,
        State.CANCELLED,
        State.NULL_ACCOUNT_NAME,
        State.ACTIVITY_DESTROYED,
        State.ACTIVITY_SURVIVED,
        State.NUM_STATES
    })
    public @interface State {
        int REQUESTED = 0;
        int STARTED = 1;
        int SUCCEEDED = 2;
        int FAILED = 3;
        int CANCELLED = 4;
        int NULL_ACCOUNT_NAME = 5;
        int ACTIVITY_DESTROYED = 6;
        int ACTIVITY_SURVIVED = 7;
        int NUM_STATES = 8;
    }

    /**
     * Logs Signin.AccountConsistencyPromoAction.* histograms.
     *
     * @param promoAction {@link AccountConsistencyPromoAction} for this sign-in flow.
     * @param accessPoint {@link SigninAccessPoint} that initiated the sign-in flow.
     */
    public static void logAccountConsistencyPromoAction(
            @AccountConsistencyPromoAction int promoAction, @SigninAccessPoint int accessPoint) {
        SigninMetricsUtilsJni.get().logAccountConsistencyPromoAction(promoAction, accessPoint);
    }

    /**
     * Logs Signin.SignIn.Started histogram (used to record that a signin UI was displayed). Sign-in
     * completion histogram is recorded by {@link SigninManager#signin}.
     *
     * @param accessPoint {@link SigninAccessPoint} that initiated the sign-in flow.
     */
    public static void logSigninStarted(@SigninAccessPoint int accessPoint) {
        RecordHistogram.recordEnumeratedHistogram(
                "Signin.SignIn.Started", accessPoint, SigninAccessPoint.MAX_VALUE);
    }

    /**
     * Logs Signin.SignIn.Offered histograms (used to record that a sign-in promo was displayed).
     *
     * @param promoAction {@link SigninPromoAction} user actions on the sign-in promo.
     * @param accessPoint {@link SigninAccessPoint} that initiated the sign-in flow.
     */
    public static void logSigninOffered(
            @SigninPromoAction int promoAction, @SigninAccessPoint int accessPoint) {
        SigninMetricsUtilsJni.get().logSigninOffered(promoAction, accessPoint);
    }

    /** Logs Signin.AddAccountState histogram. */
    public static void logAddAccountStateHistogram(@State int state) {
        RecordHistogram.recordEnumeratedHistogram(
                "Signin.AddAccountState", state, State.NUM_STATES);
    }

    public static void logHistorySyncAcceptButtonClicked(
            @SigninAccessPoint int accessPoint, @SyncButtonClicked int syncButtonType) {
        RecordHistogram.recordEnumeratedHistogram(
                "Signin.HistorySyncOptIn.Completed", accessPoint, SigninAccessPoint.MAX_VALUE);
        recordButtonTypeClicked(syncButtonType);
    }

    public static void logHistorySyncDeclineButtonClicked(
            @SigninAccessPoint int accessPoint, @SyncButtonClicked int syncButtonType) {
        RecordHistogram.recordEnumeratedHistogram(
                "Signin.HistorySyncOptIn.Declined", accessPoint, SigninAccessPoint.MAX_VALUE);
        recordButtonTypeClicked(syncButtonType);
    }

    public static void recordButtonTypeClicked(@SyncButtonClicked int type) {
        RecordHistogram.recordEnumeratedHistogram(
                "Signin.SyncButtons.Clicked", type, SyncButtonClicked.MAX_VALUE);
    }

    public static void recordButtonsShown(@SyncButtonsType int type) {
        RecordHistogram.recordEnumeratedHistogram(
                "Signin.SyncButtons.Shown", type, SyncButtonsType.MAX_VALUE);
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
    @NativeMethods
    public interface Natives {
        void logSigninUserActionForAccessPoint(int accessPoint);

        void logAccountConsistencyPromoAction(int consistencyPromoAction, int accessPoint);

        void logSigninOffered(int signinPromoAction, int accessPoint);
    }

    private SigninMetricsUtils() {}
}
