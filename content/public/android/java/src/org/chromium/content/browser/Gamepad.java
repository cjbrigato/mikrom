// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.content.Context;
import android.view.KeyEvent;
import android.view.MotionEvent;

import org.chromium.base.UserData;
import org.chromium.build.annotations.NullMarked;
import org.chromium.content.browser.webcontents.WebContentsImpl;
import org.chromium.content_public.browser.WebContents;
import org.chromium.content_public.browser.WebContents.UserDataFactory;
import org.chromium.device.gamepad.GamepadList;

/**
 * Encapsulates component class {@link GamepadList} for use in content, with regards
 * to its state according to content being attached to/detached from window.
 */
@NullMarked
class Gamepad implements WindowEventObserver, UserData {
    private final Context mContext;

    private static final class UserDataFactoryLazyHolder {
        private static final UserDataFactory<Gamepad> INSTANCE = Gamepad::new;
    }

    public static Gamepad from(WebContents webContents) {
        Gamepad ret =
                webContents.getOrSetUserData(Gamepad.class, UserDataFactoryLazyHolder.INSTANCE);
        assert ret != null;
        return ret;
    }

    public Gamepad(WebContents webContents) {
        mContext = assumeNonNull(((WebContentsImpl) webContents).getContext());
        WindowEventObserverManager.from(webContents).addObserver(this);
    }

    // WindowEventObserver

    @Override
    public void onAttachedToWindow() {
        GamepadList.onAttachedToWindow(mContext);
    }

    @Override
    public void onDetachedFromWindow() {
        GamepadList.onDetachedFromWindow();
    }

    public boolean onGenericMotionEvent(MotionEvent event) {
        return GamepadList.onGenericMotionEvent(event);
    }

    public boolean dispatchKeyEvent(KeyEvent event) {
        return GamepadList.dispatchKeyEvent(event);
    }
}
