// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.core.IsCollectionContaining.hasItems;

import androidx.test.filters.SmallTest;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.CommandLine;
import org.chromium.base.test.util.Batch;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Features;
import org.chromium.base.test.util.Features.DisableFeatures;
import org.chromium.base.test.util.Features.EnableFeatures;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.transit.ChromeTransitTestRules;
import org.chromium.chrome.test.transit.ReusedCtaTransitTestRule;
import org.chromium.chrome.test.transit.page.WebPageStation;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Tests for {@link Features}. */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add(ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE)
@Batch(Batch.PER_CLASS)
public class FeaturesAnnotationsTest {
    @Rule
    public ReusedCtaTransitTestRule<WebPageStation> mActivityTestRule =
            ChromeTransitTestRules.blankPageStartReusedActivityRule();

    @Before
    public void setUp() {
        mActivityTestRule.start();
    }

    /**
     * Tests that {@link EnableFeatures} and {@link DisableFeatures} can alter the flags registered
     * on {@link CommandLine}.
     */
    @Test
    @SmallTest
    @EnableFeatures("One")
    @DisableFeatures("Two")
    public void testFeaturesSetExistingFlags() {
        List<String> finalEnabledList = getFeatureList(true);

        assertThat(finalEnabledList, hasItems("One"));
        assertThat(finalEnabledList.size(), equalTo(1));

        List<String> finalDisabledList = getFeatureList(false);
        assertThat(finalDisabledList, hasItems("Two"));
        assertThat(finalDisabledList.size(), equalTo(1));
    }

    /**
     * Tests the compatibility between the legacy {@link CommandLineFlags} annotation usage for
     * features and the new dedicated annotations.
     *
     * <p>If a feature is already present in the command line, it should not be removed nor alter
     * the current feature list.
     */
    @Test
    @SmallTest
    @CommandLineFlags.Add("enable-features=One,Two,Three")
    @EnableFeatures("Two")
    public void testFeaturesDoNotRemoveExistingFlags() {
        List<String> finalEnabledList = getFeatureList(true);

        assertThat(finalEnabledList, hasItems("One", "Two", "Three"));
        assertThat(finalEnabledList.size(), equalTo(3));
    }

    /**
     * Tests the compatibility between the legacy {@link CommandLineFlags} annotation usage for
     * features and the new dedicated annotations.
     *
     * <p>New features should be added to the existing command line, without removing the current
     * ones.
     */
    @Test
    @SmallTest
    @CommandLineFlags.Add("enable-features=One,Two,Three")
    @EnableFeatures({"Three", "Four"})
    public void testFeaturesAddToExistingFlags() {
        List<String> finalEnabledList = getFeatureList(true);

        assertThat(finalEnabledList, hasItems("Four"));
        assertThat(finalEnabledList.size(), equalTo(4));
    }

    private static List<String> getFeatureList(boolean enabled) {
        String switchName = enabled ? "enable-features" : "disable-features";
        ArrayList<String> allFeatures =
                new ArrayList(
                        Arrays.asList(
                                CommandLine.getInstance().getSwitchValue(switchName).split(",")));
        // To avoid interferences with features enabled or disabled outside of
        // this test class, we only return the one we set in the tests.
        ArrayList<String> relevantFeatures =
                new ArrayList(Arrays.asList("One", "Two", "Three", "Four"));
        allFeatures.retainAll(relevantFeatures);
        return allFeatures;
    }
}
