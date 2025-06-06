// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tab;

import org.chromium.build.annotations.NullMarked;
import org.chromium.chrome.browser.preferences.ChromePreferenceKeys;
import org.chromium.chrome.browser.preferences.ChromeSharedPreferences;

import java.util.concurrent.atomic.AtomicInteger;

/**
 * Maintains a monotonically increasing ID that is used for uniquely identifying {@link Tab}s. This
 * class is responsible for ensuring that Tabs created in the same process, across every TabModel,
 * are allocated a unique ID. Note that only the browser process should be generating Tab IDs to
 * prevent collisions.
 *
 * <p>Calling {@link TabIdManager#incrementIdCounterTo(int)} will ensure new {@link Tab}s get IDs
 * greater than or equal to the parameter passed to that method. This should be used when doing
 * things like loading persisted {@link Tab}s from disk on process start to ensure all new {@link
 * Tab}s don't have id collision.
 *
 * <p>TODO(dfalcantara): Tab ID generation prior to M45 is haphazard and dependent on which Activity
 * is started first. Unify the ways the maximum Tab ID is set (crbug.com/502384).
 */
@NullMarked
public class TabIdManager {
    private static TabIdManager sInstance = new TabIdManager();

    private final AtomicInteger mIdCounter = new AtomicInteger();

    /** Returns the Singleton instance of the TabIdManager. */
    public static TabIdManager getInstance() {
        return sInstance;
    }

    /**
     * Validates {@code id} and increments the internal counter to make sure future ids don't
     * collide.
     * @param id The current id.  May be {@link Tab#INVALID_TAB_ID}.
     * @return A new id if {@code id} was {@link Tab#INVALID_TAB_ID}, or {@code id}.
     */
    public final int generateValidId(int id) {
        if (id == Tab.INVALID_TAB_ID) id = mIdCounter.getAndIncrement();
        incrementIdCounterTo(id + 1);
        return id;
    }

    /**
     * Ensures the counter is at least as high as the specified value.  The counter should always
     * point to an unused ID (which will be handed out next time a request comes in).  Exposed so
     * that anything externally loading tabs and ids can set enforce new tabs start at the correct
     * id.
     * TODO(dfalcantara): Reduce the visibility of this method once all TabModels are united in how
     *                    the IDs are assigned (crbug.com/502384).
     * @param id The minimum id we should hand out to the next new tab.
     */
    public final void incrementIdCounterTo(int id) {
        int diff = id - mIdCounter.get();
        if (diff < 0) return;

        // It's possible idCounter has been incremented between the get and the add but that's OK --
        // in the worst case mIdCounter will just be overly incremented.
        mIdCounter.addAndGet(diff);
        ChromeSharedPreferences.getInstance()
                .writeInt(ChromePreferenceKeys.TAB_ID_MANAGER_NEXT_ID, mIdCounter.get());
    }

    private TabIdManager() {
        // Read the shared preference.  This has to be done on the critical path to ensure that the
        // myriad Activities that serve as entries into Chrome are all synchronized on the correct
        // maximum Tab ID.
        mIdCounter.set(
                ChromeSharedPreferences.getInstance()
                        .readInt(ChromePreferenceKeys.TAB_ID_MANAGER_NEXT_ID));
    }

    public static void resetInstanceForTesting() {
        sInstance = new TabIdManager();
    }
}
