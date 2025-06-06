// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.ui.base;

import static android.view.InputDevice.KEYBOARD_TYPE_ALPHABETIC;
import static android.view.InputDevice.SOURCE_MOUSE;
import static android.view.InputDevice.SOURCE_TOUCHPAD;

import android.content.Context;
import android.hardware.input.InputManager;
import android.hardware.input.InputManager.InputDeviceListener;
import android.util.SparseArray;
import android.view.InputDevice;
import android.view.MotionEvent;

import androidx.annotation.VisibleForTesting;

import org.chromium.base.ContextUtils;
import org.chromium.base.ResettersForTesting;
import org.chromium.base.ThreadUtils;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

/**
 * Utilities for accessing device input information. Note that this class is not thread-safe and
 * currently asserts all interactions occur on the UI thread. If usage is required off the UI thread
 * in the future, this class can be modified for multi-thread support.
 */
@NullMarked
public class DeviceInput implements InputDeviceListener {

    /** Wrapper class which provides lazy initialization of a singleton instance. */
    private static final class LazyInit {
        public static final DeviceInput sInstance = new DeviceInput();
    }

    /** See {@link #setSupportsAlphabeticKeyboardForTesting(boolean)}. */
    private static @Nullable Boolean sSupportsAlphabeticKeyboardForTesting;

    /** See {@link #setSupportsPrevisionPointerForTesting(boolean)}. */
    private static @Nullable Boolean sSupportsPrecisionPointerForTesting;

    /** Cached snapshots of all currently connected {@link InputDevice}s. */
    private final SparseArray<DeviceSnapshot> mDeviceSnapshotsById = new SparseArray<>();

    /** Only a lazy singleton instance may be instantiated. */
    private DeviceInput() {
        ThreadUtils.assertOnUiThread();

        // Initialize cache.
        final int[] deviceIds = InputDevice.getDeviceIds();
        for (int i = 0; i < deviceIds.length; i++) {
            int deviceId = deviceIds[i];
            InputDevice device = InputDevice.getDevice(deviceId);
            if (device != null) {
                mDeviceSnapshotsById.put(deviceId, DeviceSnapshot.from(device));
            }
        }

        // Register listener to perform cache updates.
        var context = ContextUtils.getApplicationContext();
        var inputManager = (InputManager) context.getSystemService(Context.INPUT_SERVICE);
        inputManager.registerInputDeviceListener(this, /* handler= */ null);
    }

    /** Returns a lazily instantiated singleton instance. */
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    public static DeviceInput getInstance() {
        ThreadUtils.assertOnUiThread();
        return LazyInit.sInstance;
    }

    /** Modifies the output of {@link #supportsAlphabeticKeyboard()} for testing. */
    public static void setSupportsAlphabeticKeyboardForTesting(Boolean supportsAlphabeticKeyboard) {
        sSupportsAlphabeticKeyboardForTesting = supportsAlphabeticKeyboard;
        ResettersForTesting.register(() -> sSupportsAlphabeticKeyboardForTesting = null);
    }

    /**
     * @return Whether any currently connected {@link InputDevice} supports an alphabetic keyboard.
     */
    public static boolean supportsAlphabeticKeyboard() {
        ThreadUtils.assertOnUiThread();
        return getInstance().supportsAlphabeticKeyboardImpl();
    }

    /** Implementation of {@link #supportsAlphabeticKeyboard()}. */
    public boolean supportsAlphabeticKeyboardImpl() {
        ThreadUtils.assertOnUiThread();
        if (sSupportsAlphabeticKeyboardForTesting != null) {
            return sSupportsAlphabeticKeyboardForTesting;
        }
        for (int i = 0; i < mDeviceSnapshotsById.size(); i++) {
            if (mDeviceSnapshotsById.valueAt(i).supportsAlphabeticKeyboard) {
                return true;
            }
        }
        return false;
    }

    /** Modifies the output of {@link #supportsPrecisionPointer()} for testing. */
    public static void setSupportsPrecisionPointerForTesting(Boolean supportsPrecisionPointer) {
        sSupportsPrecisionPointerForTesting = supportsPrecisionPointer;
        ResettersForTesting.register(() -> sSupportsPrecisionPointerForTesting = null);
    }

    /**
     * @return Whether any currently connected {@link InputDevice} supports precision pointing. Note
     *     that this includes not only mice, but also any mice-like pointing devices (e.g. stylus,
     *     touchpad, etc).
     */
    public static boolean supportsPrecisionPointer() {
        ThreadUtils.assertOnUiThread();
        return getInstance().supportsPrecisionPointerImpl();
    }

    /** Implementation of {@link #supportsPrecisionPointer()}. */
    private boolean supportsPrecisionPointerImpl() {
        ThreadUtils.assertOnUiThread();
        if (sSupportsPrecisionPointerForTesting != null) {
            return sSupportsPrecisionPointerForTesting;
        }
        for (int i = 0; i < mDeviceSnapshotsById.size(); i++) {
            if (mDeviceSnapshotsById.valueAt(i).supportsPrecisionPointer) {
                return true;
            }
        }
        return false;
    }

    /**
     * @return the Touchpad MotionRange of AXIS_X for the provided {@param deviceId}, or null if the
     *     device is not found or the device doesn't support touchpad source
     */
    public static InputDevice.@Nullable MotionRange getTouchpadXAxisMotionRange(int deviceId) {
        ThreadUtils.assertOnUiThread();
        DeviceSnapshot snapshot = getInstance().mDeviceSnapshotsById.get(deviceId);
        if (snapshot != null) {
            return snapshot.touchpadXAxisMotionRange;
        }

        return null;
    }

    /**
     * @return the Touchpad MotionRange of AXIS_Y for the provided {@param deviceId}, or null if the
     *     device is not found or the device doesn't support touchpad source
     */
    public static InputDevice.@Nullable MotionRange getTouchpadYAxisMotionRange(int deviceId) {
        ThreadUtils.assertOnUiThread();
        DeviceSnapshot snapshot = getInstance().mDeviceSnapshotsById.get(deviceId);
        if (snapshot != null) {
            return snapshot.touchpadYAxisMotionRange;
        }

        return null;
    }

    @Override
    public void onInputDeviceAdded(int deviceId) {
        ThreadUtils.assertOnUiThread();
        InputDevice device = InputDevice.getDevice(deviceId);
        if (device != null) {
            mDeviceSnapshotsById.put(deviceId, DeviceSnapshot.from(device));
        }
    }

    @Override
    public void onInputDeviceChanged(int deviceId) {
        ThreadUtils.assertOnUiThread();
        InputDevice device = InputDevice.getDevice(deviceId);
        if (device != null) {
            mDeviceSnapshotsById.put(deviceId, DeviceSnapshot.from(device));
        } else {
            mDeviceSnapshotsById.remove(deviceId);
        }
    }

    @Override
    public void onInputDeviceRemoved(int deviceId) {
        ThreadUtils.assertOnUiThread();
        mDeviceSnapshotsById.remove(deviceId);
    }

    /** Class which represents a snapshot of given {@link InputDevice}. */
    private static final class DeviceSnapshot {

        /** Whether the associated {@link InputDevice} supports an alphabetic keyboard. */
        public final boolean supportsAlphabeticKeyboard;

        /**
         * Whether the associated {@link InputDevice} supports precision pointing. Note that this
         * includes not only mice, but also any mice-like pointing devices (e.g. stylus, touchpad,
         * etc).
         */
        public final boolean supportsPrecisionPointer;

        /** The MotionRange of AXIS_X for the Touchpad source */
        public final InputDevice.MotionRange touchpadXAxisMotionRange;

        /** The MotionRange of AXIS_Y for the Touchpad source */
        public final InputDevice.MotionRange touchpadYAxisMotionRange;

        /** See {@link #from(InputDevice)}. */
        private DeviceSnapshot(
                boolean supportsAlphabeticKeyboard,
                boolean supportsPrecisionPointer,
                InputDevice.MotionRange touchpadXAxisMotionRange,
                InputDevice.MotionRange touchpadYAxisMotionRange) {
            this.supportsAlphabeticKeyboard = supportsAlphabeticKeyboard;
            this.supportsPrecisionPointer = supportsPrecisionPointer;
            this.touchpadXAxisMotionRange = touchpadXAxisMotionRange;
            this.touchpadYAxisMotionRange = touchpadYAxisMotionRange;
        }

        /**
         * @return a new snapshot representing the specified {@link InputDevice}.
         */
        public static DeviceSnapshot from(InputDevice device) {
            boolean isPhysical = !device.isVirtual();
            return new DeviceSnapshot(
                    /* supportsAlphabeticKeyboard= */ isPhysical
                            && device.getKeyboardType() == KEYBOARD_TYPE_ALPHABETIC,
                    // SOURCE_MOUSE applies to pointer devices, including mouse and touchpad
                    /* supportsPrecisionPointer= */ isPhysical
                            && device.supportsSource(SOURCE_MOUSE),
                    device.getMotionRange(MotionEvent.AXIS_X, SOURCE_TOUCHPAD),
                    device.getMotionRange(MotionEvent.AXIS_Y, SOURCE_TOUCHPAD));
        }
    }
}
