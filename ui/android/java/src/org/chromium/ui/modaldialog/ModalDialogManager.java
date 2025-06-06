// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.ui.modaldialog;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.util.SparseArray;
import android.view.View;

import androidx.activity.ComponentDialog;
import androidx.annotation.IntDef;

import org.chromium.base.Callback;
import org.chromium.base.CommandLine;
import org.chromium.base.ObserverList;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.build.annotations.EnsuresNonNullIf;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.ui.UiSwitches;
import org.chromium.ui.insets.InsetObserver;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.util.TokenHolder;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Manager for managing the display of a queue of {@link PropertyModel}s. */
@NullMarked
public class ModalDialogManager {
    /**
     * An observer of the ModalDialogManager intended to broadcast notifications about any dialog
     * being shown. Observers will know if something is overlaying the screen.
     */
    public interface ModalDialogManagerObserver {
        /**
         * A notification that the manager queues a dialog to be shown.
         *
         * @param model The model that describes the dialog that was added.
         */
        default void onDialogAdded(PropertyModel model) {}

        /**
         * A notification that the manager has created the dialog but not shown it yet.
         *
         * @param model The model that describes the dialog that was dismissed.
         * @param dialog The ComponentDialog associated with the {@link AppModalPresenter}
         *     implementation of the modal dialog. For dialog types that don't use the
         *     ComponentDialog internally, this value will be null.
         */
        default void onDialogCreated(PropertyModel model, @Nullable ComponentDialog dialog) {}

        /**
         * A notification that the manager showed a modal dialog.
         *
         * @param dialogView The view associated with the modal dialog.
         */
        default void onDialogShown(View dialogView) {}

        /**
         * A notification that the manager dismisses a modal dialog.
         *
         * @param model The model that describes the dialog that was dismissed.
         */
        default void onDialogDismissed(PropertyModel model) {}

        /**
         * A notification that the manager has suppressed a modal dialog because it is suspended.
         *
         * @param model The model that describes the dialog that was suppressed.
         */
        default void onDialogSuppressed(PropertyModel model) {}

        /** A notification that the manager has dismissed all queued modal dialog. */
        default void onLastDialogDismissed() {}
    }

    /** Present a {@link PropertyModel} in a container. */
    public abstract static class Presenter {
        private @Nullable Callback<Integer> mDismissCallback;
        private @Nullable PropertyModel mDialogModel;

        /**
         * @param model The dialog model that's currently showing in this presenter. If null, no
         *     dialog is currently showing.
         */
        private void setDialogModel(
                @Nullable PropertyModel model,
                @Nullable Callback<Integer> dismissCallback,
                @Nullable Callback<ComponentDialog> onDialogCreatedCallback,
                @Nullable Callback<View> onDialogShownCallback) {
            if (model == null) {
                removeDialogView(mDialogModel);
                mDialogModel = null;
                mDismissCallback = null;
            } else {
                assert mDialogModel == null
                        : "Should call setDialogModel(null) before setting a dialog model.";
                mDialogModel = model;
                mDismissCallback = dismissCallback;
                addDialogView(model, onDialogCreatedCallback, onDialogShownCallback);
            }
        }

        /** Run the cached cancel callback and reset the cached callback. */
        public final void dismissCurrentDialog(@DialogDismissalCause int dismissalCause) {
            if (mDismissCallback == null) return;

            // Set #mCancelCallback to null before calling the callback to avoid it being
            // updated during the callback.
            Callback<Integer> callback = mDismissCallback;
            mDismissCallback = null;
            callback.onResult(dismissalCause);
        }

        /**
         * @return The dialog model that this presenter is showing.
         */
        public final @Nullable PropertyModel getDialogModel() {
            return mDialogModel;
        }

        /**
         * @param model The dialog model from which the properties should be obtained.
         * @return The property value for {@link ModalDialogProperties#CONTENT_DESCRIPTION}, or a
         *         fallback content description if it is not set.
         */
        protected static String getContentDescription(PropertyModel model) {
            String description = model.get(ModalDialogProperties.CONTENT_DESCRIPTION);
            if (description == null) description = model.get(ModalDialogProperties.TITLE);
            assert description != null;
            return description;
        }

        /**
         * Creates a view for the specified dialog model and puts the view in a container.
         *
         * @param model The dialog model that needs to be shown.
         * @param onDialogCreatedCallback The callback that notifies observers when the dialog is
         *     created but not shown yet, providing the ComponentDialog associated with the {@link
         *     AppModalPresenter} implementation of this modal dialog.
         * @param onDialogShownCallback The callback that notifies observers after triggering
         *     `showDialog`.
         */
        protected abstract void addDialogView(
                PropertyModel model,
                @Nullable Callback<ComponentDialog> onDialogCreatedCallback,
                @Nullable Callback<View> onDialogShownCallback);

        /**
         * Removes the view created for the specified model from a container.
         *
         * @param model The dialog model that needs to be removed.
         */
        protected abstract void removeDialogView(@Nullable PropertyModel model);

        /**
         * An {@link InsetObserver} to get insets for the window associated with a modal dialog.
         *
         * @param insetObserver The observer to set.
         */
        protected void setInsetObserver(InsetObserver insetObserver) {}

        /**
         * A supplier to determine whether edge-to-edge is active in the enclosing window.
         *
         * @param edgeToEdgeStateSupplier The supplier for edge-to-edge state.
         * @param isEdgeToEdgeEverywhereEnabled Whether the edge-to-edge-everywhere feature is
         *     enabled.
         */
        protected void setEdgeToEdgeStateSupplier(
                ObservableSupplier<Boolean> edgeToEdgeStateSupplier,
                boolean isEdgeToEdgeEverywhereEnabled) {}
    }

    // This affects only the dialog style. To define a priority, call showDialog with {@link
    // ModalDialogPriority} instead.
    @IntDef({ModalDialogType.TAB, ModalDialogType.APP})
    @Retention(RetentionPolicy.SOURCE)
    public @interface ModalDialogType {
        // If priority is not specified then the integer values here represent the default
        // priorities where a lower value indicates a lower priority.
        int TAB = 0;
        int APP = 1;

        int RANGE_MIN = TAB;
        int RANGE_MAX = APP;
        int NUM_ENTRIES = RANGE_MAX - RANGE_MIN + 1;
    }

    /**
     * This signifies the priority of the dialog. A priority of the dialog influences
     * which dialog will be shown or hidden if there's more than one dialog in the queue of dialogs
     * in {@link PendingDialogContainer}.
     */
    @IntDef({ModalDialogPriority.LOW, ModalDialogPriority.HIGH, ModalDialogPriority.VERY_HIGH})
    @Retention(RetentionPolicy.SOURCE)
    public @interface ModalDialogPriority {
        int LOW = 1;
        int HIGH = 2;

        // This is intended to be used only by those dialogs which are meant to block any access to
        // a subset of Chrome features when they are being shown. This also decouples the dialog
        // from any suspend calls! For example, incognito re-auth feature uses this to gate the
        // user's access to Incognito feature unless they re-authenticate successfully and it
        // ensures that the dialog doesn't get removed because of any other Chrome clients.
        // STOP: Other Chrome clients should just rely on HIGH instead! Check with the existing
        // clients if you still intend on using this.
        int VERY_HIGH = 3;

        int RANGE_MIN = LOW;
        // Please note that the max value of {@link ModalDialogPriority} should never exceed 9
        // because of how {@link PendingDialogContainer} is built.
        int RANGE_MAX = VERY_HIGH;
        int NUM_ENTRIES = RANGE_MAX - RANGE_MIN + 1;
    }

    /**
     * This is for identifying individual dialog types. If a dialog needs identifying, add an entry
     * for the type here, set it in the property model and then you can query the name field.
     */
    @IntDef({DialogName.UNKNOWN, DialogName.DUPLICATE_DOWNLOAD_DIALOG})
    @Retention(RetentionPolicy.SOURCE)
    public @interface DialogName {
        // Dialog name wasn't set.
        int UNKNOWN = 0;
        // Dialog shown when there is a file name collision.
        int DUPLICATE_DOWNLOAD_DIALOG = 1;
        int NUM_ENTRIES = 2;
    }

    /** Mapping of the {@link Presenter}s and the type of dialogs they are showing. */
    private final SparseArray<Presenter> mPresenters = new SparseArray<>();

    /**
     * The list of suspended types of dialogs. The dialogs of types in the list will be suspended
     * from showing and will only be shown after {@link #resumeType(int)} is called.
     */
    private final Set<Integer> mSuspendedTypes = new HashSet<>();

    /** The default presenter to be used if a specified type is not supported. */
    private final Presenter mDefaultPresenter;

    /**
     * The presenter of the type of the dialog that is currently showing. Note that if there is no
     * matching {@link Presenter} for {@link #mCurrentType}, this will be the default presenter.
     */
    private @Nullable Presenter mCurrentPresenter;

    /**
     * The type of the current dialog. This can be different from the type of the current
     * {@link Presenter} if there is no registered presenter for this type.
     */
    private @ModalDialogType int mCurrentType;

    /** The priority of the current dialog. */
    private @ModalDialogPriority int mCurrentPriority;

    /** True if the current dialog is in the process of being dismissed. */
    private boolean mDismissingCurrentDialog;

    private @Nullable ModalDialogManagerBridge mModalDialogManagerBridge;

    private boolean mDestroyed;

    /** Observers of this manager. */
    private final ObserverList<ModalDialogManagerObserver> mObserverList = new ObserverList<>();

    /** Tokens for features temporarily suppressing dialogs. */
    private final Map<Integer, TokenHolder> mTokenHolders = new HashMap<>();

    /**
     * A container to insert pending dialogs on both {@link ModalDialogType} and {@link
     * ModalDialogPriority} attributes.
     */
    private final PendingDialogContainer mPendingDialogContainer = new PendingDialogContainer();

    /** An {@link InsetObserver} to provide system window insets. */
    private @Nullable InsetObserver mInsetObserver;

    /** A supplier to determine whether edge-to-edge is active in the enclosing window. */
    private final @Nullable ObservableSupplier<Boolean> mEdgeToEdgeStateSupplier;

    private final boolean mIsEdgeToEdgeEverywhereEnabled;

    /**
     * Constructor for initializing default {@link Presenter}. TODO (crbug.com/41492646): Remove
     * this constructor in favor of the one depending on E2E when this bug is addressed.
     *
     * @param defaultPresenter The default presenter to be used when no presenter specified.
     * @param defaultType The dialog type of the default presenter.
     */
    public ModalDialogManager(Presenter defaultPresenter, @ModalDialogType int defaultType) {
        this(
                defaultPresenter,
                defaultType,
                /* edgeToEdgeStateSupplier= */ null,
                /* isEdgeToEdgeEverywhereEnabled= */ false);
    }

    /**
     * Constructor for initializing default {@link Presenter}, when knowledge of edge-to-edge state
     * is required.
     *
     * @param defaultPresenter The default presenter to be used when no presenter specified.
     * @param defaultType The dialog type of the default presenter.
     * @param edgeToEdgeStateSupplier Supplier to determine whether edge-to-edge is active. This
     *     will be used to account for system bars insets in dialog margin calculations when
     *     applicable.
     * @param isEdgeToEdgeEverywhereEnabled Whether the edge-to-edge-everywhere feature is enabled.
     */
    public ModalDialogManager(
            Presenter defaultPresenter,
            @ModalDialogType int defaultType,
            @Nullable ObservableSupplier<Boolean> edgeToEdgeStateSupplier,
            boolean isEdgeToEdgeEverywhereEnabled) {
        mDefaultPresenter = defaultPresenter;
        mEdgeToEdgeStateSupplier = edgeToEdgeStateSupplier;
        mIsEdgeToEdgeEverywhereEnabled = isEdgeToEdgeEverywhereEnabled;
        registerPresenter(defaultPresenter, defaultType);

        mTokenHolders.put(
                ModalDialogType.APP,
                new TokenHolder(() -> resumeTypeInternal(ModalDialogType.APP)));
        mTokenHolders.put(
                ModalDialogType.TAB,
                new TokenHolder(() -> resumeTypeInternal(ModalDialogType.TAB)));
    }

    /** Clears any dependencies on the showing or pending dialogs. */
    public void destroy() {
        dismissAllDialogs(DialogDismissalCause.ACTIVITY_DESTROYED);
        mObserverList.clear();
        if (mModalDialogManagerBridge != null) {
            mModalDialogManagerBridge.destroyNative();
            mModalDialogManagerBridge = null;
        }
        mDestroyed = true;
    }

    /**
     * Add an observer to this manager.
     *
     * @param observer The observer to add.
     */
    public void addObserver(ModalDialogManagerObserver observer) {
        mObserverList.addObserver(observer);
    }

    /**
     * Remove an observer of this manager.
     *
     * @param observer The observer to remove.
     */
    public void removeObserver(ModalDialogManagerObserver observer) {
        mObserverList.removeObserver(observer);
    }

    /**
     * Set the {@link InsetObserver} to get insets for the window associated with a modal dialog.
     *
     * @param observer The observer to set.
     */
    public void setInsetObserver(InsetObserver observer) {
        mInsetObserver = observer;
        for (int i = 0; i < mPresenters.size(); i++) {
            mPresenters.valueAt(i).setInsetObserver(observer);
        }
    }

    /**
     * Register a {@link Presenter} that shows a specific type of dialog. Note that only one
     * presenter of each type can be registered.
     *
     * @param presenter The {@link Presenter} to be registered.
     * @param dialogType The type of the dialog shown by the specified presenter.
     */
    public void registerPresenter(Presenter presenter, @ModalDialogType int dialogType) {
        assert mPresenters.get(dialogType) == null
                : "Only one presenter can be registered for each type.";
        mPresenters.put(dialogType, presenter);
        if (mInsetObserver != null) {
            presenter.setInsetObserver(mInsetObserver);
        }
        if (mEdgeToEdgeStateSupplier != null) {
            presenter.setEdgeToEdgeStateSupplier(
                    mEdgeToEdgeStateSupplier, mIsEdgeToEdgeEverywhereEnabled);
        }
    }

    /**
     * @return Whether a dialog is currently showing.
     */
    @EnsuresNonNullIf("mCurrentPresenter")
    public boolean isShowing() {
        return mCurrentPresenter != null;
    }

    /**
     * @return Whether dialogs of the specified type are suspended.
     */
    public boolean isSuspended(@ModalDialogType int dialogType) {
        return mSuspendedTypes.contains(dialogType);
    }

    /**
     * @return The type of dialog showing, or last type that was shown.
     */
    public @ModalDialogType int getCurrentType() {
        return mCurrentType;
    }

    /**
     * Show the specified dialog. If another dialog of higher priority is currently showing, the
     * specified dialog will be added to the end of the pending dialog list of the specified type.
     *
     * @param model The dialog model to be shown or added to pending list.
     * @param dialogType The type of the dialog to be shown.
     */
    public void showDialog(PropertyModel model, @ModalDialogType int dialogType) {
        showDialog(model, dialogType, false);
    }

    /**
     * Show the specified dialog. If another dialog of higher priority is currently showing, the
     * specified dialog will be added to the end of the pending dialog list of the specified type.
     *
     * @param model The dialog model to be shown or added to pending list.
     * @param dialogType The type of the dialog to be shown.
     * @param dialogPriority The priority of the dialog to be shown.
     */
    public void showDialog(
            PropertyModel model,
            @ModalDialogType int dialogType,
            @ModalDialogPriority int dialogPriority) {
        showDialog(model, dialogType, dialogPriority, false);
    }

    /**
     * Show the specified dialog. If another dialog of higher priority is currently showing, the
     * specified dialog will be added to the pending dialog list. If showNext is set to true, the
     * dialog will be added to the top of the pending list of its type, otherwise it will be added
     * to the end. The priority of the specified dialog is inferred from the type of the dialog.
     *
     * @param model The dialog model to be shown or added to pending list.
     * @param dialogType The type of the dialog to be shown.
     * @param showAsNext Whether the specified dialog should be set highest priority of its type.
     */
    public void showDialog(
            PropertyModel model, @ModalDialogType int dialogType, boolean showAsNext) {
        showDialog(model, dialogType, getDefaultPriorityByType(dialogType), showAsNext);
    }

    /**
     * Show the specified dialog. If another dialog of higher priority is currently showing, the
     * specified dialog will be added to the pending dialog list. If showNext is set to true, the
     * dialog will be added to the top of the pending list of its type, otherwise it will be added
     * to the end.
     *
     * @param model The dialog model to be shown or added to pending list.
     * @param dialogType The type of the dialog to be shown.
     * @param dialogPriority The priority of the dialog to be shown.
     * @param showAsNext Whether the specified dialog should be set highest priority of its type.
     */
    public void showDialog(
            PropertyModel model,
            @ModalDialogType int dialogType,
            @ModalDialogPriority int dialogPriority,
            boolean showAsNext) {
        if (CommandLine.getInstance().hasSwitch(UiSwitches.ENABLE_SCREENSHOT_UI_MODE)) {
            return;
        }

        // The requested dialog is of very high priority. This needs special treatment when
        // considering to put in pending list or not.
        if (dialogPriority == ModalDialogPriority.VERY_HIGH) {
            // We only put the requested dialog in pending list if the currently shown dialog
            // also has a VERY_HIGH priority.
            if (isShowing() && mCurrentPriority >= dialogPriority) {
                assert mCurrentPriority == ModalDialogPriority.VERY_HIGH
                        : "Higher priority is not supported.";
                mPendingDialogContainer.put(dialogType, dialogPriority, model, showAsNext);
                return;
            }
        } else {
            // Put the new dialog in pending list if the dialog type is suspended or the current
            // dialog is of higher priority.
            if (mSuspendedTypes.contains(dialogType)
                    || (isShowing() && mCurrentPriority >= dialogPriority)) {
                mPendingDialogContainer.put(dialogType, dialogPriority, model, showAsNext);
                for (ModalDialogManagerObserver o : mObserverList) o.onDialogSuppressed(model);
                return;
            }
        }

        if (isShowing()) suspendCurrentDialog();

        assert !isShowing();
        mCurrentType = dialogType;
        mCurrentPriority = dialogPriority;
        mCurrentPresenter = mPresenters.get(dialogType, mDefaultPresenter);
        mCurrentPresenter.setDialogModel(
                model,
                (dismissalCause) -> dismissDialog(model, dismissalCause),
                (dialog) -> {
                    for (ModalDialogManagerObserver o : mObserverList) {
                        o.onDialogCreated(model, dialog);
                    }
                },
                (dialogView) -> {
                    for (ModalDialogManagerObserver o : mObserverList) {
                        o.onDialogShown(dialogView);
                    }
                });
        for (ModalDialogManagerObserver o : mObserverList) o.onDialogAdded(model);
    }

    /**
     * Dismiss the specified dialog. If the dialog is not currently showing, it will be removed from
     * the pending dialog list. If the dialog is currently being dismissed this function does
     * nothing.
     *
     * @param model The dialog model to be dismissed or removed from pending list.
     * @param dismissalCause The {@link DialogDismissalCause} that describes why the dialog is
     *     dismissed.
     */
    public void dismissDialog(
            @Nullable PropertyModel model, @DialogDismissalCause int dismissalCause) {
        if (model == null) return;
        if (dismissalCause == DialogDismissalCause.NAVIGATE_BACK_OR_TOUCH_OUTSIDE) {
            assert mCurrentType == ModalDialogType.APP;
        } else if (dismissalCause == DialogDismissalCause.NAVIGATE_BACK
                || dismissalCause == DialogDismissalCause.TOUCH_OUTSIDE) {
            assert mCurrentType == ModalDialogType.TAB;
        }
        if (mCurrentPresenter == null || model != mCurrentPresenter.getDialogModel()) {
            if (mPendingDialogContainer.remove(model)) {
                model.get(ModalDialogProperties.CONTROLLER).onDismiss(model, dismissalCause);
                for (ModalDialogManagerObserver o : mObserverList) {
                    o.onDialogDismissed(model);
                }
                dispatchOnLastDialogDismissedIfEmpty();
                return;
            }
            // If the specified dialog is not found, return without any callbacks.
            return;
        }

        if (!isShowing()) return;
        assert model == mCurrentPresenter.getDialogModel();
        if (mDismissingCurrentDialog) return;
        mDismissingCurrentDialog = true;
        model.get(ModalDialogProperties.CONTROLLER).onDismiss(model, dismissalCause);
        mCurrentPresenter.setDialogModel(null, null, null, null);
        for (ModalDialogManagerObserver o : mObserverList) o.onDialogDismissed(model);
        mCurrentPresenter = null;
        mCurrentPriority = ModalDialogPriority.LOW;
        mDismissingCurrentDialog = false;
        dispatchOnLastDialogDismissedIfEmpty();
        showNextDialog();
    }

    /**
     * Dismiss the dialog currently shown and remove all pending dialogs.
     *
     * @param dismissalCause The {@link DialogDismissalCause} that describes why the dialogs are
     *     dismissed.
     */
    public void dismissAllDialogs(@DialogDismissalCause int dismissalCause) {
        for (@ModalDialogType int dialogType = ModalDialogType.RANGE_MIN;
                dialogType <= ModalDialogType.RANGE_MAX;
                ++dialogType) {
            dismissPendingDialogsOfType(dialogType, dismissalCause);
        }

        if (isShowing()) dismissDialog(mCurrentPresenter.getDialogModel(), dismissalCause);
        assert mPendingDialogContainer.isEmpty();
    }

    /**
     * Dismiss the dialog currently shown and remove all pending dialogs of the specified type.
     * @param dialogType The specified type of dialog.
     * @param dismissalCause The {@link DialogDismissalCause} that describes why the dialogs are
     *                       dismissed.
     */
    public void dismissDialogsOfType(
            @ModalDialogType int dialogType, @DialogDismissalCause int dismissalCause) {
        dismissPendingDialogsOfType(dialogType, dismissalCause);
        dismissActiveDialogOfType(dialogType, dismissalCause);
    }

    /**
     * Dismiss the dialog currently shown if it is of the specified type.
     *
     * <p>Any pending dialogs will then be shown.
     *
     * @param dialogType The specified type of dialog.
     * @param dismissalCause The {@link DialogDismissalCause} that describes why the dialogs are
     *     dismissed.
     * @return true if a dialog was showing and was dismissed.
     */
    public boolean dismissActiveDialogOfType(
            @ModalDialogType int dialogType, @DialogDismissalCause int dismissalCause) {
        if (isShowing() && dialogType == mCurrentType) {
            dismissDialog(mCurrentPresenter.getDialogModel(), dismissalCause);
            return true;
        }
        return false;
    }

    /** Helper method to dismiss pending dialogs of the specified type. */
    private void dismissPendingDialogsOfType(
            @ModalDialogType int dialogType, @DialogDismissalCause int dismissalCause) {
        mPendingDialogContainer.remove(
                dialogType,
                model -> {
                    ModalDialogProperties.Controller controller =
                            model.get(ModalDialogProperties.CONTROLLER);
                    controller.onDismiss(model, dismissalCause);
                    for (ModalDialogManagerObserver o : mObserverList) o.onDialogDismissed(model);
                    dispatchOnLastDialogDismissedIfEmpty();
                });
    }

    /**
     * Suspend all dialogs of the specified type, including the one currently shown. The currently
     * shown dialog would be suspended if its priority is not VERY_HIGH.
     *
     * <p>These dialogs will be prevented from showing unless {@link #resumeType(int, int)} is
     * called after the suspension. If the current dialog is suspended, it will be moved back to the
     * first dialog in the pending list. Any dialogs of the specified type in the pending list will
     * be skipped.
     *
     * @param dialogType The specified type of dialogs to be suspended.
     * @return A token to use when resuming the suspended type.
     */
    public int suspendType(@ModalDialogType int dialogType) {
        mSuspendedTypes.add(dialogType);
        if (isShowing()
                && dialogType == mCurrentType
                && mCurrentPriority != ModalDialogPriority.VERY_HIGH
                && !mDismissingCurrentDialog) {
            suspendCurrentDialog();
            showNextDialog();
        }
        return assumeNonNull(mTokenHolders.get(dialogType)).acquireToken();
    }

    /**
     * Resume the specified type of dialogs after suspension. This method does not resume showing
     * the dialog until after all held tokens are released.
     *
     * @param dialogType The specified type of dialogs to be resumed.
     * @param token The token generated from suspending the dialog type.
     */
    public void resumeType(@ModalDialogType int dialogType, int token) {
        assumeNonNull(mTokenHolders.get(dialogType)).releaseToken(token);
    }

    public long getOrCreateNativeBridge() {
        // Prevents the bridge gets recreated after `destroy()` is called.
        if (mDestroyed) return 0;

        if (mModalDialogManagerBridge == null) {
            mModalDialogManagerBridge = new ModalDialogManagerBridge(this);
        }
        return mModalDialogManagerBridge.getNativePtr();
    }

    /**
     * Actually resumes showing the type of dialog after all tokens are released.
     *
     * @param dialogType The specified type of dialogs to be resumed.
     */
    private void resumeTypeInternal(@ModalDialogType int dialogType) {
        if (assumeNonNull(mTokenHolders.get(dialogType)).hasTokens()) return;
        mSuspendedTypes.remove(dialogType);
        if (!isShowing()) showNextDialog();
    }

    /** Hide the current dialog and put it back to the front of the pending list. */
    private void suspendCurrentDialog() {
        assert isShowing();
        PropertyModel dialogView = mCurrentPresenter.getDialogModel();
        mCurrentPresenter.setDialogModel(null, null, null, null);
        mCurrentPresenter = null;
        mPendingDialogContainer.put(
                mCurrentType, mCurrentPriority, dialogView, /* showAsNext= */ true);
    }

    /** Helper method for showing the next available dialog in the pending dialog list. */
    private void showNextDialog() {
        assert !isShowing();
        PendingDialogContainer.PendingDialogType nextDialog =
                mPendingDialogContainer.getNextPendingDialog(mSuspendedTypes);
        if (nextDialog == null) return;
        showDialog(nextDialog.propertyModel, nextDialog.dialogType, nextDialog.dialogPriority);
    }

    // This calls onLastDialogDismissed() if there are no pending dialogs.
    private void dispatchOnLastDialogDismissedIfEmpty() {
        if (mPendingDialogContainer.isEmpty()) {
            for (ModalDialogManagerObserver o : mObserverList) {
                o.onLastDialogDismissed();
            }
        }
    }

    private @ModalDialogPriority int getDefaultPriorityByType(@ModalDialogType int dialogType) {
        if (dialogType == ModalDialogType.TAB) {
            return ModalDialogPriority.LOW;
        } else if (dialogType == ModalDialogType.APP) {
            return ModalDialogPriority.HIGH;
        } else {
            assert false : "Default priority not set for given dialog type.";
            return ModalDialogPriority.LOW;
        }
    }

    public @Nullable PropertyModel getCurrentDialogForTest() {
        return mCurrentPresenter == null ? null : mCurrentPresenter.getDialogModel();
    }

    public @Nullable List<PropertyModel> getPendingDialogsForTest(@ModalDialogType int dialogType) {
        @ModalDialogPriority int priority = getDefaultPriorityByType(dialogType);
        return mPendingDialogContainer.get(dialogType, priority);
    }

    public @Nullable Presenter getPresenterForTest(@ModalDialogType int dialogType) {
        return mPresenters.get(dialogType);
    }

    public @Nullable Presenter getCurrentPresenterForTest() {
        return mCurrentPresenter;
    }
}
