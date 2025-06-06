// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.browserservices.ui.controller;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.when;
import static org.robolectric.Shadows.shadowOf;

import android.os.Looper;

import androidx.test.filters.SmallTest;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.browserservices.ui.controller.CurrentPageVerifier.VerificationStatus;
import org.chromium.chrome.browser.customtabs.CustomTabIntentDataProvider;
import org.chromium.chrome.browser.customtabs.content.CustomTabActivityTabProvider;
import org.chromium.chrome.browser.customtabs.content.TabObserverRegistrar;
import org.chromium.chrome.browser.customtabs.content.TabObserverRegistrar.CustomTabTabObserver;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.components.embedder_support.util.Origin;
import org.chromium.content_public.browser.NavigationHandle;
import org.chromium.content_public.browser.Page;
import org.chromium.url.GURL;

import java.util.Collections;

/** Tests for {@link CurrentPageVerifier}. */
@RunWith(BaseRobolectricTestRunner.class)
@SuppressWarnings("DoNotMock") // Mocking GURL
public class CurrentPageVerifierTest {
    private static final Origin TRUSTED_ORIGIN = Origin.create("https://www.origin1.com/");
    private static final Origin OTHER_TRUSTED_ORIGIN = Origin.create("https://www.origin2.com/");
    private static final String TRUSTED_ORIGIN_PAGE1 = TRUSTED_ORIGIN + "/page1";
    private static final String OTHER_TRUSTED_ORIGIN_PAGE1 = OTHER_TRUSTED_ORIGIN + "/page1";
    private static final String UNTRUSTED_PAGE = "https://www.origin3.com/page1";

    public static final String PACKAGE_NAME = "package.name";

    @Rule public final MockitoRule mMockitoRule = MockitoJUnit.rule();
    @Mock TabObserverRegistrar mTabObserverRegistrar;
    @Mock ActivityLifecycleDispatcher mLifecycleDispatcher;
    @Mock CustomTabActivityTabProvider mTabProvider;
    @Mock CustomTabIntentDataProvider mIntentDataProvider;
    @Mock Tab mTab;
    @Captor ArgumentCaptor<CustomTabTabObserver> mTabObserverCaptor;

    TestVerifier mVerifierDelegate = new TestVerifier();

    private CurrentPageVerifier mCurrentPageVerifier;

    @Before
    public void setUp() {
        when(mTabProvider.getTab()).thenReturn(mTab);
        doNothing()
                .when(mTabObserverRegistrar)
                .registerActivityTabObserver(mTabObserverCaptor.capture());
        when(mIntentDataProvider.getTrustedWebActivityAdditionalOrigins())
                .thenReturn(Collections.singletonList("https://www.origin2.com/"));
        mCurrentPageVerifier =
                new CurrentPageVerifier(
                        mTabProvider,
                        mIntentDataProvider,
                        mVerifierDelegate,
                        mTabObserverRegistrar,
                        mLifecycleDispatcher);
        // TODO(peconn): Add check on permission updated being updated.
    }

    @Test
    @SmallTest
    public void verifiesOriginOfInitialPage() {
        setInitialUrl(TRUSTED_ORIGIN_PAGE1);
        mCurrentPageVerifier.onFinishNativeInitialization();
        verifyStartsVerification(TRUSTED_ORIGIN_PAGE1);
    }

    @Test
    @SmallTest
    public void statusIsPending_UntilVerificationFinished() {
        setInitialUrl(TRUSTED_ORIGIN_PAGE1);
        mCurrentPageVerifier.onFinishNativeInitialization();
        assertStatus(VerificationStatus.PENDING);
    }

    @Test
    @SmallTest
    public void statusIsSuccess_WhenVerificationSucceeds() {
        setInitialUrl(TRUSTED_ORIGIN_PAGE1);
        mCurrentPageVerifier.onFinishNativeInitialization();
        mVerifierDelegate.passVerification(Origin.create(TRUSTED_ORIGIN_PAGE1));
        assertStatus(VerificationStatus.SUCCESS);
    }

    @Test
    @SmallTest
    public void statusIsFail_WhenVerificationFails() {
        setInitialUrl(UNTRUSTED_PAGE);
        mCurrentPageVerifier.onFinishNativeInitialization();
        mVerifierDelegate.failVerification(Origin.create(UNTRUSTED_PAGE));
        assertStatus(VerificationStatus.FAILURE);
    }

    @Test
    @SmallTest
    public void verifies_WhenNavigatingToOtherTrustedOrigin() {
        setInitialUrl(TRUSTED_ORIGIN_PAGE1);
        mCurrentPageVerifier.onFinishNativeInitialization();
        mVerifierDelegate.passVerification(Origin.create(TRUSTED_ORIGIN_PAGE1));

        navigateToUrl(OTHER_TRUSTED_ORIGIN_PAGE1);
        verifyStartsVerification(OTHER_TRUSTED_ORIGIN_PAGE1);
    }

    @Test
    @SmallTest
    public void doesntUpdateState_IfVerificationFinishedAfterLeavingOrigin() {
        setInitialUrl(TRUSTED_ORIGIN_PAGE1);
        mCurrentPageVerifier.onFinishNativeInitialization();
        navigateToUrl(UNTRUSTED_PAGE);
        mVerifierDelegate.failVerification(Origin.create(UNTRUSTED_PAGE));

        assertStatus(VerificationStatus.FAILURE);
    }

    @Test
    @SmallTest
    public void reverifiesOrigin_WhenReturningToIt_IfFirstVerificationDidntFinishInTime() {
        setInitialUrl(TRUSTED_ORIGIN_PAGE1);
        mCurrentPageVerifier.onFinishNativeInitialization();
        navigateToUrl(OTHER_TRUSTED_ORIGIN_PAGE1);
        mVerifierDelegate.passVerification(Origin.create(OTHER_TRUSTED_ORIGIN_PAGE1));
        navigateToUrl(TRUSTED_ORIGIN_PAGE1);
        mVerifierDelegate.passVerification(Origin.create(TRUSTED_ORIGIN_PAGE1));
        assertStatus(VerificationStatus.SUCCESS);
    }

    private void assertStatus(@CurrentPageVerifier.VerificationStatus int status) {
        shadowOf(Looper.getMainLooper()).idle();
        assertEquals(status, mCurrentPageVerifier.getState().status);
    }

    private void verifyStartsVerification(String url) {
        assertTrue(mVerifierDelegate.hasPendingVerification(Origin.create(url)));
    }

    private static GURL createMockGurl(String url) {
        GURL gurl = Mockito.mock(GURL.class);
        when(gurl.getSpec()).thenReturn(url);
        return gurl;
    }

    private void setInitialUrl(String url) {
        when(mIntentDataProvider.getUrlToLoad()).thenReturn(url);
        // TODO(crbug.com/40549331): Pass in GURL.
        GURL gurl = createMockGurl(url);
        when(mTab.getUrl()).thenReturn(gurl);
    }

    private void navigateToUrl(String url) {
        GURL gurl = createMockGurl(url);
        when(mTab.getUrl()).thenReturn(gurl);
        NavigationHandle navigation =
                NavigationHandle.createForTesting(
                        gurl,
                        /* isRendererInitiated= */ false,
                        /* transition= */ 0,
                        /* hasUserGesture= */ false);
        for (CustomTabTabObserver tabObserver : mTabObserverCaptor.getAllValues()) {
            tabObserver.onDidStartNavigationInPrimaryMainFrame(mTab, navigation);
        }

        navigation.didFinish(
                gurl,
                /* isErrorPage= */ false,
                /* hasCommitted= */ true,
                /* isPrimaryMainFrameFragmentNavigation= */ false,
                /* isDownload= */ false,
                /* isValidSearchFormUrl= */ false,
                /* transition= */ 0,
                /* errorCode= */ 0,
                /* httpStatuscode= */ 200,
                /* isExternalProtocol= */ false,
                /* isPdf= */ false,
                /* mimeType= */ "",
                /* isSaveableNavigation= */ false,
                Page.createForTesting());
        for (CustomTabTabObserver tabObserver : mTabObserverCaptor.getAllValues()) {
            tabObserver.onDidFinishNavigationInPrimaryMainFrame(mTab, navigation);
        }
    }
}
