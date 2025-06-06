// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tab.state;

import org.chromium.base.Callback;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

import java.nio.ByteBuffer;

/**
 * Creates a {@link PersistedTabData}
 * @param <T> {@link PersistedTabData} being created
 */
@NullMarked
public interface PersistedTabDataFactory<T extends @Nullable PersistedTabData> {
    /**
     * @param data serialized {@link PersistedTabData}
     * @param storage storage method {@link PersistedTabDataStorage} for {@link PersistedTabData}
     * @param id identifier for {@link PersistedTabData} in storage
     * @param callback {@link Callback} the {@link PersistedTabData} is passed back in
     */
    void create(
            ByteBuffer data,
            PersistedTabDataStorage storage,
            String id,
            Callback<@Nullable T> callback);
}
