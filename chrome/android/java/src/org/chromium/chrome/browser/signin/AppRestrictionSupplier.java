// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.signin;

import android.content.Context;
import android.os.Bundle;
import android.os.SystemClock;
import android.os.UserManager;

import org.chromium.base.Callback;
import org.chromium.base.CommandLine;
import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.ThreadUtils;
import org.chromium.base.task.AsyncTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.components.policy.AbstractAppRestrictionsProvider;
import org.chromium.components.policy.AppRestrictionsProvider;
import org.chromium.components.policy.PolicySwitches;

import java.util.LinkedList;
import java.util.Locale;
import java.util.Queue;
import java.util.concurrent.RejectedExecutionException;

/**
 * Checks if app restrictions are present during a fullscreen signin activity. Internally it uses an
 * asynchronous background task to fetch restrictions, then notifies registered callbacks once
 * complete.
 *
 * <p>This class is used during fullscreen signin flows, so its lifecycle ends when the flow
 * completes. At which point {@link #destroy()} should be called.
 */
// TODO(crbug.com/385693639): This class should implement ObservableSupplier<Boolean>.
@NullMarked
public class AppRestrictionSupplier {
    private static final String TAG = "AppRestriction";

    private boolean mInitialized;
    private boolean mHasAppRestriction;
    private long mCompletionElapsedRealtimeMs;
    private final Queue<Callback<Boolean>> mCallbacks = new LinkedList<>();
    private final Queue<Callback<Long>> mCompletionTimeCallbacks = new LinkedList<>();

    private @Nullable AsyncTask<Boolean> mFetchAppRestrictionAsyncTask;

    public AppRestrictionSupplier() {
        ThreadUtils.assertOnUiThread();
        initialize();
    }

    /**
     * Register a callback whether app restriction is found on device. If app restrictions have
     * already been fetched, the callback will be invoked immediately.
     *
     * @param callback Callback to run with whether app restriction is found on device.
     */
    public void getHasAppRestriction(Callback<Boolean> callback) {
        ThreadUtils.assertOnUiThread();

        if (mInitialized) {
            callback.onResult(mHasAppRestriction);
        } else {
            mCallbacks.add(callback);
        }
    }

    /**
     * Registers a callback for the timestamp from {@link SystemClock#elapsedRealtime} when the app
     * restrictions call finished. If the restrictions have already been fetched, the callback will
     * be invoked immediately.
     *
     * @param callback Callback to run with the timestamp of completing the fetch.
     */
    public void getCompletionElapsedRealtimeMs(Callback<Long> callback) {
        ThreadUtils.assertOnUiThread();
        if (mInitialized) {
            callback.onResult(mCompletionElapsedRealtimeMs);
        } else {
            mCompletionTimeCallbacks.add(callback);
        }
    }

    /** Start fetching app restriction on an async thread. */
    private void initialize() {
        ThreadUtils.assertOnUiThread();
        long startTime = SystemClock.elapsedRealtime();

        // This is an imperfect system, and can sometimes return true when there will not actually
        // be any app restrictions. But we do not have parsing logic in Java to understand if the
        // switch sets valid policies.
        if (CommandLine.getInstance().hasSwitch(PolicySwitches.CHROME_POLICY)) {
            onRestrictionDetected(true, startTime);
            return;
        }

        if (AbstractAppRestrictionsProvider.hasTestRestrictions()) {
            onRestrictionDetected(true, startTime);
            return;
        }

        Context appContext = ContextUtils.getApplicationContext();
        try {
            mFetchAppRestrictionAsyncTask =
                    new AsyncTask<Boolean>() {
                        @Override
                        protected Boolean doInBackground() {
                            UserManager userManager =
                                    (UserManager) appContext.getSystemService(Context.USER_SERVICE);
                            Bundle bundle =
                                    AppRestrictionsProvider
                                            .getApplicationRestrictionsFromUserManager(
                                                    userManager, appContext.getPackageName());
                            return bundle != null && !bundle.isEmpty();
                        }

                        @Override
                        protected void onPostExecute(Boolean isAppRestricted) {
                            onRestrictionDetected(isAppRestricted, startTime);
                        }
                    };
            mFetchAppRestrictionAsyncTask.executeWithTaskTraits(TaskTraits.USER_BLOCKING_MAY_BLOCK);
        } catch (RejectedExecutionException e) {
            // Though unlikely, if the task is rejected, we assume no restriction exists.
            onRestrictionDetected(false, startTime);
        }
    }

    private void onRestrictionDetected(boolean isAppRestricted, long startTime) {
        mHasAppRestriction = isAppRestricted;
        mInitialized = true;

        // Only record histogram when startTime is valid.
        if (startTime > 0) {
            mCompletionElapsedRealtimeMs = SystemClock.elapsedRealtime();
            long runTime = mCompletionElapsedRealtimeMs - startTime;
            Log.i(
                    TAG,
                    String.format(
                            Locale.US,
                            "Policy received. Runtime: [%d], result: [%s]",
                            runTime,
                            isAppRestricted));
        }

        while (!mCallbacks.isEmpty()) {
            mCallbacks.remove().onResult(mHasAppRestriction);
        }
        while (!mCompletionTimeCallbacks.isEmpty()) {
            mCompletionTimeCallbacks.remove().onResult(mCompletionElapsedRealtimeMs);
        }
    }
}
