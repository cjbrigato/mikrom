// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.support_lib_boundary;

import android.content.Context;
import android.net.Uri;
import android.webkit.ValueCallback;

import org.jspecify.annotations.NullMarked;
import org.jspecify.annotations.Nullable;

import java.util.List;
import java.util.Set;

/** Boundary interface for WebViewFactoryProvider.Statics. */
@NullMarked
public interface StaticsBoundaryInterface {
    void initSafeBrowsing(Context context, ValueCallback<Boolean> callback);

    void setSafeBrowsingAllowlist(Set<String> hosts, @Nullable ValueCallback<Boolean> callback);

    void setSafeBrowsingWhitelist(List<String> hosts, @Nullable ValueCallback<Boolean> callback);

    Uri getSafeBrowsingPrivacyPolicyUrl();

    boolean isMultiProcessEnabled();

    String getVariationsHeader();

    void setDefaultTrafficStatsTag(int tag);

    void setDefaultTrafficStatsUid(int uid);
}
