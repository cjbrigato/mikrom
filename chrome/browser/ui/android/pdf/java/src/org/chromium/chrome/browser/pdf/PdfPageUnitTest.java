// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.pdf;

import static org.mockito.Mockito.doReturn;

import android.app.Activity;
import android.graphics.Rect;
import android.net.Uri;
import android.view.View;
import android.view.ViewGroup;

import androidx.test.ext.junit.rules.ActivityScenarioRule;

import org.json.JSONObject;
import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;

import org.chromium.base.supplier.DestroyableObservableSupplier;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.base.test.util.Features.EnableFeatures;
import org.chromium.base.test.util.HistogramWatcher;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.ui.native_page.NativePageHost;
import org.chromium.chrome.browser.util.ChromeFileProvider;
import org.chromium.components.embedder_support.util.UrlConstants;
import org.chromium.content_public.browser.ContentFeatureList;
import org.chromium.ui.base.MimeTypeUtils;
import org.chromium.ui.base.TestActivity;

@RunWith(BaseRobolectricTestRunner.class)
@EnableFeatures(ContentFeatureList.ANDROID_OPEN_PDF_INLINE)
public class PdfPageUnitTest {
    @Rule
    public ActivityScenarioRule<TestActivity> mActivityScenarioRule =
            new ActivityScenarioRule<>(TestActivity.class);

    @Mock private NativePageHost mMockNativePageHost;
    @Mock private Profile mMockProfile;
    @Mock private DestroyableObservableSupplier<Rect> mMarginSupplier;
    private Activity mActivity;
    private AutoCloseable mCloseableMocks;
    private PdfInfo mPdfInfo;
    private String mPdfPageUrl;
    private String mPdfPageBlobUrl;

    private static final String DEFAULT_TAB_TITLE = "Loading PDF…";
    private static final int TAB_ID = 123;
    private static final String CONTENT_URL = "content://media/external/downloads/1000000022";
    private static final String FILE_URL = "file:///media/external/downloads/sample.pdf";
    private static final String PDF_LINK = "https://www.foo.com/testfiles/pdf/sample.pdf";
    private static final String PDF_BLOB_URL = "blob:https://www.foo.com/abc";
    private static final String EXAMPLE_URL = "https://www.example.com/";
    private static final String FILE_PATH = "/media/external/downloads/sample.pdf";
    private static final String FILE_NAME = "sample.pdf";

    @Before
    public void setUp() {
        mCloseableMocks = MockitoAnnotations.openMocks(this);
        mActivityScenarioRule
                .getScenario()
                .onActivity(
                        activity -> {
                            mActivity = activity;
                            doReturn(activity).when(mMockNativePageHost).getContext();
                        });
        doReturn(mMarginSupplier).when(mMockNativePageHost).createDefaultMarginSupplier();
        mPdfInfo = new PdfInfo();
        ChromeFileProvider.setGeneratedUriForTesting(Uri.parse(CONTENT_URL));
        PdfCoordinator.skipLoadPdfForTesting(true);
        mPdfPageUrl = PdfUtils.encodePdfPageUrl(PDF_LINK);
        mPdfPageBlobUrl = PdfUtils.encodePdfPageUrl(PDF_BLOB_URL);
    }

    @After
    public void tearDown() throws Exception {
        mCloseableMocks.close();
        ChromeFileProvider.setGeneratedUriForTesting(null);
        PdfCoordinator.skipLoadPdfForTesting(false);
    }

    @Test
    public void testCreatePdfPage_WithContentUri() throws Exception {
        HistogramWatcher histogramExpectation =
                HistogramWatcher.newBuilder()
                        .expectBooleanRecord("Android.Pdf.AssistContent.IsWorkProfile", true)
                        .build();
        String encodedUrl = PdfUtils.encodePdfPageUrl(CONTENT_URL);
        PdfPage pdfPage =
                new PdfPage(
                        mMockNativePageHost,
                        mMockProfile,
                        mActivity,
                        encodedUrl,
                        mPdfInfo,
                        DEFAULT_TAB_TITLE,
                        TAB_ID);
        Assert.assertNotNull(pdfPage);
        Assert.assertEquals(
                "Pdf page host should match.", UrlConstants.PDF_HOST, pdfPage.getHost());
        Assert.assertEquals("Pdf page url should match.", encodedUrl, pdfPage.getUrl());
        Assert.assertFalse(
                "Pdf should not be loaded when the view is not attached to window.",
                pdfPage.mPdfCoordinator.getIsPdfLoadedForTesting());

        // Simulate tab brought from background to foreground
        View view = pdfPage.mPdfCoordinator.getView();
        ViewGroup contentView = mActivity.findViewById(android.R.id.content);
        contentView.addView(view);
        Assert.assertTrue(
                "Pdf should be loaded when the view is attached to window.",
                pdfPage.mPdfCoordinator.getIsPdfLoadedForTesting());
        String jsonString = pdfPage.requestAssistContent(/*isWorkProfile*/ true);
        Assert.assertNotNull(
                "Assist content should be generated when the pdf is ready to load", jsonString);
        JSONObject jsonObject = new JSONObject(jsonString);
        JSONObject metadata = (JSONObject) jsonObject.get("file_metadata");
        Assert.assertEquals(
                "File uri should match.",
                pdfPage.mPdfCoordinator.getUri().toString(),
                metadata.get("file_uri"));
        Assert.assertEquals(
                "File name should match.", pdfPage.getTitle(), metadata.get("file_name"));
        Assert.assertEquals(
                "Mime type should match.", MimeTypeUtils.PDF_MIME_TYPE, metadata.get("mime_type"));
        Assert.assertEquals("Work profile should match.", true, metadata.get("is_work_profile"));
        histogramExpectation.assertExpected();

        contentView.removeView(view);
        pdfPage.destroy();
    }

    @Test
    public void testCreatePdfPage_WithFileUri() {
        String encodedUrl = PdfUtils.encodePdfPageUrl(FILE_URL);
        PdfPage pdfPage =
                new PdfPage(
                        mMockNativePageHost,
                        mMockProfile,
                        mActivity,
                        encodedUrl,
                        mPdfInfo,
                        DEFAULT_TAB_TITLE,
                        TAB_ID);
        Assert.assertNotNull(pdfPage);
        Assert.assertEquals("Pdf page title should match.", FILE_NAME, pdfPage.getTitle());
        Assert.assertEquals(
                "Pdf page host should match.", UrlConstants.PDF_HOST, pdfPage.getHost());
        Assert.assertEquals("Pdf page url should match.", encodedUrl, pdfPage.getUrl());
        Assert.assertFalse(
                "Pdf should not be loaded when the view is not attached to window.",
                pdfPage.mPdfCoordinator.getIsPdfLoadedForTesting());

        // Simulate tab brought from background to foreground
        View view = pdfPage.mPdfCoordinator.getView();
        ViewGroup contentView = mActivity.findViewById(android.R.id.content);
        contentView.addView(view);
        Assert.assertTrue(
                "Pdf should be loaded when the view is attached to window.",
                pdfPage.mPdfCoordinator.getIsPdfLoadedForTesting());
        contentView.removeView(view);
    }

    @Test
    public void testCreatePdfPage_WithPdfLink_Https() throws Exception {
        testCreatePdfPage_WithPdfLink(mPdfPageUrl);
    }

    @Test
    public void testCreatePdfPage_WithPdfLink_Blob() throws Exception {
        testCreatePdfPage_WithPdfLink(mPdfPageBlobUrl);
    }

    private void testCreatePdfPage_WithPdfLink(String pdfPageUrl) throws Exception {
        HistogramWatcher histogramExpectation =
                HistogramWatcher.newBuilder()
                        .expectBooleanRecord("Android.Pdf.AssistContent.IsWorkProfile", false)
                        .build();
        PdfPage pdfPage =
                new PdfPage(
                        mMockNativePageHost,
                        mMockProfile,
                        mActivity,
                        pdfPageUrl,
                        mPdfInfo,
                        DEFAULT_TAB_TITLE,
                        TAB_ID);
        Assert.assertNotNull(pdfPage);
        Assert.assertFalse(
                "Pdf should not be loaded when the download is not completed.",
                pdfPage.mPdfCoordinator.getIsPdfLoadedForTesting());
        Assert.assertNull(
                "Assist content cannot be generated when the pdf is not ready to load",
                pdfPage.requestAssistContent(/*isWorkProfile*/ false));

        // Simulate download complete
        pdfPage.onDownloadComplete(FILE_NAME, FILE_PATH, true);
        Assert.assertEquals("Pdf page title should match.", FILE_NAME, pdfPage.getTitle());
        Assert.assertEquals(
                "Pdf page host should match.", UrlConstants.PDF_HOST, pdfPage.getHost());
        Assert.assertEquals("Pdf page url should match.", pdfPageUrl, pdfPage.getUrl());
        Assert.assertFalse(
                "Pdf should not be loaded when the view is not attached to window.",
                pdfPage.mPdfCoordinator.getIsPdfLoadedForTesting());

        // Simulate tab brought from background to foreground
        View view = pdfPage.mPdfCoordinator.getView();
        ViewGroup contentView = mActivity.findViewById(android.R.id.content);
        contentView.addView(view);
        Assert.assertTrue(
                "Pdf should be loaded when the view is attached to window.",
                pdfPage.mPdfCoordinator.getIsPdfLoadedForTesting());
        String jsonString = pdfPage.requestAssistContent(/*isWorkProfile*/ false);
        Assert.assertNotNull(
                "Assist content should be generated when the pdf is ready to load", jsonString);
        JSONObject jsonObject = new JSONObject(jsonString);
        JSONObject metadata = (JSONObject) jsonObject.get("file_metadata");
        Assert.assertEquals(
                "File uri should match.",
                pdfPage.mPdfCoordinator.getUri().toString(),
                metadata.get("file_uri"));
        Assert.assertEquals(
                "File name should match.", pdfPage.getTitle(), metadata.get("file_name"));
        Assert.assertEquals(
                "Mime type should match.", MimeTypeUtils.PDF_MIME_TYPE, metadata.get("mime_type"));
        Assert.assertEquals("Work profile should match.", false, metadata.get("is_work_profile"));
        histogramExpectation.assertExpected();

        contentView.removeView(view);
        pdfPage.destroy();
    }
}
