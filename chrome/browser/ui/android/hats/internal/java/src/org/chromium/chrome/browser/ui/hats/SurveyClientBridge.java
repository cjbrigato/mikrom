// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ui.hats;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.app.Activity;

import androidx.annotation.VisibleForTesting;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;

import org.chromium.base.Log;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcherProvider;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorSupplier;
import org.chromium.ui.base.WindowAndroid;

import java.util.HashMap;
import java.util.Map;

/**
 * Glue code that's used to pass C++ initiated SurveyClient to Java. Thus, this class should not be
 * initialized from Java.
 */
@JNINamespace("hats")
@NullMarked
class SurveyClientBridge implements SurveyClient {
    private static final String TAG = "SurveyClient";

    private final SurveyClient mDelegate;

    private SurveyClientBridge(SurveyClient delegate) {
        mDelegate = delegate;
    }

    @CalledByNative
    @VisibleForTesting
    static @Nullable SurveyClientBridge create(
            String trigger,
            SurveyUiDelegate uiDelegate,
            Profile profile,
            String suppliedTriggerId,
            WindowAndroid windowAndroid) {
        assert SurveyClientFactory.getInstance() != null;
        SurveyConfig config = SurveyConfig.get(profile, trigger, suppliedTriggerId);
        if (config == null) {
            return null;
        }

        var tabModelSelector = TabModelSelectorSupplier.getValueOrNullFrom(windowAndroid);
        // TODO(crbug.com/418075247): Remove nullness of tabModelSelector, currently it's required
        // for tests

        SurveyClient client =
                SurveyClientFactory.getInstance()
                        .createClient(config, uiDelegate, profile, tabModelSelector);
        if (client == null) {
            Log.d(TAG, "SurveyClient is null. config: " + SurveyConfig.toString(config));
            return null;
        }
        return new SurveyClientBridge(client);
    }

    /**
     * Called from Java to show a survey without PSD. Used if SurveyUiDelegate is created from C++.
     */
    @Override
    public void showSurvey(Activity activity, ActivityLifecycleDispatcher lifecycleDispatcher) {
        mDelegate.showSurvey(activity, lifecycleDispatcher);
    }

    /** Called from Java to show a survey with PSD. Used if SurveyUiDelegate is created from C++. */
    @Override
    public void showSurvey(
            Activity activity,
            @Nullable ActivityLifecycleDispatcher lifecycleDispatcher,
            Map<String, Boolean> surveyPsdBitValues,
            Map<String, String> surveyPsdStringValues) {
        mDelegate.showSurvey(
                activity, lifecycleDispatcher, surveyPsdBitValues, surveyPsdStringValues);
    }

    /** Called when a C++ client wants to display a survey with PSD. */
    @CalledByNative
    void showSurvey(
            WindowAndroid windowAndroid,
            String[] surveyPsdBitFields,
            boolean[] surveyPsdBitValues,
            String[] surveyPsdStringFields,
            String[] surveyPsdStringValues) {
        assert surveyPsdBitFields.length == surveyPsdBitValues.length;
        assert surveyPsdStringFields.length == surveyPsdStringValues.length;

        Map<String, Boolean> bitsValues = new HashMap<>();
        for (int i = 0; i < surveyPsdBitFields.length; ++i) {
            bitsValues.put(surveyPsdBitFields[i], surveyPsdBitValues[i]);
        }
        Map<String, String> stringValues = new HashMap<>();
        for (int i = 0; i < surveyPsdStringFields.length; ++i) {
            stringValues.put(surveyPsdStringFields[i], surveyPsdStringValues[i]);
        }

        Activity activity = windowAndroid.getActivity().get();
        ActivityLifecycleDispatcher lifecycleDispatcher = null;
        if (activity instanceof ActivityLifecycleDispatcherProvider) {
            // TODO(crbug/326643655): Allow access ActivityLifecycleDispatcher from WindowAndroid.
            lifecycleDispatcher =
                    ((ActivityLifecycleDispatcherProvider) activity).getLifecycleDispatcher();
        }
        showSurvey(assumeNonNull(activity), lifecycleDispatcher, bitsValues, stringValues);
    }
}
