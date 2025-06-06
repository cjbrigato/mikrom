// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.browserservices.intents;

import org.chromium.blink.mojom.DisplayMode;
import org.chromium.build.annotations.NullMarked;
import org.chromium.components.webapps.ShortcutSource;
import org.chromium.device.mojom.ScreenOrientationLockType;

/** Stores webapp specific information on behalf of {@link BrowserServicesIntentDataProvider}. */
@NullMarked
public class WebappExtras {
    public final String id;

    /** The URL to navigate to. */
    public final String url;

    /** The navigation scope of the webapp's application context. */
    public final String scopeUrl;

    /** The webapp's launcher icon. */
    public final WebappIcon icon;

    /** The webapp's name as it is usually displayed to the user. */
    public final String name;

    /** Short version of the webapp's name. */
    public final String shortName;

    public final @DisplayMode.EnumType int displayMode;

    /** The screen orientation to lock the webapp to. */
    public final @ScreenOrientationLockType.EnumType int orientation;

    /**
     * If the webapp was launched from the home screen or the app list: source where the webapp was
     * added from.
     * Otherwise: reason that the webapp was launched (e.g. deep link).
     */
    public final @ShortcutSource int source;

    /** Background color for webapp's splash screen. */
    public final Integer backgroundColor;

    /** Dark background color for webapp's splash screen. */
    public final Integer darkBackgroundColor;

    /** Background color to use if the Web Manifest does not provide a background color. */
    public final int defaultBackgroundColor;

    /** Whether {@link icon} was generated by Chromium. */
    public final boolean isIconGenerated;

    /** Whether {@link #icon} should be used as an Android Adaptive icon. */
    public final boolean isIconAdaptive;

    /** Whether the webapp should be navigated to {@link #url} if the webapp is already open. */
    public final boolean shouldForceNavigation;

    public WebappExtras(
            String id,
            String url,
            String scopeUrl,
            WebappIcon icon,
            String name,
            String shortName,
            @DisplayMode.EnumType int displayMode,
            int orientation,
            int source,
            Integer backgroundColor,
            Integer darkBackgroundColor,
            int defaultBackgroundColor,
            boolean isIconGenerated,
            boolean isIconAdaptive,
            boolean shouldForceNavigation) {
        this.id = id;
        this.url = url;
        this.scopeUrl = scopeUrl;
        this.icon = icon;
        this.name = name;
        this.shortName = shortName;
        this.displayMode = displayMode;
        this.orientation = orientation;
        this.source = source;
        this.backgroundColor = backgroundColor;
        this.darkBackgroundColor = darkBackgroundColor;
        this.defaultBackgroundColor = defaultBackgroundColor;
        this.isIconGenerated = isIconGenerated;
        this.isIconAdaptive = isIconAdaptive;
        this.shouldForceNavigation = shouldForceNavigation;
    }
}
