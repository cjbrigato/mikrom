// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omaha.metrics;

import android.util.Base64;

import org.chromium.base.Promise;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskRunner;
import org.chromium.base.task.TaskTraits;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.omaha.OmahaPrefUtils;
import org.chromium.chrome.browser.omaha.metrics.UpdateProtos.Tracking;

/** A helper class to manage retrieving and storing a persisted instance of {@link Tracking}. */
@NullMarked
class TrackingProvider {
    private static final String TRACKING_PERSISTENT_KEY = "UpdateProtos_Tracking";

    private final TaskRunner mTaskRunner;

    /**Builds a new instance of TrackingProvider. */
    public TrackingProvider() {
        mTaskRunner = PostTask.createSequencedTaskRunner(TaskTraits.BEST_EFFORT);
    }

    /** @return The persisted instance of {@link Tracking} or {@code null} if none is saved. */
    public Promise<@Nullable Tracking> get() {
        final Promise<@Nullable Tracking> promise = new Promise<>();

        mTaskRunner.execute(
                () -> {
                    Tracking state = null;

                    String serialized =
                            OmahaPrefUtils.getSharedPreferences()
                                    .getString(TRACKING_PERSISTENT_KEY, null);
                    if (serialized != null) {
                        try {
                            state = Tracking.parseFrom(Base64.decode(serialized, Base64.DEFAULT));
                        } catch (com.google.protobuf.InvalidProtocolBufferException e) {
                        }
                    }

                    final Tracking finalState = state;
                    PostTask.postTask(TaskTraits.UI_DEFAULT, () -> promise.fulfill(finalState));
                });

        return promise;
    }

    /** Clears any persisted instance of {@link Tracking}. */
    public void clear() {
        mTaskRunner.execute(
                () ->
                        OmahaPrefUtils.getSharedPreferences()
                                .edit()
                                .remove(TRACKING_PERSISTENT_KEY)
                                .apply());
    }

    /**
     * Persists {@code state}, overwriting any currently persisted instance of {@link Tracking}.
     *
     * @param state The new instance of {@link Tracking} to persist.
     */
    public void put(Tracking state) {
        mTaskRunner.execute(
                () -> {
                    String serialized = Base64.encodeToString(state.toByteArray(), Base64.DEFAULT);
                    OmahaPrefUtils.getSharedPreferences()
                            .edit()
                            .putString(TRACKING_PERSISTENT_KEY, serialized)
                            .apply();
                });
    }
}
