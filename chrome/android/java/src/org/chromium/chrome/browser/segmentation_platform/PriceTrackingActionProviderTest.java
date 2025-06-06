// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.segmentation_platform;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;

import android.os.Handler;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.invocation.InvocationOnMock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.annotation.Config;

import org.chromium.base.Callback;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.bookmarks.BookmarkModel;
import org.chromium.chrome.browser.commerce.PriceTrackingUtils;
import org.chromium.chrome.browser.commerce.PriceTrackingUtilsJni;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.segmentation_platform.ContextualPageActionController.ActionProvider;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.toolbar.adaptive.AdaptiveToolbarButtonVariant;
import org.chromium.components.bookmarks.BookmarkId;
import org.chromium.components.commerce.core.CommerceFeatureUtils;
import org.chromium.components.commerce.core.CommerceFeatureUtilsJni;
import org.chromium.components.commerce.core.ShoppingService;
import org.chromium.components.commerce.core.ShoppingService.ProductInfo;
import org.chromium.components.commerce.core.ShoppingService.ProductInfoCallback;
import org.chromium.url.JUnitTestGURLs;

import java.util.HashMap;
import java.util.Optional;

/** Unit tests for {@link PriceTrackingActionProvider} */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class PriceTrackingActionProviderTest {

    @Rule public final MockitoRule mMockitoRule = MockitoJUnit.rule();
    @Mock PriceTrackingUtils.Natives mMockPriceTrackingUtilsJni;

    @Mock private Tab mMockTab;

    @Mock private ShoppingService mShoppingService;

    @Mock private CommerceFeatureUtils.Natives mCommerceFeatureUtilsJniMock;

    @Mock private BookmarkModel mBookmarkModel;

    @Mock private Profile mProfile;

    @Before
    public void setUp() {
        setBookmarkModelReady();
    }

    private void setBookmarkModelReady() {
        PriceTrackingUtilsJni.setInstanceForTesting(mMockPriceTrackingUtilsJni);
        CommerceFeatureUtilsJni.setInstanceForTesting(mCommerceFeatureUtilsJniMock);

        // Setup bookmark model expectations.
        Mockito.doAnswer(
                        invocation -> {
                            Runnable runnable = invocation.getArgument(0);
                            runnable.run();
                            return null;
                        })
                .when(mBookmarkModel)
                .finishLoadingBookmarkModel(any());
    }

    private void setIsUrlPriceTrackableResult(boolean hasProductInfo) {
        ProductInfo testProductInfo =
                new ProductInfo(
                        null,
                        null,
                        Optional.of(12345L),
                        Optional.empty(),
                        null,
                        0,
                        null,
                        Optional.empty());
        doReturn(true).when(mCommerceFeatureUtilsJniMock).isShoppingListEligible(anyLong());
        Mockito.doAnswer(
                        invocation -> {
                            ProductInfoCallback callback = invocation.getArgument(1);
                            callback.onResult(
                                    invocation.getArgument(0),
                                    hasProductInfo ? testProductInfo : null);
                            return null;
                        })
                .when(mShoppingService)
                .getProductInfoForUrl(any(), any());
    }

    private void setIsBookmarkPriceTrackedResult(boolean isBookmarkPriceTracked) {
        doAnswer(
                        (InvocationOnMock invocation) -> {
                            ((Callback<Boolean>) invocation.getArgument(2))
                                    .onResult(isBookmarkPriceTracked);
                            return null;
                        })
                .when(mMockPriceTrackingUtilsJni)
                .isBookmarkPriceTracked(any(Profile.class), anyLong(), any());
    }

    @Test
    public void priceTrackingActionShownSuccessfully() {
        doReturn(JUnitTestGURLs.EXAMPLE_URL).when(mMockTab).getUrl();
        HashMap<Integer, ActionProvider> providers = new HashMap<>();
        PriceTrackingActionProvider provider =
                new PriceTrackingActionProvider(() -> mShoppingService, () -> mBookmarkModel);
        providers.put(AdaptiveToolbarButtonVariant.PRICE_TRACKING, provider);
        SignalAccumulator accumulator = new SignalAccumulator(new Handler(), mMockTab, providers);
        setIsUrlPriceTrackableResult(true);
        provider.getAction(mMockTab, accumulator);
        Assert.assertTrue(accumulator.getSignal(AdaptiveToolbarButtonVariant.PRICE_TRACKING));
    }

    @Test
    public void priceTrackingNotShownForNonTrackablePages() {
        doReturn(JUnitTestGURLs.GOOGLE_URL).when(mMockTab).getUrl();
        HashMap<Integer, ActionProvider> providers = new HashMap<>();
        PriceTrackingActionProvider provider =
                new PriceTrackingActionProvider(() -> mShoppingService, () -> mBookmarkModel);
        providers.put(AdaptiveToolbarButtonVariant.PRICE_TRACKING, provider);
        SignalAccumulator accumulator = new SignalAccumulator(new Handler(), mMockTab, providers);
        // URL does not support price tracking.
        setIsUrlPriceTrackableResult(false);
        // URL is bookmarked.
        doReturn(new BookmarkId(1L, 0)).when(mBookmarkModel).getUserBookmarkIdForTab(mMockTab);
        // Bookmark has no price tracking information.
        setIsBookmarkPriceTrackedResult(false);
        provider.getAction(mMockTab, accumulator);
        Assert.assertFalse(accumulator.getSignal(AdaptiveToolbarButtonVariant.PRICE_TRACKING));
    }

    @Test
    public void priceTrackingNotUsedForNonHttpUrls() {
        // Use a non-http(s) url (about:blank).
        doReturn(JUnitTestGURLs.ABOUT_BLANK).when(mMockTab).getUrl();
        HashMap<Integer, ActionProvider> providers = new HashMap<>();
        PriceTrackingActionProvider provider =
                new PriceTrackingActionProvider(() -> mShoppingService, () -> mBookmarkModel);
        providers.put(AdaptiveToolbarButtonVariant.PRICE_TRACKING, provider);
        SignalAccumulator accumulator = new SignalAccumulator(new Handler(), mMockTab, providers);
        provider.getAction(mMockTab, accumulator);
        Assert.assertFalse(accumulator.getSignal(AdaptiveToolbarButtonVariant.PRICE_TRACKING));
        // Bookmark model shouldn't be loaded/queried.
        verify(mBookmarkModel, never()).finishLoadingBookmarkModel(any());
    }
}
