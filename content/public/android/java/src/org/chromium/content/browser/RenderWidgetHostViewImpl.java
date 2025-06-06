// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import android.app.Activity;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;

import org.chromium.base.Callback;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.content.R;
import org.chromium.content_public.browser.RenderWidgetHostView;
import org.chromium.ui.base.DeviceInput;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.ui.widget.Toast;

/**
 * The Android implementation of RenderWidgetHostView. This is a Java wrapper to allow communicating
 * with the native RenderWidgetHostViewAndroid object (note the different class names). This object
 * allows the browser to access and control the renderer's top level View.
 */
@JNINamespace("content")
@NullMarked
public class RenderWidgetHostViewImpl implements RenderWidgetHostView {
    private long mNativeRenderWidgetHostView;

    // Remember the stack for clearing native the native stack for debugging use after destroy.
    private @Nullable Throwable mNativeDestroyThrowable;

    private @Nullable Toast mPointerLockToast;

    @CalledByNative
    private static RenderWidgetHostViewImpl create(long renderWidgetHostViewLong) {
        return new RenderWidgetHostViewImpl(renderWidgetHostViewLong);
    }

    /** Do not call this constructor from Java, use native WebContents->GetRenderWidgetHostView. */
    private RenderWidgetHostViewImpl(long renderWidgetHostViewLong) {
        mNativeRenderWidgetHostView = renderWidgetHostViewLong;
    }

    @Override
    public boolean isReady() {
        checkNotDestroyed();
        return RenderWidgetHostViewImplJni.get()
                .isReady(getNativePtr(), RenderWidgetHostViewImpl.this);
    }

    @Override
    public int getBackgroundColor() {
        return RenderWidgetHostViewImplJni.get()
                .getBackgroundColor(getNativePtr(), RenderWidgetHostViewImpl.this);
    }

    /** Removes handles used in text selection. */
    public void dismissTextHandles() {
        if (isDestroyed()) return;
        RenderWidgetHostViewImplJni.get()
                .dismissTextHandles(getNativePtr(), RenderWidgetHostViewImpl.this);
    }

    /**
     * Shows the paste popup menu and the touch handles at the specified location.
     * @param x The horizontal location of the touch in dps.
     * @param y The vertical location of the touch in dps.
     */
    public void showContextMenuAtTouchHandle(int x, int y) {
        checkNotDestroyed();
        RenderWidgetHostViewImplJni.get()
                .showContextMenuAtTouchHandle(getNativePtr(), RenderWidgetHostViewImpl.this, x, y);
    }

    @Override
    public void onViewportInsetBottomChanged() {
        checkNotDestroyed();
        RenderWidgetHostViewImplJni.get()
                .onViewportInsetBottomChanged(getNativePtr(), RenderWidgetHostViewImpl.this);
    }

    @Override
    public void writeContentBitmapToDiskAsync(
            int width, int height, String path, Callback<String> callback) {
        if (isDestroyed()) callback.onResult("RWHVA already destroyed!");
        RenderWidgetHostViewImplJni.get()
                .writeContentBitmapToDiskAsync(
                        getNativePtr(),
                        RenderWidgetHostViewImpl.this,
                        width,
                        height,
                        path,
                        callback);
    }

    @Override
    public void onResume() {
        RenderWidgetHostViewImplJni.get().onResume(getNativePtr());
    }

    // TODO(https://crbug.com/419544853): Move the pointer lock logic to a separate class once
    // WindowAndroid implements SupportsUserData
    @CalledByNative
    private void showPointerLockToast(WindowAndroid windowAndroid) {
        int messageId = R.string.pointer_lock_api_notification;
        if (!DeviceInput.supportsAlphabeticKeyboard()) {
            messageId = R.string.pointer_lock_api_notification_no_keyboard;
        }

        Activity activity = windowAndroid.getActivity().get();
        if (activity != null) {
            mPointerLockToast = Toast.makeText(activity, messageId, Toast.LENGTH_SHORT);
            mPointerLockToast.show();
        }
    }

    @CalledByNative
    private void hidePointerLockToast() {
        if (mPointerLockToast != null) {
            mPointerLockToast.cancel();
            mPointerLockToast = null;
        }
    }

    // ====================
    // Support for native.
    // ====================

    public boolean isDestroyed() {
        return getNativePtr() == 0;
    }

    private long getNativePtr() {
        return mNativeRenderWidgetHostView;
    }

    @CalledByNative
    private void clearNativePtr() {
        mNativeRenderWidgetHostView = 0;
        mNativeDestroyThrowable = new RuntimeException("clearNativePtr");
    }

    private void checkNotDestroyed() {
        if (getNativePtr() != 0) return;
        throw new IllegalStateException(
                "Native RenderWidgetHostViewAndroid already destroyed", mNativeDestroyThrowable);
    }

    @NativeMethods
    interface Natives {
        boolean isReady(long nativeRenderWidgetHostViewAndroid, RenderWidgetHostViewImpl caller);

        int getBackgroundColor(
                long nativeRenderWidgetHostViewAndroid, RenderWidgetHostViewImpl caller);

        void dismissTextHandles(
                long nativeRenderWidgetHostViewAndroid, RenderWidgetHostViewImpl caller);

        void showContextMenuAtTouchHandle(
                long nativeRenderWidgetHostViewAndroid,
                RenderWidgetHostViewImpl caller,
                int x,
                int y);

        void onViewportInsetBottomChanged(
                long nativeRenderWidgetHostViewAndroid, RenderWidgetHostViewImpl caller);

        void writeContentBitmapToDiskAsync(
                long nativeRenderWidgetHostViewAndroid,
                RenderWidgetHostViewImpl caller,
                int width,
                int height,
                String path,
                Callback<String> callback);

        void onResume(long nativeRenderWidgetHostViewAndroid);
    }
}
