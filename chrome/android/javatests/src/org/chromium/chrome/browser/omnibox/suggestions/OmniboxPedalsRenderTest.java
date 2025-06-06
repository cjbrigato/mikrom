// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox.suggestions;

import androidx.test.filters.MediumTest;

import org.junit.After;
import org.junit.AfterClass;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.params.ParameterAnnotations;
import org.chromium.base.test.params.ParameterSet;
import org.chromium.base.test.params.ParameterizedRunner;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Feature;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.night_mode.ChromeNightModeTestUtils;
import org.chromium.chrome.browser.omnibox.suggestions.action.OmniboxPedal;
import org.chromium.chrome.browser.omnibox.suggestions.base.BaseSuggestionView;
import org.chromium.chrome.browser.tabmodel.IncognitoTabHostUtils;
import org.chromium.chrome.test.ChromeJUnit4RunnerDelegate;
import org.chromium.chrome.test.transit.ChromeTransitTestRules;
import org.chromium.chrome.test.transit.FreshCtaTransitTestRule;
import org.chromium.chrome.test.transit.page.WebPageStation;
import org.chromium.chrome.test.util.ChromeRenderTestRule;
import org.chromium.chrome.test.util.OmniboxTestUtils;
import org.chromium.chrome.test.util.OmniboxTestUtils.SuggestionInfo;
import org.chromium.components.omnibox.AutocompleteMatch;
import org.chromium.components.omnibox.AutocompleteMatchBuilder;
import org.chromium.components.omnibox.AutocompleteResult;
import org.chromium.components.omnibox.OmniboxFeatures;
import org.chromium.components.omnibox.OmniboxSuggestionType;
import org.chromium.components.omnibox.action.OmniboxPedalId;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** Tests of the Omnibox Pedals feature. */
@RunWith(ParameterizedRunner.class)
@ParameterAnnotations.UseRunnerDelegate(ChromeJUnit4RunnerDelegate.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
public class OmniboxPedalsRenderTest {
    @ParameterAnnotations.ClassParameter
    private static final List<ParameterSet> sClassParams =
            List.of(
                    new ParameterSet().value(false).name("LiteMode_RegularTab"),
                    new ParameterSet().value(true).name("NightMode_RegularTab"));

    @Rule
    public ChromeRenderTestRule mRenderTestRule =
            ChromeRenderTestRule.Builder.withPublicCorpus()
                    .setBugComponent(ChromeRenderTestRule.Component.UI_BROWSER_OMNIBOX)
                    .build();

    @Rule
    public FreshCtaTransitTestRule mActivityTestRule =
            ChromeTransitTestRules.freshChromeTabbedActivityRule();

    private WebPageStation mStartingPage;
    private OmniboxTestUtils mOmniboxUtils;

    public OmniboxPedalsRenderTest(boolean nightMode) {
        OmniboxFeatures.setShouldRetainOmniboxOnFocusForTesting(false);
        ChromeNightModeTestUtils.setUpNightModeForChromeActivity(nightMode);
        mRenderTestRule.setNightModeEnabled(nightMode);
        mRenderTestRule.setVariantPrefix("RegularTab");
    }

    @BeforeClass
    public static void beforeClass() {
        ChromeNightModeTestUtils.setUpNightModeBeforeChromeActivityLaunched();
    }

    @Before
    public void setUp() throws InterruptedException {
        mStartingPage = mActivityTestRule.startOnBlankPage();
        mOmniboxUtils = new OmniboxTestUtils(mStartingPage.getActivity());
        mOmniboxUtils.requestFocus();
    }

    @After
    public void tearDown() {
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    IncognitoTabHostUtils.closeAllIncognitoTabs();
                });
    }

    @AfterClass
    public static void afterClass() {
        ChromeNightModeTestUtils.tearDownNightModeAfterChromeActivityDestroyed();
    }

    /**
     * Create a fake pedal suggestion.
     *
     * @param name The fake suggestion name.
     * @param id The Omnibox pedal type to be created.
     * @return a fake pedal suggestion.
     */
    private AutocompleteMatch createFakePedalSuggestion(String name, @OmniboxPedalId int id) {
        return AutocompleteMatchBuilder.searchWithType(OmniboxSuggestionType.SEARCH_SUGGEST)
                .setDisplayText(name)
                .setActions(List.of(new OmniboxPedal(0, "hint", "accessibility", id)))
                .build();
    }

    @Test
    @MediumTest
    @Feature("RenderTest")
    public void testRunChromeSafetyCheckPedal() throws IOException, InterruptedException {
        List<AutocompleteMatch> suggestionsList = new ArrayList<>();
        suggestionsList.add(
                createFakePedalSuggestion("pedal", OmniboxPedalId.RUN_CHROME_SAFETY_CHECK));
        mOmniboxUtils.setSuggestions(AutocompleteResult.fromCache(suggestionsList, null));
        mOmniboxUtils.checkSuggestionsShown();

        SuggestionInfo<BaseSuggestionView> info = mOmniboxUtils.findSuggestionWithActionChips();
        mRenderTestRule.render(info.view, "ClearBrowsingDataPedal");
    }

    @Test
    @MediumTest
    @Feature("RenderTest")
    public void testPlayChromeDinoGamePedal() throws IOException, InterruptedException {
        List<AutocompleteMatch> suggestionsList = new ArrayList<>();
        suggestionsList.add(
                createFakePedalSuggestion("pedal", OmniboxPedalId.PLAY_CHROME_DINO_GAME));
        mOmniboxUtils.setSuggestions(AutocompleteResult.fromCache(suggestionsList, null));
        mOmniboxUtils.checkSuggestionsShown();

        SuggestionInfo<BaseSuggestionView> info = mOmniboxUtils.findSuggestionWithActionChips();
        mRenderTestRule.render(info.view, "DinoGamePedal");
    }
}
