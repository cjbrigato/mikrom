// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.touch_to_fill.password_generation;

import android.content.Context;
import android.content.res.Resources;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.view.View;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.annotation.StringRes;
import androidx.appcompat.content.res.AppCompatResources;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.password_manager.PasswordManagerResourceProviderFactory;
import org.chromium.chrome.browser.touch_to_fill.common.TouchToFillUtil;
import org.chromium.chrome.browser.touch_to_fill.password_generation.TouchToFillPasswordGenerationCoordinator.GenerationCallback;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetContent;

/**
 * This class is responsible for rendering the password generation bottom sheet. It is a View in
 * this Model-View-Controller component and doesn't inherit but holds Android Views.
 */
@NullMarked
class TouchToFillPasswordGenerationView implements BottomSheetContent {
    private final View mContent;
    private final Context mContext;
    private final TextView mPasswordView;

    // Minimum password length that allows to label the password as strong in
    // the UI. Must stay in sync with kLengthSufficientForStrongLabel in
    // components/autofill/core/common/password_generation_util.h.
    private static final int LENGTH_SUFFICIENT_FOR_STRONG_LABEL = 12;

    TouchToFillPasswordGenerationView(Context context, View content) {
        mContext = context;
        mContent = content;

        mContent.setOnGenericMotionListener((v, e) -> true); // Filter background interaction.

        ImageView sheetHeaderImage = mContent.findViewById(R.id.touch_to_fill_sheet_header_image);
        sheetHeaderImage.setImageDrawable(
                AppCompatResources.getDrawable(
                        context,
                        PasswordManagerResourceProviderFactory.create().getPasswordManagerIcon()));
        mPasswordView = mContent.findViewById(R.id.password);
        TouchToFillUtil.addColorAndRippleToBackground(
                mPasswordView, (GradientDrawable) mPasswordView.getBackground(), mContext);
    }

    void setSheetTitle(String generatedPassword) {
        TextView sheetSubtitleView = mContent.findViewById(R.id.touch_to_fill_sheet_title);
        String sheetTitle =
                generatedPassword.length() >= LENGTH_SUFFICIENT_FOR_STRONG_LABEL
                        ? mContext.getString(R.string.password_generation_bottom_sheet_title)
                        : mContext.getString(
                                R.string.password_generation_bottom_sheet_title_without_strong);
        sheetSubtitleView.setText(sheetTitle);
    }

    void setSheetSubtitle(String accountEmail) {
        TextView sheetSubtitleView = mContent.findViewById(R.id.touch_to_fill_sheet_subtitle);
        String sheetSubtitle =
                accountEmail.isEmpty()
                        ? mContext.getString(
                                R.string.password_generation_bottom_sheet_subtitle_no_account)
                        : mContext.getString(
                                R.string.password_generation_bottom_sheet_subtitle, accountEmail);
        sheetSubtitleView.setText(sheetSubtitle);
    }

    void setGeneratedPassword(String generatedPassword) {
        mPasswordView.setTypeface(Typeface.MONOSPACE);
        mPasswordView.setText(generatedPassword);
        mPasswordView.setContentDescription(
                mContext.getString(
                        R.string.password_generation_bottom_sheet_use_password_button_content,
                        generatedPassword));
    }

    void setPasswordAcceptedCallback(GenerationCallback callback) {
        Button passwordAcceptedButton = mContent.findViewById(R.id.use_password_button);
        mPasswordView.setOnClickListener(
                (v) ->
                        callback.onAccept(
                                mPasswordView.getText().toString(),
                                TouchToFillPasswordGenerationCoordinator.InteractionResult
                                        .ACCEPTED_VIA_PASSWORD_VIEW));
        passwordAcceptedButton.setOnClickListener(
                (v) ->
                        callback.onAccept(
                                mPasswordView.getText().toString(),
                                TouchToFillPasswordGenerationCoordinator.InteractionResult
                                        .ACCEPTED_VIA_ACCEPT_BUTTON));
    }

    void setPasswordRejectedCallback(Runnable callback) {
        Button passwordRejectedButton = mContent.findViewById(R.id.reject_password_button);
        passwordRejectedButton.setOnClickListener((v) -> callback.run());
    }

    @Override
    public View getContentView() {
        return mContent;
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
    public int getPriority() {
        return BottomSheetContent.ContentPriority.HIGH;
    }

    @Override
    public boolean swipeToDismissEnabled() {
        return false;
    }

    @Override
    public String getSheetContentDescription(Context context) {
        return context.getString(R.string.password_generation_bottom_sheet_content_description);
    }

    @Override
    public @StringRes int getSheetHalfHeightAccessibilityStringId() {
        // Half-height is disabled so no need for an accessibility string.
        assert false : "This method should not be called";
        return Resources.ID_NULL;
    }

    @Override
    public @StringRes int getSheetFullHeightAccessibilityStringId() {
        return R.string.password_generation_bottom_sheet_content_description;
    }

    @Override
    public @StringRes int getSheetClosedAccessibilityStringId() {
        return R.string.password_generation_bottom_sheet_closed;
    }

    @Override
    public float getHalfHeightRatio() {
        return HeightMode.DISABLED;
    }

    @Override
    public float getFullHeightRatio() {
        return HeightMode.WRAP_CONTENT;
    }
}
