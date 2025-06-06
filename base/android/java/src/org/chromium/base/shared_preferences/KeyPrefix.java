// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.shared_preferences;

import org.chromium.build.annotations.NullMarked;

/**
 * A prefix for a range of SharedPreferences keys generated dynamically.
 *
 * <p>Instances should be declared as keys in the PreferenceKeys registry.
 */
@NullMarked
public class KeyPrefix {
    private final String mPrefix;

    public KeyPrefix(String pattern) {
        // More thorough checking is performed in ChromePreferenceKeysTest.
        assert pattern.endsWith("*");
        mPrefix = pattern.substring(0, pattern.length() - 1);
    }

    /**
     * @param stem A non-empty string. The '*' character is reserved.
     * @return The complete SharedPreferences key to be passed to {@link SharedPreferencesManager}.
     */
    public String createKey(String stem) {
        return mPrefix + stem;
    }

    /**
     * @param index An int to generate a unique key.
     * @return The complete SharedPreferences key to be passed to {@link SharedPreferencesManager}.
     */
    public String createKey(int index) {
        return mPrefix + index;
    }

    public String pattern() {
        return mPrefix + "*";
    }

    public boolean hasGenerated(String key) {
        return key.startsWith(mPrefix);
    }
}
