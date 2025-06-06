// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.password_manager.account_storage_notice;

import android.content.Context;
import android.text.SpannableString;
import android.text.method.LinkMovementMethod;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.TextView;

import androidx.annotation.StringRes;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetContent;
import org.chromium.ui.text.ChromeClickableSpan;
import org.chromium.ui.text.SpanApplier;
import org.chromium.ui.widget.ButtonCompat;

@NullMarked
class AccountStorageNoticeView implements BottomSheetContent {
    private final View mContentView;

    /** Context must be consumed on the constructor and not cached. */
    public AccountStorageNoticeView(
            Context context, Runnable buttonCallback, Runnable settingsLinkCallback) {
        assert context != null;
        assert buttonCallback != null;
        assert settingsLinkCallback != null;
        mContentView =
                LayoutInflater.from(context)
                        .inflate(R.layout.account_storage_notice_layout, /* root= */ null);
        ((ButtonCompat) mContentView.findViewById(R.id.account_storage_notice_button))
                .setOnClickListener(unused -> buttonCallback.run());
        TextView linkView = mContentView.findViewById(R.id.account_storage_settings_link);
        SpannableString linkText =
                SpanApplier.applySpans(
                        context.getString(R.string.passwords_account_storage_notice_subtitle),
                        new SpanApplier.SpanInfo(
                                "<link>",
                                "</link>",
                                new ChromeClickableSpan(
                                        context, unused -> settingsLinkCallback.run())));
        linkView.setText(linkText);
        linkView.setMovementMethod(LinkMovementMethod.getInstance());
    }

    @Override
    public View getContentView() {
        return mContentView;
    }

    @Override
    public @Nullable View getToolbarView() {
        return null;
    }

    @Override
    public int getVerticalScrollOffset() {
        return 0;
    }

    @Override
    public void destroy() {}

    @Override
    public @ContentPriority int getPriority() {
        return ContentPriority.HIGH;
    }

    @Override
    public boolean swipeToDismissEnabled() {
        return true;
    }

    @Override
    public float getFullHeightRatio() {
        return HeightMode.WRAP_CONTENT;
    }

    @Override
    public String getSheetContentDescription(Context context) {
        return context.getString(R.string.passwords_account_storage_notice_title);
    }

    @Override
    public @StringRes int getSheetHalfHeightAccessibilityStringId() {
        return R.string.passwords_account_storage_notice_half_height_accessibility_label;
    }

    @Override
    public @StringRes int getSheetFullHeightAccessibilityStringId() {
        return R.string.passwords_account_storage_notice_full_height_accessibility_label;
    }

    @Override
    public @StringRes int getSheetClosedAccessibilityStringId() {
        return R.string.passwords_account_storage_notice_closed_accessibility_label;
    }
}
