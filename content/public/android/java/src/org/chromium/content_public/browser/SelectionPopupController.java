// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content_public.browser;

import android.view.ActionMode;
import android.view.textclassifier.TextClassifier;

import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.content.browser.selection.SelectionPopupControllerImpl;
import org.chromium.content_public.browser.selection.SelectionActionMenuDelegate;
import org.chromium.content_public.browser.selection.SelectionDropdownMenuDelegate;
import org.chromium.ui.base.WindowAndroid;

/**
 * An interface that handles input-related web content selection UI like action mode
 * and paste popup view. It wraps an {@link ActionMode} created by the associated view,
 * providing modified interaction with it.
 *
 * Embedders can use {@link ActionModeCallbackHelper} provided by the implementation of
 * this interface to create {@link ActionMode.Callback} instance and configure the selection
 * action mode tasks to their requirements.
 */
@NullMarked
public interface SelectionPopupController {
    // User action of clicking on the Share option within the selection UI.
    static final String UMA_MOBILE_ACTION_MODE_SHARE = "MobileActionMode.Share";

    /**
     * @param webContents A non-destroyed {@link WebContents} object.
     * @return {@link SelectionPopupController} object used for the give WebContents.
     */
    static SelectionPopupController fromWebContents(WebContents webContents) {
        var ret = SelectionPopupControllerImpl.fromWebContents(webContents);
        assert ret != null;
        return ret;
    }

    /**
     * @param webContents {@link WebContents} object.
     * @return {@link SelectionPopupController} object used for the given WebContents if created.
     *         {@code null} if not available.
     */
    static @Nullable SelectionPopupController fromWebContentsNoCreate(WebContents webContents) {
        return SelectionPopupControllerImpl.fromWebContentsNoCreate(webContents);
    }

    /**
     * Makes {@link SelectionPopupcontroller} to use the readback view from {@link WindowAndroid}
     */
    static void setShouldGetReadbackViewFromWindowAndroid() {
        SelectionPopupControllerImpl.setShouldGetReadbackViewFromWindowAndroid();
    }

    /** Set allow using magnifer built using surface control instead of the system-proivded one. */
    static void setAllowSurfaceControlMagnifier() {
        SelectionPopupControllerImpl.setAllowSurfaceControlMagnifier();
    }

    /** Check if need to disable SurfaceControl during selection. */
    static boolean needsSurfaceViewDuringSelection() {
        return !SelectionPopupControllerImpl.isMagnifierWithSurfaceControlSupported();
    }

    /** Set {@link ActionModeCallback} used by {@link SelectionPopupController}. */
    void setActionModeCallback(ActionModeCallback callback);

    /**
     * @return {@link SelectionClient.ResultCallback} instance.
     */
    SelectionClient.ResultCallback getResultCallback();

    /**
     * @return The selected text (empty if no text selected).
     */
    String getSelectedText();

    /**
     * @return Whether the current focused node is editable.
     */
    boolean isFocusedNodeEditable();

    /**
     * @return Whether the page has an active, touch-controlled selection region.
     */
    boolean hasSelection();

    /** Hide action mode and put into destroyed state. */
    void destroySelectActionMode();

    boolean isSelectActionBarShowing();

    /**
     * @return An {@link ObservableSupplier<Boolean>} which holds true when a selection action bar
     *         is showing; otherwise, it holds false.
     */
    ObservableSupplier<Boolean> isSelectActionBarShowingSupplier();

    /**
     * @return {@link ActionModeCallbackHelper} object.
     */
    ActionModeCallbackHelper getActionModeCallbackHelper();

    /**
     * Clears the current text selection. Note that we will try to move cursor to selection
     * end if applicable.
     */
    void clearSelection();

    /**
     * Replaces the current selection in editable field.
     * @param text String with which current selection need to be replaced.
     */
    void handleTextReplacementAction(String text);

    /** Sets the given {@link SelectionClient} in the selection popup controller. */
    void setSelectionClient(@Nullable SelectionClient selectionClient);

    /** Returns the {@link SelectionClient} in the selection popup controller. */
    public @Nullable SelectionClient getSelectionClient();

    /** Sets TextClassifier for Smart Text selection. */
    void setTextClassifier(TextClassifier textClassifier);

    /**
     * Returns TextClassifier that is used for Smart Text selection. If the custom classifier
     * has been set with setTextClassifier, returns that object, otherwise returns the system
     * classifier.
     */
    @Nullable
    TextClassifier getTextClassifier();

    /** Returns the TextClassifier which has been set with setTextClassifier(), or null. */
    @Nullable
    TextClassifier getCustomTextClassifier();

    /**
     * Set the flag indicating where the selection is preserved the next time the view loses focus.
     * @param preserve {@code true} if the selection needs to be preserved.
     */
    void setPreserveSelectionOnNextLossOfFocus(boolean preserve);

    /**
     * Update the text selection UI depending on the focus of the page. This will hide the selection
     * handles and selection popups if focus is lost.
     * TODO(mdjones): This was added as a temporary measure to hide text UI while Reader Mode or
     * Contextual Search are showing. This should be removed in favor of proper focusing of the
     * panel's WebContents (which is currently not being added to the view hierarchy).
     * @param focused If the WebContents currently has focus.
     */
    void updateTextSelectionUI(boolean focused);

    /** Set the dropdown menu delegate that handles showing a dropdown style text selection menu. */
    void setDropdownMenuDelegate(SelectionDropdownMenuDelegate dropdownMenuDelegate);

    /**
     * Set the {@link SelectionActionMenuDelegate} used by {@link SelectionPopupController} while
     * modifying menu items.
     */
    void setSelectionActionMenuDelegate(@Nullable SelectionActionMenuDelegate delegate);

    /**
     * Returns the {@link SelectionActionMenuDelegate} used by {@link SelectionPopupController}
     * while modifying menu items.
     *
     * @return SelectionActionMenuDelegate instance if available, Otherwise Null.
     */
    @Nullable
    SelectionActionMenuDelegate getSelectionActionMenuDelegate();
}
