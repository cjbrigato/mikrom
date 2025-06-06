// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.browserservices;

import android.content.Context;
import android.content.SharedPreferences;

import org.chromium.base.ContextUtils;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.components.embedder_support.util.Origin;

import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

/**
 * Records whether Chrome has data relevant to an installed webapp (TWA or WebAPK).
 *
 * <p>Lifecycle: Most of the data used by this class modifies the underlying {@link
 * SharedPreferences} (which are global and preserved across Chrome restarts).
 */
@NullMarked
public class InstalledWebappDataRegister {
    /** The shared preferences file name. If you modify this you'll have to migrate old data. */
    private static final String PREFS_FILE = "trusted_web_activity_client_apps";

    /**
     * The key to the set of UIDs stored as strings in shared preferences. If you modify this
     * you'll have to migrate old data.
     */
    private static final String UIDS_KEY = "trusted_web_activity_uids";

    /* Preferences unique to this class. */
    private static @Nullable SharedPreferences sPreferences;

    private InstalledWebappDataRegister() {}

    private static SharedPreferences getPreferences() {
        // Not threadsafe, but doesn't matter as this is idempotent.
        if (sPreferences == null) {
            sPreferences =
                    ContextUtils.getApplicationContext()
                            .getSharedPreferences(PREFS_FILE, Context.MODE_PRIVATE);
        }
        return sPreferences;
    }

    // Trigger a Preferences read in a background thread to try to load the Preferences file
    // before we need it.
    public static void prefetchPreferences() {
        PostTask.postTask(
                TaskTraits.BEST_EFFORT,
                () -> {
                    getUids();
                });
    }

    /**
     * Saves to Preferences that the app with |uid| has the application name |appName| and when it
     * is removed or cleared, we should consider doing the same with Chrome data relevant to
     * |origin|. |domain| is stored as well in order to not have to derive it from origin while
     * handling uninstallation or data clear, since that would require loading native libraries.
     */
    public static void registerPackageForOrigin(
            int uid, String appName, String packageName, String domain, Origin origin) {
        // Store the UID in the main Chrome Preferences.
        Set<String> uids = getUids();
        uids.add(String.valueOf(uid));
        setUids(uids);

        SharedPreferences.Editor editor = getPreferences().edit();
        editor.putString(createAppNameKey(uid), appName);
        editor.putString(createPackageNameKey(uid), packageName);
        writeToSet(editor, createDomainKey(uid), domain);
        writeToSet(editor, createOriginKey(uid), origin.toString());
        editor.apply();
    }

    private static void writeToSet(SharedPreferences.Editor editor, String key, String newElement) {
        Set<String> set = new HashSet<>(getPreferences().getStringSet(key, Collections.emptySet()));
        set.add(newElement);
        editor.putStringSet(key, set);
    }

    private static void setUids(Set<String> uids) {
        getPreferences().edit().putStringSet(UIDS_KEY, uids).apply();
    }

    /* package */ static Set<String> getUids() {
        // We try to ensure that this is loaded on a background thread before it is needed (see
        // constructor), but if the load hasn't completed, disable StrictMode so we don't crash.
        return new HashSet<>(getPreferences().getStringSet(UIDS_KEY, Collections.emptySet()));
    }

    public static void removePackage(int uid) {
        Set<String> uids = getUids();
        uids.remove(String.valueOf(uid));
        setUids(uids);

        SharedPreferences.Editor editor = getPreferences().edit();
        editor.putString(createAppNameKey(uid), null);
        editor.putString(createPackageNameKey(uid), null);
        editor.putStringSet(createDomainKey(uid), null);
        editor.putStringSet(createOriginKey(uid), null);
        editor.apply();
    }

    /* package */ static boolean chromeHoldsDataForPackage(int uid) {
        return getUids().contains(String.valueOf(uid));
    }

    /** Gets the application name that was previously registered for the uid. */
    /* package */ static @Nullable String getAppNameForRegisteredUid(int uid) {
        return getPreferences().getString(createAppNameKey(uid), null);
    }

    /** Gets the package name that was previously registered for the uid. */
    /* package */ static @Nullable String getPackageNameForRegisteredUid(int uid) {
        return getPreferences().getString(createPackageNameKey(uid), null);
    }

    /**
     * Gets all the domains that have been registered for the uid. Do not modify the set returned by
     * this method.
     */
    /* package */ static Set<String> getDomainsForRegisteredUid(int uid) {
        return getPreferences().getStringSet(createDomainKey(uid), Collections.emptySet());
    }

    /**
     * Gets all the origins that have been registered for the uid. Do not modify the set returned by
     * this method.
     */
    /* package */ static Set<String> getOriginsForRegisteredUid(int uid) {
        return getPreferences().getStringSet(createOriginKey(uid), Collections.emptySet());
    }

    // Methods below create the Preferences keys to access the data associated with given app uid.
    // If you modify any of them you'll have to migrate old data.

    private static String createAppNameKey(int uid) {
        return uid + ".appName";
    }

    private static String createPackageNameKey(int uid) {
        return uid + ".packageName";
    }

    private static String createDomainKey(int uid) {
        return uid + ".domain";
    }

    private static String createOriginKey(int uid) {
        return uid + ".origin";
    }
}
