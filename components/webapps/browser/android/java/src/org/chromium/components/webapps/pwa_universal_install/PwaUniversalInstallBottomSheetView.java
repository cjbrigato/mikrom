// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.webapps.pwa_universal_install;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.drawable.Icon;
import android.os.Build;
import android.util.TypedValue;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.LinearLayout.LayoutParams;

import org.chromium.build.annotations.Initializer;
import org.chromium.build.annotations.NullMarked;
import org.chromium.components.webapps.R;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.base.LocalizationUtils;

/** The view portion of the PWA Universal Install bottom sheet. */
@NullMarked
public class PwaUniversalInstallBottomSheetView {

    // The details of the bottom sheet.
    private View mContentView;

    public PwaUniversalInstallBottomSheetView() {}

    @Initializer
    public void initialize(
            Context context,
            WebContents webContents,
            int arrowId,
            int installOverlayId,
            int shortcutOverlayId) {
        View contentView =
                LayoutInflater.from(context)
                        .inflate(
                                R.layout.pwa_universal_install_bottom_sheet_content,
                                /* root= */ null);
        mContentView = contentView;

        // Draw background shape with rounded-corners for both options.
        contentView
                .findViewById(R.id.option_install)
                .setBackgroundResource(R.drawable.pwa_restore_app_item_background_top);
        contentView
                .findViewById(R.id.option_shortcut)
                .setBackgroundResource(R.drawable.pwa_restore_app_item_background_bottom);

        // Add a 2dp separator view as a separate item in the ScrollView so as to not affect the
        // height of the app item view (or mess up the rounded corners).
        View separator = contentView.findViewById(R.id.separator);
        separator.setLayoutParams(
                new LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        (int)
                                TypedValue.applyDimension(
                                        TypedValue.COMPLEX_UNIT_DIP,
                                        2,
                                        context.getResources().getDisplayMetrics())));

        if (arrowId != 0) {
            ImageView installArrow = contentView.findViewById(R.id.arrow_install);
            ImageView shortcutArrow = contentView.findViewById(R.id.arrow_shortcut);
            installArrow.setImageResource(arrowId);
            shortcutArrow.setImageResource(arrowId);
            if (LocalizationUtils.isLayoutRtl()) {
                // Flip the image horizontally, so that the arrow points the right way for RTL.
                installArrow.setScaleX(-1);
                shortcutArrow.setScaleX(-1);
            }
        }
        if (installOverlayId != 0 && shortcutOverlayId != 0) {
            ((ImageView) contentView.findViewById(R.id.install_icon_overlay))
                    .setImageResource(installOverlayId);
            ((ImageView) contentView.findViewById(R.id.shortcut_icon_overlay))
                    .setImageResource(shortcutOverlayId);
        }
    }

    public void setIcon(Bitmap appIcon, boolean isAdaptive) {
        assert (!isAdaptive || Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                : "Adaptive icons should not be provided pre-Android O.";

        View contentView = mContentView;
        ImageView appIconInstall = contentView.findViewById(R.id.app_icon_install);
        ImageView appIconShortcut = contentView.findViewById(R.id.app_icon_shortcut);

        if (isAdaptive) {
            Icon icon = Icon.createWithAdaptiveBitmap(appIcon);
            appIconInstall.setImageIcon(icon);
            appIconShortcut.setImageIcon(icon);
        } else {
            appIconInstall.setImageBitmap(appIcon);
            appIconShortcut.setImageBitmap(appIcon);
        }

        // Swap out the spinners for icons.
        contentView.findViewById(R.id.spinny_install).setVisibility(View.GONE);
        contentView.findViewById(R.id.spinny_shortcut).setVisibility(View.GONE);
        appIconInstall.setVisibility(View.VISIBLE);
        appIconShortcut.setVisibility(View.VISIBLE);
        contentView.findViewById(R.id.install_icon_overlay).setVisibility(View.VISIBLE);
        contentView.findViewById(R.id.shortcut_icon_overlay).setVisibility(View.VISIBLE);
    }

    public View getContentView() {
        return mContentView;
    }

    int getPeekHeight() {
        return mContentView.getHeight();
    }
}
