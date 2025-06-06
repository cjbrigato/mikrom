// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.omnibox;

import androidx.annotation.DrawableRes;
import androidx.annotation.StringRes;

import org.chromium.build.annotations.NullMarked;
import org.chromium.components.security_state.ConnectionSecurityLevel;

/** Utility class to get security state info for the omnibox. */
@NullMarked
public class SecurityStatusIcon {
    /** @return the id of the resource identifying the icon corresponding to the securityLevel. */
    @DrawableRes
    public static int getSecurityIconResource(
            @ConnectionSecurityLevel int securityLevel,
            boolean isSmallDevice,
            boolean skipIconForNeutralState,
            boolean useLockIconForSecureState) {
        switch (securityLevel) {
            case ConnectionSecurityLevel.NONE:
                if (isSmallDevice && skipIconForNeutralState) return 0;
                return R.drawable.omnibox_info;
            case ConnectionSecurityLevel.WARNING:
                return R.drawable.omnibox_not_secure_warning;
            case ConnectionSecurityLevel.DANGEROUS:
                return R.drawable.omnibox_dangerous;
            case ConnectionSecurityLevel.SECURE:
                return useLockIconForSecureState
                        ? R.drawable.omnibox_https_valid_lock
                        : R.drawable.omnibox_https_valid_page_info;
            default:
                assert false;
        }
        return 0;
    }

    /** @return The resource ID of the content description for the security icon. */
    @StringRes
    public static int getSecurityIconContentDescriptionResourceId(
            @ConnectionSecurityLevel int securityLevel) {
        switch (securityLevel) {
            case ConnectionSecurityLevel.NONE:
            case ConnectionSecurityLevel.WARNING:
                return R.string.accessibility_security_btn_warn;
            case ConnectionSecurityLevel.DANGEROUS:
                return R.string.accessibility_security_btn_dangerous;
            case ConnectionSecurityLevel.SECURE:
                return R.string.accessibility_security_btn_secure;
            default:
                assert false;
        }
        return 0;
    }
}
