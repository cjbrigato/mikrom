// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.keyboard_accessory;

import org.chromium.build.annotations.NullMarked;

/** Use {@link #createComponent()} to instantiate a {@link ManualFillingComponent}. */
@NullMarked
public class ManualFillingComponentFactory {
    private ManualFillingComponentFactory() {}

    /**
     * Creates a {@link ManualFillingCoordinator}.
     * @return A {@link ManualFillingCoordinator}.
     */
    public static ManualFillingComponent createComponent() {
        return new ManualFillingCoordinator();
    }
}
