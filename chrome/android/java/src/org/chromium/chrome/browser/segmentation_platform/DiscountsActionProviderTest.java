// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.segmentation_platform;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doReturn;

import android.os.Handler;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.annotation.Config;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.segmentation_platform.ContextualPageActionController.ActionProvider;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.toolbar.adaptive.AdaptiveToolbarButtonVariant;
import org.chromium.components.commerce.core.DiscountInfo;
import org.chromium.components.commerce.core.ShoppingService;
import org.chromium.components.commerce.core.ShoppingService.DiscountInfoCallback;
import org.chromium.url.JUnitTestGURLs;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

/** Unit tests for {@link DiscountsActionProvider} */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class DiscountsActionProviderTest {
    @Rule public final MockitoRule mMockitoRule = MockitoJUnit.rule();
    @Mock private ShoppingService mMockShoppingService;
    @Mock private Tab mMockTab;

    private DiscountsActionProvider mDiscountsActionProvider;

    @Before
    public void setUp() {
        mDiscountsActionProvider = new DiscountsActionProvider(() -> mMockShoppingService);
    }

    SignalAccumulator getSignalAccumulator() {
        HashMap<Integer, ActionProvider> providers = new HashMap<>();
        providers.put(AdaptiveToolbarButtonVariant.DISCOUNTS, mDiscountsActionProvider);
        return new SignalAccumulator(new Handler(), mMockTab, providers);
    }

    @Test
    public void testDiscountsActionNotUsedForNonHttpUrls() {
        // Use a non-http(s) url (about:blank).
        doReturn(JUnitTestGURLs.ABOUT_BLANK).when(mMockTab).getUrl();

        SignalAccumulator accumulator = getSignalAccumulator();
        mDiscountsActionProvider.getAction(mMockTab, accumulator);
        Assert.assertFalse(accumulator.getSignal(AdaptiveToolbarButtonVariant.DISCOUNTS));
    }

    @Test
    public void testDiscountsActionNotUsedWhenDiscountsDisabled() {
        doReturn(JUnitTestGURLs.EXAMPLE_URL).when(mMockTab).getUrl();
        doReturn(false).when(mMockShoppingService).isDiscountEligibleToShowOnNavigation();

        SignalAccumulator accumulator = getSignalAccumulator();
        mDiscountsActionProvider.getAction(mMockTab, accumulator);
        Assert.assertFalse(accumulator.getSignal(AdaptiveToolbarButtonVariant.DISCOUNTS));
    }

    @Test
    public void testDiscountsActionShown() {
        doReturn(JUnitTestGURLs.EXAMPLE_URL).when(mMockTab).getUrl();
        doReturn(true).when(mMockShoppingService).isDiscountEligibleToShowOnNavigation();
        doAnswer(
                        invocation -> {
                            List<DiscountInfo> discountInfoList = new ArrayList<>();
                            discountInfoList.add(
                                    new DiscountInfo(
                                            0, 0, "en-US", "detail", "terms", "value", "code", 123,
                                            false, true, 10, 123));
                            DiscountInfoCallback callback = invocation.getArgument(1);
                            callback.onResult(invocation.getArgument(0), discountInfoList);
                            return null;
                        })
                .when(mMockShoppingService)
                .getDiscountInfoForUrl(any(), any());

        SignalAccumulator accumulator = getSignalAccumulator();
        mDiscountsActionProvider.getAction(mMockTab, accumulator);
        Assert.assertTrue(accumulator.getSignal(AdaptiveToolbarButtonVariant.DISCOUNTS));
    }

    @Test
    public void testDiscountsActionNotShown() {
        doReturn(JUnitTestGURLs.EXAMPLE_URL).when(mMockTab).getUrl();
        doReturn(true).when(mMockShoppingService).isDiscountEligibleToShowOnNavigation();
        doAnswer(
                        invocation -> {
                            List<DiscountInfo> discountInfoList = new ArrayList<>();
                            DiscountInfoCallback callback = invocation.getArgument(1);
                            callback.onResult(invocation.getArgument(0), discountInfoList);
                            return null;
                        })
                .when(mMockShoppingService)
                .getDiscountInfoForUrl(any(), any());

        SignalAccumulator accumulator = getSignalAccumulator();
        mDiscountsActionProvider.getAction(mMockTab, accumulator);
        Assert.assertFalse(accumulator.getSignal(AdaptiveToolbarButtonVariant.DISCOUNTS));
    }
}
