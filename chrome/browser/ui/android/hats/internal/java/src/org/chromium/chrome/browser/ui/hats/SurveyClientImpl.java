// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ui.hats;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.app.Activity;

import org.chromium.base.Callback;
import org.chromium.base.CommandLine;
import org.chromium.base.Log;
import org.chromium.base.ResettersForTesting;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.task.AsyncTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.lifecycle.LifecycleObserver;
import org.chromium.chrome.browser.lifecycle.PauseResumeWithNativeObserver;
import org.chromium.chrome.browser.preferences.Pref;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorObserver;
import org.chromium.chrome.browser.ui.hats.SurveyConfig.RequestedBrowserType;
import org.chromium.components.user_prefs.UserPrefs;

import java.lang.ref.WeakReference;
import java.util.HashMap;
import java.util.Map;

/** Impl for SurveyClient interface. */
// TODO(crbug.com/40250401): Add metrics and refine the logging in this class.
@NullMarked
class SurveyClientImpl implements SurveyClient {
    private static final String TAG = "SurveyClient";

    /**
     * When set, bypass the AsyncTask to read throttler in the background, and ignore whether the
     * current activity is alive. Set for unit testing / native tests.
     */
    private static @Nullable Boolean sForceShowSurveyForTesting;

    private final SurveyConfig mConfig;
    private final SurveyUiDelegate mUiDelegate;
    private final SurveyController mController;
    private final SurveyThrottler mThrottler;
    private final ObservableSupplier<Boolean> mCrashUploadPermissionSupplier;
    private final Map<String, String> mAggregatedSurveyPsd;
    private final Profile mProfile;
    private final @Nullable TabModelSelector mTabModelSelector;

    private @Nullable WeakReference<Activity> mActivityRef;
    private @Nullable ActivityLifecycleDispatcher mLifecycleDispatcher;
    private @Nullable LifecycleObserver mLifecycleObserver;
    private @Nullable Callback<Boolean> mOnCrashUploadPermissionChangeCallback;
    private @Nullable TabModelSelectorObserver mTabModelSelectorObserver;
    private boolean mIsDestroyed;

    SurveyClientImpl(
            SurveyConfig config,
            SurveyUiDelegate uiDelegate,
            SurveyController controller,
            ObservableSupplier<Boolean> crashUploadPermissionSupplier,
            Profile profile,
            @Nullable TabModelSelector tabModelSelector) {
        mConfig = config;
        mUiDelegate = uiDelegate;
        mController = controller;
        mCrashUploadPermissionSupplier = crashUploadPermissionSupplier;
        mThrottler = new SurveyThrottler(mConfig);
        mAggregatedSurveyPsd = new HashMap<>();
        mProfile = profile;
        mTabModelSelector = tabModelSelector;
    }

    @Override
    public void showSurvey(Activity activity, ActivityLifecycleDispatcher lifecycleDispatcher) {
        showSurveyImpl(activity, lifecycleDispatcher, Map.of(), Map.of());
    }

    @Override
    public void showSurvey(
            Activity activity,
            @Nullable ActivityLifecycleDispatcher lifecycleDispatcher,
            Map<String, Boolean> surveyPsdBitValues,
            Map<String, String> surveyPsdStringValues) {
        showSurveyImpl(activity, lifecycleDispatcher, surveyPsdStringValues, surveyPsdBitValues);
    }

    /** Kick off the survey presentation flow. */
    private void showSurveyImpl(
            Activity activity,
            @Nullable ActivityLifecycleDispatcher lifecycleDispatcher,
            Map<String, String> surveyPsdStringValues,
            Map<String, Boolean> surveyPsdBitValues) {
        if (!configurationAllowsSurveys()) return;

        mActivityRef = new WeakReference<>(activity);
        mLifecycleDispatcher = lifecycleDispatcher;
        generateSurveyPsd(surveyPsdStringValues, surveyPsdBitValues);
        showSurveyIfEligible();
    }

    /** Generate the key-value pairs of survey PSD based on the config and given value fields. */
    private void generateSurveyPsd(
            Map<String, String> surveyPsdStringValues, Map<String, Boolean> surveyPsdBitValues) {
        assert surveyPsdStringValues.size() == mConfig.mPsdStringDataFields.length
                : "StringValues have a different size with fields.";
        assert surveyPsdBitValues.size() == mConfig.mPsdBitDataFields.length
                : "BitValues have a different size with fields.";

        for (var stringField : mConfig.mPsdStringDataFields) {
            assert surveyPsdStringValues.containsKey(stringField)
                    : "Undefined string fields: " + stringField;
            mAggregatedSurveyPsd.put(stringField, surveyPsdStringValues.get(stringField));
        }
        for (var bitField : mConfig.mPsdBitDataFields) {
            assert surveyPsdBitValues.get(bitField) != null : "Undefined bit fields: " + bitField;
            mAggregatedSurveyPsd.put(bitField, surveyPsdBitValues.get(bitField) ? "true" : "false");
        }
    }

    private void showSurveyIfEligible() {
        if (forceShowSurvey()) {
            startSurveyDownload(true);
            return;
        }
        AsyncTask<Boolean> throttlerTask =
                new AsyncTask<>() {
                    @Override
                    protected Boolean doInBackground() {
                        return mThrottler.canShowSurvey();
                    }

                    @Override
                    protected void onPostExecute(Boolean canShowSurvey) {
                        startSurveyDownload(canShowSurvey);
                    }
                };
        throttlerTask.executeWithTaskTraits(TaskTraits.BEST_EFFORT_MAY_BLOCK);
    }

    private void startSurveyDownload(boolean canShowSurvey) {
        if (!canShowSurvey) {
            Log.d(TAG, "Survey can't be shown");
            return;
        }
        assert mActivityRef != null;
        mController.downloadSurvey(
                assumeNonNull(mActivityRef.get()),
                mConfig.mTriggerId,
                this::onSurveyDownloadSucceeded,
                this::onSurveyDownloadFailed);
    }

    /**
     * @return Whether the actual browser type matches the requested browser type from the survey
     *     config.
     */
    private boolean isRightBrowserType() {
        if (mTabModelSelector == null) {
            // TODO(crbug.com/418075247): Ensure mTabModelSelector is never null.
            return true;
        }

        @RequestedBrowserType int requestedBrowserType = mConfig.mRequestedBrowserType;
        switch (requestedBrowserType) {
            case RequestedBrowserType.REGULAR:
                return !mTabModelSelector.isIncognitoSelected();
            case RequestedBrowserType.INCOGNITO:
                return mTabModelSelector.isIncognitoSelected();
            default:
                assert false : "Unknown requested browser type: " + requestedBrowserType;
                return false;
        }
    }

    private void onSurveyDownloadSucceeded() {
        Log.d(TAG, "Survey Download succeed.");
        if (!configurationAllowsSurveys()) return;

        // Dismiss the survey prompt if it is expired.
        if (mLifecycleDispatcher != null) {
            mLifecycleObserver =
                    new PauseResumeWithNativeObserver() {
                        @Override
                        public void onResumeWithNative() {
                            if (mController.isSurveyExpired(mConfig.mTriggerId)) {
                                mUiDelegate.dismiss();
                            }
                        }

                        @Override
                        public void onPauseWithNative() {}
                    };
            mLifecycleDispatcher.register(mLifecycleObserver);
        }

        // Dismiss the survey as soon as the crash upload permission changed.
        mOnCrashUploadPermissionChangeCallback =
                permitted -> {
                    if (!permitted) {
                        // TODO(crbug.com/40281825): Dismiss the on going survey if possible.
                        mUiDelegate.dismiss();
                    }
                };
        mCrashUploadPermissionSupplier.addObserver(mOnCrashUploadPermissionChangeCallback);

        // TODO(crbug.com/418075247): Ensure mTabModelSelector is never null.
        if (mTabModelSelector != null) {
            mTabModelSelectorObserver =
                    new TabModelSelectorObserver() {
                        @Override
                        public void onChange() {
                            if (!isRightBrowserType()) {
                                mUiDelegate.dismiss();
                                mTabModelSelector.removeObserver(this);
                            }
                        }
                    };
            mTabModelSelector.addObserver(mTabModelSelectorObserver);
        }

        mUiDelegate.showSurveyInvitation(
                this::onSurveyAccepted, this::onSurveyDeclined, this::onSurveyPresentationFailed);
    }

    private void onSurveyDownloadFailed() {
        Log.d(TAG, "Survey Download failed.");
        destroy(true);
    }

    private void onSurveyAccepted() {
        Log.d(TAG, "Survey accepted.");
        assert mActivityRef != null;
        if (!forceShowSurvey()
                && (mActivityRef.get() == null
                        || mActivityRef.get().isFinishing()
                        || mActivityRef.get().isDestroyed())) {
            destroy(false);
            return;
        }
        mThrottler.recordSurveyPromptDisplayed();
        mController.showSurveyIfAvailable(
                assumeNonNull(mActivityRef.get()),
                mConfig.mTriggerId,
                R.drawable.chrome_sync_logo,
                mLifecycleDispatcher,
                mAggregatedSurveyPsd);
        if (mLifecycleDispatcher != null && mLifecycleObserver != null) {
            mLifecycleDispatcher.unregister(mLifecycleObserver);
            mLifecycleObserver = null;
        }

        // Do not destroy the survey client at the end, since the controller still stored the survey
        // data.
    }

    private void onSurveyDeclined() {
        Log.d(TAG, "Survey declined.");
        mThrottler.recordSurveyPromptDisplayed();
        destroy(false);
    }

    private void onSurveyPresentationFailed() {
        Log.d(TAG, "Survey failed to present.");
        destroy(false);
    }

    /**
     * Destroy and clear the dependencies of this survey client. Note that if this call is invoked
     * bu SurveyUiDelegate, we should not call {@link SurveyUiDelegate#dismiss()} to avoid
     * recursively invoking #onSurveyPresentationFailed / #onSurveyDecliened.
     * @param dismissUiDelegate Whether we should call SurveyUiDelegate#dismiss()
     */
    private void destroy(boolean dismissUiDelegate) {
        assert !mIsDestroyed;
        mIsDestroyed = true;

        if (mActivityRef != null) {
            mActivityRef.clear();
            mActivityRef = null;
        }
        if (mLifecycleDispatcher != null && mLifecycleObserver != null) {
            mLifecycleDispatcher.unregister(mLifecycleObserver);
            mLifecycleObserver = null;
        }
        if (mOnCrashUploadPermissionChangeCallback != null) {
            mCrashUploadPermissionSupplier.removeObserver(mOnCrashUploadPermissionChangeCallback);
            mOnCrashUploadPermissionChangeCallback = null;
        }
        if (mTabModelSelector != null && mTabModelSelectorObserver != null) {
            mTabModelSelector.removeObserver(mTabModelSelectorObserver);
            mTabModelSelectorObserver = null;
        }
        mLifecycleDispatcher = null;

        if (dismissUiDelegate) {
            mUiDelegate.dismiss();
        }
        mController.destroy();
    }

    /**
     * When metrics reporting is enabled (i.e. crash upload is allowed), the enterprise policy
     * `FeedbackSurveysEnabled` which defaults to true decides whether surveys can be shown. When
     * metrics reporting is disabled, surveys can never be shown.
     *
     * @return a boolean indicating whether the user's configuration allows a survey to be shown.
     */
    private boolean configurationAllowsSurveys() {
        if (!isRightBrowserType()) return false;
        if (forceShowSurvey()) return true;

        // Do not include any logging to avoid reveal the fact user has crash upload disabled.
        boolean isCrashUploadAllowed =
                mCrashUploadPermissionSupplier.hasValue() && mCrashUploadPermissionSupplier.get();
        boolean isHatsEnabledByPolicy =
                UserPrefs.get(mProfile).getBoolean(Pref.FEEDBACK_SURVEYS_ENABLED);
        return isCrashUploadAllowed && isHatsEnabledByPolicy;
    }

    SurveyController getControllerForTesting() {
        return mController;
    }

    boolean isDestroyed() {
        return mIsDestroyed;
    }

    static boolean forceShowSurvey() {
        if (CommandLine.getInstance().hasSwitch(ChromeSwitches.CHROME_FORCE_ENABLE_SURVEY)) {
            return true;
        }
        return sForceShowSurveyForTesting != null && sForceShowSurveyForTesting;
    }

    static void setForceShowSurveyForTesting(Boolean forcedResult) {
        sForceShowSurveyForTesting = forcedResult;
        ResettersForTesting.register(() -> sForceShowSurveyForTesting = null);
    }
}
