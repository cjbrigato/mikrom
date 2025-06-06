// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.suggestions;

import org.chromium.base.metrics.RecordUserAction;
import org.chromium.build.annotations.NullMarked;
import org.chromium.chrome.browser.preferences.ChromePreferenceKeys;
import org.chromium.chrome.browser.preferences.ChromeSharedPreferences;

/** Exposes methods to report suggestions related events, for UMA or Fetch scheduling purposes. */
@NullMarked
public abstract class SuggestionsMetrics {
    private SuggestionsMetrics() {}

    // UI Element interactions

    public static void recordSurfaceVisible() {
        if (!ChromeSharedPreferences.getInstance()
                .readBoolean(ChromePreferenceKeys.CONTENT_SUGGESTIONS_SHOWN, false)) {
            RecordUserAction.record("Suggestions.FirstTimeSurfaceVisible");
            ChromeSharedPreferences.getInstance()
                    .writeBoolean(ChromePreferenceKeys.CONTENT_SUGGESTIONS_SHOWN, true);
        }

        RecordUserAction.record("Suggestions.SurfaceVisible");
    }

    public static void recordSurfaceHidden() {
        RecordUserAction.record("Suggestions.SurfaceHidden");
    }

    public static void recordTileTapped() {
        RecordUserAction.record("Suggestions.Tile.Tapped");
    }

    public static void recordExpandableHeaderTapped(boolean expanded) {
        if (expanded) {
            RecordUserAction.record("Suggestions.ExpandableHeader.Expanded");
        } else {
            RecordUserAction.record("Suggestions.ExpandableHeader.Collapsed");
        }
    }
}
