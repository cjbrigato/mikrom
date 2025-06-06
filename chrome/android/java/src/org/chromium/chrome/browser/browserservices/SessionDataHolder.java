// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.browserservices;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.util.SparseArray;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import androidx.browser.customtabs.CustomTabsSessionToken;

import org.chromium.base.Callback;
import org.chromium.base.ResettersForTesting;
import org.chromium.chrome.browser.browserservices.intents.SessionHolder;
import org.chromium.chrome.browser.customtabs.CustomTabsConnection;

/**
 * Holds the currently active {@link SessionHandler} and redirects relevant intents and calls into
 * it. {@link SessionHandler} is an interface owned by the currently focused activity that has a
 * linkage to a third party client app through a session.
 */
public class SessionDataHolder {
    private final SparseArray<SessionData> mTaskIdToSessionData = new SparseArray<>();

    @Nullable private SessionHandler mActiveSessionHandler;

    @Nullable private Callback<SessionHolder<?>> mSessionDisconnectCallback;

    private static SessionDataHolder sInstance = new SessionDataHolder();

    @VisibleForTesting
    /* package */ SessionDataHolder() {}

    public static SessionDataHolder getInstance() {
        return sInstance;
    }

    public static void setInstanceForTesting(SessionDataHolder instance) {
        var oldValue = sInstance;
        sInstance = instance;
        ResettersForTesting.register(() -> sInstance = oldValue);
    }

    /** Data associated with a {@link SessionHandler} necessary to pass new intents to it. */
    private static class SessionData {
        public final SessionHolder<?> session;

        // Session handlers can reside in Activities of different types, so we need to store the
        // Activity class to be able to route new intents into it.
        public final Class<? extends Activity> activityClass;

        private SessionData(SessionHolder<?> session, Class<? extends Activity> activityClass) {
            this.session = session;
            this.activityClass = activityClass;
        }
    }

    /**
     * Sets the currently active {@link SessionHandler} in focus.
     *
     * @param sessionHandler {@link SessionHandler} to set.
     */
    public void setActiveHandler(@NonNull SessionHandler sessionHandler) {
        mActiveSessionHandler = sessionHandler;
        SessionHolder<?> session = sessionHandler.getSession();
        if (session == null) return;

        mTaskIdToSessionData.append(
                sessionHandler.getTaskId(),
                new SessionData(session, sessionHandler.getActivityClass()));
        ensureSessionCleanUpOnDisconnects();
    }

    /** Notifies that given {@link SessionHandler} no longer has focus. */
    public void removeActiveHandler(SessionHandler sessionHandler) {
        if (mActiveSessionHandler == sessionHandler) {
            mActiveSessionHandler = null;
        } // else this sessionHandler has already been replaced.

        // Intentionally not removing from mTaskIdToSessionData to handle cases when the task is
        // brought to foreground by a new intent - the CCT might not be able to call
        // setActiveHandler in time.
    }

    /**
     * Returns the class of Activity with a matching session running in the same task as the given
     * intent is being launched from, or null if no such Activity present.
     */
    @Nullable
    public Class<? extends Activity> getActiveHandlerClassInCurrentTask(
            Intent intent, Context context) {
        if (!(context instanceof Activity)) return null;
        int taskId = ((Activity) context).getTaskId();
        SessionData handlerDataInCurrentTask = mTaskIdToSessionData.get(taskId);
        if (handlerDataInCurrentTask == null
                || !handlerDataInCurrentTask.session.equals(
                        SessionHolder.getSessionHolderFromIntent(intent))) {
            return null;
        }
        return handlerDataInCurrentTask.activityClass;
    }

    /**
     * Attempts to handle an Intent.
     *
     * Checks whether an incoming intent can be handled by the current {@link SessionHandler}, and
     * if so, delegates the handling to it.
     *
     * @return Whether the active {@link SessionHandler} has handled the intent.
     */
    public boolean handleIntent(Intent intent) {
        SessionHandler handler = getActiveHandlerForIntent(intent);
        return handler != null && handler.handleIntent(intent);
    }

    /** Returns whether the given session is the currently active session. */
    public boolean isActiveSession(@Nullable SessionHolder<?> session) {
        return getActiveHandler(session) != null;
    }

    /**
     * Returns the active session handler if it is associated with given session, null otherwise.
     */
    public @Nullable SessionHandler getActiveHandler(@Nullable SessionHolder<?> session) {
        if (mActiveSessionHandler == null) return null;
        SessionHolder<?> activeSession = mActiveSessionHandler.getSession();
        if (activeSession == null || !activeSession.equals(session)) return null;
        return mActiveSessionHandler;
    }

    private @Nullable SessionHandler getActiveHandlerForIntent(Intent intent) {
        return getActiveHandler(SessionHolder.getSessionHolderFromIntent(intent));
    }

    /**
     * Checks whether the given referrer can be used as valid within the Activity launched with the
     * given {@link CustomTabsSessionToken}. For this to be true the token should correspond to the
     * currently in focus custom tab and also the related client should have a verified relationship
     * with the referrer origin. This can only be true for https:// origins.
     *
     * @param session The {@link SessionHolder} holding the session token specified in the activity
     *     launch intent.
     * @param referrer The referrer url that is to be used.
     * @return Whether the given referrer is a valid first party url to the client that launched the
     *     activity.
     */
    public boolean canActiveHandlerUseReferrer(@Nullable SessionHolder<?> session, Uri referrer) {
        SessionHandler handler = getActiveHandler(session);
        return handler != null && handler.canUseReferrer(referrer);
    }

    private void ensureSessionCleanUpOnDisconnects() {
        if (mSessionDisconnectCallback != null) return;
        mSessionDisconnectCallback =
                (session) -> {
                    if (session == null) {
                        return;
                    }
                    for (int i = 0; i < mTaskIdToSessionData.size(); i++) {
                        if (session.equals(mTaskIdToSessionData.valueAt(i).session)) {
                            mTaskIdToSessionData.removeAt(i);
                        }
                    }
                };
        CustomTabsConnection.getInstance().setDisconnectCallback(mSessionDisconnectCallback);
    }
}
