// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.browser_ui.widget.image_tiles;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

/** Provides the configuration params required by the tiles UI. */
@NullMarked
public class TileConfig {
    public final @Nullable String umaPrefix;

    /** Constructor. */
    private TileConfig(Builder builder) {
        umaPrefix = builder.mUmaPrefix;
    }

    /** Helper class for building a {@link TileConfig}. */
    public static class Builder {
        private @Nullable String mUmaPrefix;

        /**
         * Sets the histogram prefix to be used while collecting metrics.
         * @param umaPrefix The prefix to be used for histograms.
         * @return A {@link Builder} instance.
         */
        public Builder setUmaPrefix(String umaPrefix) {
            mUmaPrefix = umaPrefix;
            return this;
        }

        public TileConfig build() {
            return new TileConfig(this);
        }
    }
}
