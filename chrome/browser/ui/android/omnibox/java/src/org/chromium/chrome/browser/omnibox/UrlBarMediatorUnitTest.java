// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.text.Selection;
import android.text.SpannableStringBuilder;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

import org.chromium.base.Callback;
import org.chromium.base.ContextUtils;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.omnibox.UrlBar.ScrollType;
import org.chromium.chrome.browser.omnibox.UrlBarCoordinator.SelectionState;
import org.chromium.chrome.browser.omnibox.UrlBarViewBinderUnitTest.ShadowOmniboxResourceProvider;
import org.chromium.components.omnibox.OmniboxUrlEmphasizer;
import org.chromium.components.omnibox.OmniboxUrlEmphasizer.UrlEmphasisColorSpan;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.modelutil.PropertyObservable.PropertyObserver;
import org.chromium.url.GURL;

/** Unit tests for {@link UrlBarMediator}. */
@RunWith(BaseRobolectricTestRunner.class)
@Config(
        manifest = Config.NONE,
        shadows = {ShadowOmniboxResourceProvider.class})
public class UrlBarMediatorUnitTest {
    @Rule public final MockitoRule mMockitoRule = MockitoJUnit.rule();
    @Mock Callback<String> mMockUrlTextListener;
    @Mock Callback<String> mAnotherUrlTextMockListener;
    @Mock Callback<Boolean> mFocusChangeCallback;

    Context mContext;
    PropertyModel mModel;
    UrlBarMediator mMediator;

    @Before
    public void setUp() {
        mContext = ContextUtils.getApplicationContext();
        mModel = new PropertyModel(UrlBarProperties.ALL_KEYS);
        mMediator =
                new UrlBarMediator(
                        ContextUtils.getApplicationContext(), mModel, mFocusChangeCallback) {
                    @Override
                    protected String sanitizeTextForPaste(String text) {
                        return text.trim();
                    }
                };
    }

    @Test
    public void setUrlData_SendsUpdates() {
        UrlBarData baseData =
                UrlBarData.create(
                        new GURL("http://www.example.com"),
                        spannable("www.example.com"),
                        0,
                        14,
                        "Blah");
        UrlBarData dataWithDifferentDisplay =
                UrlBarData.create(
                        new GURL("http://www.example.com"),
                        spannable("www.foo.com"),
                        0,
                        11,
                        "Blah");
        UrlBarData dataWithDifferentEditing =
                UrlBarData.create(
                        new GURL("http://www.example.com"),
                        spannable("www.example.com"),
                        0,
                        14,
                        "Bar");

        Assert.assertTrue(mMediator.setUrlBarData(baseData, UrlBar.ScrollType.SCROLL_TO_TLD, 4));

        @SuppressWarnings("unchecked")
        PropertyObserver<PropertyKey> observer = Mockito.mock(PropertyObserver.class);
        mModel.addObserver(observer);
        Mockito.<PropertyObserver>reset(observer);

        Assert.assertTrue(
                mMediator.setUrlBarData(
                        dataWithDifferentDisplay, UrlBar.ScrollType.SCROLL_TO_TLD, 4));
        Assert.assertTrue(
                mMediator.setUrlBarData(
                        dataWithDifferentEditing, UrlBar.ScrollType.SCROLL_TO_TLD, 4));
        Assert.assertTrue(
                mMediator.setUrlBarData(
                        dataWithDifferentEditing, UrlBar.ScrollType.SCROLL_TO_BEGINNING, 4));

        Mockito.verify(observer, Mockito.times(3))
                .onPropertyChanged(mModel, UrlBarProperties.TEXT_STATE);
    }

    @Test
    public void setUrlData_PreventsDuplicateUpdates() {
        UrlBarData data1 =
                UrlBarData.create(
                        new GURL("http://www.example.com"),
                        spannable("www.example.com"),
                        0,
                        0,
                        "Blah");
        UrlBarData data2 =
                UrlBarData.create(
                        new GURL("http://www.example.com"),
                        spannable("www.example.com"),
                        0,
                        0,
                        "Blah");

        Assert.assertTrue(mMediator.setUrlBarData(data1, UrlBar.ScrollType.SCROLL_TO_TLD, 4));

        @SuppressWarnings("unchecked")
        PropertyObserver<PropertyKey> observer = Mockito.mock(PropertyObserver.class);
        mModel.addObserver(observer);
        Mockito.<PropertyObserver>reset(observer);

        Assert.assertFalse(mMediator.setUrlBarData(data1, UrlBar.ScrollType.SCROLL_TO_TLD, 4));
        Assert.assertFalse(mMediator.setUrlBarData(data2, UrlBar.ScrollType.SCROLL_TO_TLD, 4));

        Mockito.verifyNoMoreInteractions(observer);
    }

    @Test
    public void setUrlData_ScrollStateForDataUrl() {
        String displayText = "data:text/html,blah";
        UrlBarData data =
                UrlBarData.create(
                        new GURL("data:text/html,blah,blah"),
                        spannable(displayText),
                        0,
                        displayText.length(),
                        null);
        Assert.assertTrue(
                mMediator.setUrlBarData(
                        data,
                        UrlBar.ScrollType.SCROLL_TO_TLD,
                        UrlBarCoordinator.SelectionState.SELECT_ALL));

        // The scroll state should be overridden to SCROLL_TO_BEGINNING for file-type schemes.
        Assert.assertEquals(
                UrlBar.ScrollType.SCROLL_TO_BEGINNING,
                mModel.get(UrlBarProperties.TEXT_STATE).scrollType);
    }

    @Test
    public void setUrlData_ScrollStateForAboutUrl() {
        String displayText = "about:blank#verylongurl.totallylegit.notsuspicious.url.com";
        UrlBarData data =
                UrlBarData.create(
                        new GURL(displayText),
                        spannable(displayText),
                        0,
                        displayText.length(),
                        null);
        Assert.assertTrue(
                mMediator.setUrlBarData(
                        data,
                        UrlBar.ScrollType.SCROLL_TO_TLD,
                        UrlBarCoordinator.SelectionState.SELECT_ALL));

        // The scroll state should be overridden to SCROLL_TO_BEGINNING for file-type schemes.
        Assert.assertEquals(
                UrlBar.ScrollType.SCROLL_TO_BEGINNING,
                mModel.get(UrlBarProperties.TEXT_STATE).scrollType);
    }

    @Test
    public void urlDataComparison_equals() {
        Assert.assertTrue(UrlBarMediator.isNewTextEquivalentToExistingText(null, null));

        // Empty display text, regardless of spanned state.
        Assert.assertTrue(
                UrlBarMediator.isNewTextEquivalentToExistingText(
                        UrlBarData.create(null, spannable(""), 0, 0, null),
                        UrlBarData.create(null, "", 0, 0, null)));

        // No editing text, equal display text
        Assert.assertTrue(
                UrlBarMediator.isNewTextEquivalentToExistingText(
                        UrlBarData.create(null, spannable("Test"), 0, 0, null),
                        UrlBarData.create(null, spannable("Test"), 0, 0, null)));

        // Equal display and editing text
        Assert.assertTrue(
                UrlBarMediator.isNewTextEquivalentToExistingText(
                        UrlBarData.create(null, spannable("Test"), 0, 0, "Blah"),
                        UrlBarData.create(null, spannable("Test"), 0, 0, "Blah")));

        // Equal complex display text and editing text
        SpannableStringBuilder text1 = spannable("Test");
        text1.setSpan(new UrlEmphasisColorSpan(3), 0, 3, 0);
        text1.setSpan(new UrlEmphasisColorSpan(4), 1, 3, 0);
        text1.setSpan(new OmniboxUrlEmphasizer.UrlEmphasisSecurityErrorSpan(), 0, 1, 0);

        SpannableStringBuilder text2 = spannable("Test");
        text2.setSpan(new UrlEmphasisColorSpan(3), 0, 3, 0);
        text2.setSpan(new UrlEmphasisColorSpan(4), 1, 3, 0);
        text2.setSpan(new OmniboxUrlEmphasizer.UrlEmphasisSecurityErrorSpan(), 0, 1, 0);

        Assert.assertTrue(
                UrlBarMediator.isNewTextEquivalentToExistingText(
                        UrlBarData.create(null, text1, 0, 0, "Blah"),
                        UrlBarData.create(null, text2, 0, 0, "Blah")));

        // Ensure adding non-emphasis spans does not mess up equality.
        text1.setSpan(new Object(), 0, 3, 0);
        Selection.setSelection(text2, 0, 1);
        Assert.assertTrue(
                UrlBarMediator.isNewTextEquivalentToExistingText(
                        UrlBarData.create(null, text1, 0, 0, "Blah"),
                        UrlBarData.create(null, text2, 0, 0, "Blah")));
    }

    @Test
    public void urlDataComparison_notEquals() {
        Assert.assertFalse(
                UrlBarMediator.isNewTextEquivalentToExistingText(null, UrlBarData.EMPTY));
        Assert.assertFalse(
                UrlBarMediator.isNewTextEquivalentToExistingText(UrlBarData.EMPTY, null));

        // Different display texts
        Assert.assertFalse(
                UrlBarMediator.isNewTextEquivalentToExistingText(
                        UrlBarData.create(null, spannable("Test"), 0, 0, null),
                        UrlBarData.create(null, spannable("Test2"), 0, 0, null)));

        // Mismatched spannable state of display text
        Assert.assertFalse(
                UrlBarMediator.isNewTextEquivalentToExistingText(
                        UrlBarData.create(null, spannable("Test"), 0, 0, null),
                        UrlBarData.create(null, "Test2", 0, 0, null)));

        // Equal display text, different editing text
        Assert.assertFalse(
                UrlBarMediator.isNewTextEquivalentToExistingText(
                        UrlBarData.create(null, spannable("Test"), 0, 0, "Blah"),
                        UrlBarData.create(null, spannable("Test"), 0, 0, "Blah2")));

        // Equal display text content, but different emphasis spans
        SpannableStringBuilder text1 = spannable("Test");
        SpannableStringBuilder text2 = spannable("Test");
        text2.setSpan(new UrlEmphasisColorSpan(3), 0, 3, 0);
        text2.setSpan(new UrlEmphasisColorSpan(4), 1, 3, 0);
        text2.setSpan(new OmniboxUrlEmphasizer.UrlEmphasisSecurityErrorSpan(), 0, 1, 0);

        Assert.assertFalse(
                UrlBarMediator.isNewTextEquivalentToExistingText(
                        UrlBarData.create(null, text1, 0, 0, "Blah"),
                        UrlBarData.create(null, text2, 0, 0, "Blah")));

        // Add a subset of emphasis spans, but not all.
        text1.setSpan(new UrlEmphasisColorSpan(3), 0, 3, 0);
        text1.setSpan(new UrlEmphasisColorSpan(4), 1, 3, 0);
        Assert.assertFalse(
                UrlBarMediator.isNewTextEquivalentToExistingText(
                        UrlBarData.create(null, text1, 0, 0, "Blah"),
                        UrlBarData.create(null, text2, 0, 0, "Blah")));
    }

    @Test
    public void pasteTextValidation() {
        ClipboardManager clipboard =
                (ClipboardManager)
                        RuntimeEnvironment.application.getSystemService(Context.CLIPBOARD_SERVICE);
        clipboard.setPrimaryClip(null);
        Assert.assertNull(mMediator.getTextToPaste());

        clipboard.setPrimaryClip(ClipData.newPlainText("", ""));
        Assert.assertEquals("", mMediator.getTextToPaste());

        clipboard.setPrimaryClip(ClipData.newPlainText("", "test"));
        Assert.assertEquals("test", mMediator.getTextToPaste());

        clipboard.setPrimaryClip(ClipData.newPlainText("", "    test     "));
        Assert.assertEquals("test", mMediator.getTextToPaste());
    }

    @Test
    public void cutCopyReplacementTextValidation() {
        String url = "https://www.test.com/blah";
        String displayText = "test.com/blah";
        String editingText = "www.test.com/blah";
        mMediator.setUrlBarData(
                UrlBarData.create(new GURL(url), displayText, 0, 12, editingText),
                UrlBar.ScrollType.NO_SCROLL,
                UrlBarCoordinator.SelectionState.SELECT_ALL);

        // Replacement is only valid if selecting the full text.
        Assert.assertNull(mMediator.getReplacementCutCopyText(editingText, 1, 2));

        // Editing text will be replaced with the full URL if selecting all of the text.
        Assert.assertEquals(
                url, mMediator.getReplacementCutCopyText(editingText, 0, editingText.length()));

        // If selecting just the URL portion of the editing text, it should be replaced with the
        // unformatted URL.
        Assert.assertEquals(
                "https://www.test.com", mMediator.getReplacementCutCopyText(editingText, 0, 12));

        // If the path changed in the editing text changed but the domain is untouched, it should
        // be replaced with the full domain from the unformatted URL.
        Assert.assertEquals(
                "https://www.test.com/foo",
                mMediator.getReplacementCutCopyText("www.test.com/foo", 0, 16));
    }

    @Test
    public void setUrlBarHintText() {
        mMediator.setUrlBarHintText("Hint 1");
        Assert.assertEquals("Hint 1", mModel.get(UrlBarProperties.HINT_TEXT));
        mMediator.setUrlBarHintText("Incognito Hint");
        Assert.assertEquals("Incognito Hint", mModel.get(UrlBarProperties.HINT_TEXT));
    }

    @Test
    public void setIsInCct() {
        Assert.assertFalse(mModel.get(UrlBarProperties.IS_IN_CCT));
        mMediator.setIsInCct(true);
        Assert.assertTrue(mModel.get(UrlBarProperties.IS_IN_CCT));
    }

    @Test
    public void setShowOriginOnly() {
        UrlBarData baseData =
                UrlBarData.create(
                        new GURL("http://www.example.com/a_path_to_ignore"),
                        spannable("http://www.example.com/a_path_to_ignore"),
                        0,
                        22,
                        "Blah");
        mMediator.setUrlBarData(
                baseData, UrlBar.ScrollType.SCROLL_TO_TLD, SelectionState.SELECT_END);

        Assert.assertEquals(
                "http://www.example.com/a_path_to_ignore",
                mModel.get(UrlBarProperties.TEXT_STATE).text.toString());

        mMediator.setShowOriginOnly(true);
        Assert.assertEquals(
                "http://www.example.com", mModel.get(UrlBarProperties.TEXT_STATE).text.toString());

        mMediator.setShowOriginOnly(false);
        Assert.assertEquals(
                "http://www.example.com/a_path_to_ignore",
                mModel.get(UrlBarProperties.TEXT_STATE).text.toString());
    }

    @Test
    public void setShowOriginOnly_nonUrlText() {
        UrlBarData baseData = UrlBarData.forNonUrlText("non url");
        mMediator.setUrlBarData(baseData, ScrollType.NO_SCROLL, SelectionState.SELECT_END);
        Assert.assertEquals("non url", mModel.get(UrlBarProperties.TEXT_STATE).text.toString());

        mMediator.setShowOriginOnly(true);
        Assert.assertEquals("non url", mModel.get(UrlBarProperties.TEXT_STATE).text.toString());
    }

    private static SpannableStringBuilder spannable(String text) {
        return new SpannableStringBuilder(text);
    }
}
