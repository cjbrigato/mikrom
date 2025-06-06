// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.contextmenu;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.net.Uri;

import org.chromium.base.Callback;
import org.chromium.base.ResettersForTesting;
import org.chromium.build.annotations.EnsuresNonNullIf;
import org.chromium.build.annotations.MonotonicNonNull;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.lens.LensController;
import org.chromium.chrome.browser.lens.LensEntryPoint;
import org.chromium.chrome.browser.lens.LensQueryParams;
import org.chromium.components.embedder_support.contextmenu.ChipDelegate;
import org.chromium.components.embedder_support.contextmenu.ChipRenderParams;
import org.chromium.components.embedder_support.contextmenu.ContextMenuImageFormat;
import org.chromium.components.embedder_support.contextmenu.ContextMenuNativeDelegate;
import org.chromium.content_public.browser.WebContents;

/** The class to handle Lens chip data and actions. */
@NullMarked
public class LensChipDelegate implements ChipDelegate {
    private static final LensController sLensController = LensController.getInstance();
    private static boolean sShouldSkipIsEnabledCheckForTesting;
    private final boolean mIsChipSupported;
    private @MonotonicNonNull LensQueryParams mLensQueryParams;
    private @MonotonicNonNull ContextMenuNativeDelegate mNativeDelegate;
    private @MonotonicNonNull Callback<Integer> mOnChipClickedCallback;
    private @MonotonicNonNull Callback<Integer> mOnChipShownCallback;

    public static boolean isEnabled(boolean isIncognito, boolean isTablet) {
        if (sShouldSkipIsEnabledCheckForTesting) return true;
        return sLensController.isLensEnabled(
                new LensQueryParams.Builder(LensEntryPoint.CONTEXT_MENU_CHIP, isIncognito, isTablet)
                        .build());
    }

    /** Whether it should skip the Lens chip eligiblity check for testing. */
    protected static void setShouldSkipIsEnabledCheckForTesting(boolean shouldSkipIsEnabledCheck) {
        sShouldSkipIsEnabledCheckForTesting = shouldSkipIsEnabledCheck;
        ResettersForTesting.register(() -> sShouldSkipIsEnabledCheckForTesting = false);
    }

    public LensChipDelegate(
            String pageUrl,
            String titleOrAltText,
            String srcUrl,
            String pageTitle,
            boolean isIncognito,
            boolean isTablet,
            WebContents webContents,
            ContextMenuNativeDelegate nativeDelegate,
            Callback<Integer> onChipClickedCallback,
            Callback<Integer> onChipShownCallback) {
        mIsChipSupported = sLensController.isQueryEnabled();
        if (!mIsChipSupported) {
            return;
        }
        mLensQueryParams =
                new LensQueryParams.Builder(LensEntryPoint.CONTEXT_MENU_CHIP, isIncognito, isTablet)
                        .withPageUrl(pageUrl)
                        .withImageTitleOrAltText(titleOrAltText)
                        .withSrcUrl(srcUrl)
                        .withPageTitle(pageTitle)
                        .withWebContents(webContents)
                        .build();
        mNativeDelegate = nativeDelegate;
        mOnChipClickedCallback = onChipClickedCallback;
        mOnChipShownCallback = onChipShownCallback;
    }

    @Override
    @SuppressWarnings("NullAway")
    @EnsuresNonNullIf({
        "mLensQueryParams",
        "mNativeDelegate",
        "mOnChipClickedCallback",
        "mOnChipShownCallback"
    })
    public boolean isChipSupported() {
        return mIsChipSupported;
    }

    @Override
    public void getChipRenderParams(Callback<@Nullable ChipRenderParams> chipParamsCallback) {
        if (!isChipSupported()) {
            chipParamsCallback.onResult(null);
            return;
        }

        Callback<Uri> callback =
                (uri) -> {
                    mLensQueryParams.setImageUri(uri);
                    sLensController.getChipRenderParams(
                            mLensQueryParams,
                            (chipParams) -> {
                                if (isValidChipRenderParams(chipParams)) {
                                    // A new variable to avoid infinite loop inside the merged
                                    // onClick callback.
                                    Runnable originalOnClickCallback = chipParams.onClickCallback;
                                    Runnable mergedOnClickCallback =
                                            () -> {
                                                // The onClickCallback defined in LensController.
                                                assumeNonNull(originalOnClickCallback);
                                                originalOnClickCallback.run();
                                                // The onClickCallback defined when initialize the
                                                // LensChipDelegate.
                                                mOnChipClickedCallback
                                                        .bind(chipParams.chipType)
                                                        .run();
                                            };
                                    chipParams.onClickCallback = mergedOnClickCallback;
                                    chipParams.onShowCallback =
                                            mOnChipShownCallback.bind(chipParams.chipType);
                                }
                                chipParamsCallback.onResult(chipParams);
                            });
                };

        // Must occur on UI thread.
        mNativeDelegate.retrieveImageForShare(ContextMenuImageFormat.ORIGINAL, callback);
    }

    @Override
    public void onMenuClosed() {
        // Lens controller will not react if a classification was not in progress.
        sLensController.terminateClassification();
    }

    @Override
    public boolean isValidChipRenderParams(@Nullable ChipRenderParams chipRenderParams) {
        return chipRenderParams != null
                && chipRenderParams.titleResourceId != 0
                && chipRenderParams.onClickCallback != null
                && chipRenderParams.iconResourceId != 0;
    }
}
