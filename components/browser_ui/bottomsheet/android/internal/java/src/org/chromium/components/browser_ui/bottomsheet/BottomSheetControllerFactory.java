// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.browser_ui.bottomsheet;

import android.view.View;
import android.view.ViewGroup;
import android.view.Window;

import org.chromium.base.Callback;
import org.chromium.base.supplier.Supplier;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.components.browser_ui.desktop_windowing.DesktopWindowStateManager;
import org.chromium.components.browser_ui.widget.scrim.ScrimManager;
import org.chromium.ui.KeyboardVisibilityDelegate;
import org.chromium.ui.base.WindowAndroid;

/** A factory for producing a {@link BottomSheetController}. */
@NullMarked
public class BottomSheetControllerFactory {
    /**
     * @param scrimManagerSupplier Suppliers the {@ScrimManager}, used show scrims behind the sheet.
     * @param initializedCallback A callback for the sheet having been created.
     * @param window The activity's window.
     * @param keyboardDelegate A means of hiding the keyboard.
     * @param root The view that should contain the sheet.
     * @param edgeToEdgeBottomInsetSupplier Supplier of bottom inset when e2e is on.
     * @param desktopWindowStateManager The {@link DesktopWindowStateManager} for the app header.
     * @return A new instance of the {@link BottomSheetController}.
     */
    public static ManagedBottomSheetController createBottomSheetController(
            final Supplier<ScrimManager> scrimManagerSupplier,
            Callback<View> initializedCallback,
            Window window,
            KeyboardVisibilityDelegate keyboardDelegate,
            Supplier<ViewGroup> root,
            Supplier<Integer> edgeToEdgeBottomInsetSupplier,
            @Nullable DesktopWindowStateManager desktopWindowStateManager) {
        return new BottomSheetControllerImpl(
                scrimManagerSupplier,
                initializedCallback,
                window,
                keyboardDelegate,
                root,
                /* alwaysFullWidth= */ false,
                edgeToEdgeBottomInsetSupplier,
                desktopWindowStateManager);
    }

    /**
     * Create {@link BottomSheetController} of full-width bottom sheets.
     *
     * @param scrimManagerSupplier A supplier of scrimManagerSupplier to be shown behind the sheet.
     * @param initializedCallback A callback for the sheet having been created.
     * @param window The activity's window.
     * @param keyboardDelegate A means of hiding the keyboard.
     * @param root The view that should contain the sheet.
     * @return A new instance of the {@link BottomSheetController}.
     */
    public static ManagedBottomSheetController createFullWidthBottomSheetController(
            final Supplier<ScrimManager> scrimManagerSupplier,
            Callback<View> initializedCallback,
            Window window,
            KeyboardVisibilityDelegate keyboardDelegate,
            Supplier<ViewGroup> root) {
        return new BottomSheetControllerImpl(
                scrimManagerSupplier,
                initializedCallback,
                window,
                keyboardDelegate,
                root,
                /* alwaysFullWidth= */ true,
                () -> 0,
                /* desktopWindowStateManager= */ null);
    }

    // Redirect methods to provider to make them only accessible to classes that have access to the
    // factory.

    /**
     * Attach a shared {@link BottomSheetController} to a {@link WindowAndroid}.
     *
     * @param windowAndroid The window to attach the sheet's controller to.
     * @param controller The controller to attach.
     */
    public static void attach(
            WindowAndroid windowAndroid, ManagedBottomSheetController controller) {
        BottomSheetControllerProvider.attach(windowAndroid, controller);
    }

    /**
     * Detach the specified {@link BottomSheetController} from any {@link WindowAndroid}s it is
     * associated with.
     * @param controller The controller to remove from any associated windows.
     */
    public static void detach(ManagedBottomSheetController controller) {
        BottomSheetControllerProvider.detach(controller);
    }

    /** @param reporter A means of reporting an exception without crashing. */
    public static void setExceptionReporter(Callback<Throwable> reporter) {
        BottomSheet.setExceptionReporter(reporter);
    }
}
