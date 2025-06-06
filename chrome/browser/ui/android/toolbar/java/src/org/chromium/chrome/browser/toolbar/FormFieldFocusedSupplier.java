// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.toolbar;

import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.content_public.browser.ImeAdapter;
import org.chromium.content_public.browser.ImeEventObserver;
import org.chromium.content_public.browser.WebContents;

/**
 * Supplier of a boolean indicating whether an editable node is focused in the currently active
 * WebContents. Changes to the WebContents considered active must be reflected with calls to
 * onWebContentsChanged; this class does not attempt to track these changes directly. This class
 * also allows limited interaction with the current
 */
@NullMarked
public class FormFieldFocusedSupplier extends ObservableSupplierImpl<Boolean>
        implements ImeEventObserver {
    private @Nullable WebContents mWebContents;
    private @Nullable ImeAdapter mImeAdapter;

    public FormFieldFocusedSupplier() {
        super(false);
    }

    /**
     * Start tracking a new WebContents and stop tracking the previous one, if any. If
     * newWebContents is null, stop tracking any WebContents at all.
     */
    public void onWebContentsChanged(@Nullable WebContents newWebContents) {
        if (mWebContents == newWebContents) return;

        if (mImeAdapter != null) {
            mImeAdapter.removeEventObserver(this);
        }

        if (newWebContents == null) {
            mImeAdapter = null;
            set(false);
            return;
        }

        mWebContents = newWebContents;
        mImeAdapter = ImeAdapter.fromWebContents(mWebContents);
        mImeAdapter.addEventObserver(this);
        set(mImeAdapter.focusedNodeEditable());
    }

    @Override
    public void onNodeAttributeUpdated(boolean editable, boolean password) {
        super.set(editable);
    }

    public boolean getAsBoolean() {
        return hasValue() ? get() : false;
    }

    /**
     * See {@link ImeAdapter#resetAndHideKeyboard()}. Does nothing if there is no active ImeAdapter
     * for the current web contents.
     */
    public void resetAndHideKeyboard() {
        if (mImeAdapter != null) mImeAdapter.resetAndHideKeyboard();
    }
}
