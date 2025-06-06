// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.ui.widget;

import static org.junit.Assert.assertNotEquals;

import android.content.Context;
import android.view.InflateException;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.TextView;

import androidx.annotation.LayoutRes;
import androidx.test.filters.MediumTest;

import org.junit.BeforeClass;
import org.junit.ClassRule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseActivityTestRule;
import org.chromium.base.test.BaseJUnit4ClassRunner;
import org.chromium.base.test.util.Batch;
import org.chromium.base.test.util.Features.DisableFeatures;
import org.chromium.base.test.util.Features.EnableFeatures;
import org.chromium.ui.R;
import org.chromium.ui.base.UiAndroidFeatures;
import org.chromium.ui.test.util.BlankUiTestActivity;

/** Unit tests for {@link TextViewWithLeading}. */
@RunWith(BaseJUnit4ClassRunner.class)
@Batch(Batch.PER_CLASS)
public class TextViewWithLeadingTest {
    @ClassRule
    public static BaseActivityTestRule<BlankUiTestActivity> sActivityTestRule =
            new BaseActivityTestRule<>(BlankUiTestActivity.class);

    @BeforeClass
    public static void beforeClass() {
        sActivityTestRule.launchActivity(null);
    }

    private void inflateAndVerify(@LayoutRes int layoutRes) {
        TextView textView = (TextView) inflate(layoutRes);
        assertNotEquals(/* unexpected= */ 0, textView.getLineSpacingExtra(), /* delta= */ 1);
    }

    private View inflate(@LayoutRes int layoutRes) {
        Context context = sActivityTestRule.getActivity();
        return LayoutInflater.from(context).inflate(layoutRes, /* root= */ null);
    }

    @Test
    @MediumTest
    public void testDirect() {
        inflateAndVerify(R.layout.text_view_with_leading_direct);
    }

    @Test
    @MediumTest
    public void testStyle() {
        inflateAndVerify(R.layout.text_view_with_leading_style);
    }

    @Test
    @MediumTest
    public void testTextAppearance() {
        inflateAndVerify(R.layout.text_view_with_leading_text_appearance);
    }

    @Test
    @MediumTest
    public void testBadTextAppearance() {
        inflate(R.layout.text_view_with_leading_bad_text_appearance);
    }

    @Test
    @MediumTest
    public void testStyleIntoTextAppearance() {
        inflateAndVerify(R.layout.text_view_with_leading_style_into_text_appearance);
    }

    @Test(expected = InflateException.class)
    @MediumTest
    @EnableFeatures(UiAndroidFeatures.REQUIRE_LEADING_IN_TEXT_VIEW_WITH_LEADING)
    public void testNoLeading() {
        inflate(R.layout.text_view_with_leading_no_leading);
    }

    @Test
    @MediumTest
    @DisableFeatures(UiAndroidFeatures.REQUIRE_LEADING_IN_TEXT_VIEW_WITH_LEADING)
    public void testLeadingKillSwitch() {
        inflateAndVerify(R.layout.text_view_with_leading_direct);
    }

    @Test
    @MediumTest
    @DisableFeatures(UiAndroidFeatures.REQUIRE_LEADING_IN_TEXT_VIEW_WITH_LEADING)
    public void testNoLeadingKillSwitch() {
        inflate(R.layout.text_view_with_leading_no_leading);
    }
}
