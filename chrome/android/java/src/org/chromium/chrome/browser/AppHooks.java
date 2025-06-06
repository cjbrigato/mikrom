// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import android.content.Context;
import android.view.View;

import org.chromium.base.Callback;
import org.chromium.base.ContextUtils;
import org.chromium.base.ServiceLoaderUtil;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.partnerbookmarks.PartnerBookmark;
import org.chromium.chrome.browser.partnerbookmarks.PartnerBookmarksProviderIterator;
import org.chromium.chrome.browser.webapps.GooglePlayWebApkInstallDelegate;
import org.chromium.components.commerce.core.ShoppingService.PriceInsightsInfo;
import org.chromium.components.policy.AppRestrictionsProvider;
import org.chromium.components.policy.CombinedPolicyProvider;

/**
 * Base class for defining methods where different behavior is required by downstream targets.
 *
 * <p>Uses ServiceLoaderUtil to override when downstream is available.
 *
 * <p>Prefer to create a dedicate interface and directly use ServiceLoaderUtil for future hooks.
 */
@NullMarked
public class AppHooks {
    public static AppHooks get() {
        // R8 can better optimize if we return a new instance each time.
        AppHooks ret = ServiceLoaderUtil.maybeCreate(AppHooks.class);
        if (ret == null) {
            ret = new AppHooks();
        }
        return ret;
    }

    /** Returns the singleton instance of GooglePlayWebApkInstallDelegate. */
    public @Nullable GooglePlayWebApkInstallDelegate getGooglePlayWebApkInstallDelegate() {
        return null;
    }

    public void registerPolicyProviders(CombinedPolicyProvider combinedProvider) {
        combinedProvider.registerProvider(
                new AppRestrictionsProvider(ContextUtils.getApplicationContext()));
    }

    /** Async fetch the iterator of partner bookmarks (or null if not available). */
    public void requestPartnerBookmarkIterator(
            Callback<PartnerBookmark.@Nullable BookmarkIterator> callback) {
        PartnerBookmarksProviderIterator.createIfAvailable(callback);
    }

    /** Returns the URL to the WebAPK creation/update server. */
    public String getWebApkServerUrl() {
        return "";
    }

    public void registerProtoExtensions() {}

    /** Returns the view of the line chart given the price insights info. */
    public @Nullable View getLineChartForPriceInsightsInfo(
            Context context, PriceInsightsInfo info) {
        return null;
    }

    // Stop! Prefer to create a dedicated interface and directly use ServiceLoaderUtil for future
    // hooks.
}
