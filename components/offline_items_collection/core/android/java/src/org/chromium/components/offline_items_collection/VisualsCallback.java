// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.offline_items_collection;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

/**
 * This interface is a Java counterpart to the C++ offline_items_collection::VisualsCallback meant
 * to be used in response to {@link OfflineItemVisuals} requests.
 */
@NullMarked
public interface VisualsCallback {
    /**
     * @param id      The {@link ContentId} that {@code visuals} is associated with.
     * @param visuals The {@link OfflineItemVisuals}, if any, associated with {@code id}.
     */
    void onVisualsAvailable(@Nullable ContentId id, @Nullable OfflineItemVisuals visuals);
}
