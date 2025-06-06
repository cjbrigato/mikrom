// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.browsing_data;

import static androidx.test.espresso.matcher.ViewMatchers.assertThat;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import android.os.Build;

import androidx.test.filters.MediumTest;
import androidx.test.filters.SmallTest;
import androidx.test.platform.app.InstrumentationRegistry;

import org.hamcrest.Matchers;
import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.Batch;
import org.chromium.base.test.util.CallbackHelper;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.CriteriaHelper;
import org.chromium.base.test.util.DisableIf;
import org.chromium.base.test.util.UserActionTester;
import org.chromium.chrome.browser.ChromeTabbedActivity;
import org.chromium.chrome.browser.browserservices.intents.BrowserServicesIntentDataProvider;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.multiwindow.MultiWindowTestHelper;
import org.chromium.chrome.browser.profiles.ProfileManager;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabState;
import org.chromium.chrome.browser.tab.TabStateExtractor;
import org.chromium.chrome.browser.tab.WebContentsStateBridge;
import org.chromium.chrome.browser.tabmodel.TabClosureParams;
import org.chromium.chrome.browser.webapps.TestFetchStorageCallback;
import org.chromium.chrome.browser.webapps.WebappDataStorage;
import org.chromium.chrome.browser.webapps.WebappRegistry;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.transit.AutoResetCtaTransitTestRule;
import org.chromium.chrome.test.transit.ChromeTransitTestRules;
import org.chromium.chrome.test.util.ChromeTabUtils;
import org.chromium.chrome.test.util.browser.webapps.WebappTestHelper;
import org.chromium.content_public.browser.NavigationController;
import org.chromium.content_public.browser.NavigationEntry;
import org.chromium.content_public.browser.WebContents;
import org.chromium.net.test.EmbeddedTestServer;
import org.chromium.net.test.util.TestWebServer;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;

/** Integration tests for ClearBrowsingDataPreferences. */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
@Batch(Batch.PER_CLASS)
public class BrowsingDataBridgeTest {
    private static final String TEST_FILE_PATH_1 = "/chrome/test/data/browsing_data/a.html";
    private static final String TEST_FILE_PATH_2 = "/chrome/test/data/browsing_data/b.html";

    @Rule
    public AutoResetCtaTransitTestRule mActivityTestRule =
            ChromeTransitTestRules.fastAutoResetCtaActivityRule();

    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private BrowsingDataBridge.Natives mBrowsingDataBridgeJniMock;

    private CallbackHelper mCallbackHelper;
    private BrowsingDataBridge.OnClearBrowsingDataListener mListener;
    private UserActionTester mActionTester;
    private EmbeddedTestServer mTestServer;

    @Before
    public void setUp() throws Exception {
        mCallbackHelper = new CallbackHelper();
        mListener = mCallbackHelper::notifyCalled;
        mTestServer = mActivityTestRule.getTestServer();
        mActionTester = new UserActionTester();
    }

    @After
    public void tearDown() {
        mActionTester.tearDown();
    }

    private BrowsingDataBridge getBrowsingDataBridge() {
        return ThreadUtils.runOnUiThreadBlocking(
                () -> BrowsingDataBridge.getForProfile(ProfileManager.getLastUsedRegularProfile()));
    }

    /** Test no clear browsing data calls. */
    @Test
    @SmallTest
    public void testNoCalls() throws Exception {
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    getBrowsingDataBridge()
                            .clearBrowsingData(mListener, new int[] {}, TimePeriod.ALL_TIME);
                });
        mCallbackHelper.waitForCallback(0);
        assertThat(
                mActionTester.toString(),
                getActions(),
                Matchers.contains("ClearBrowsingData_Everything"));
    }

    /** Test cookies deletion. */
    @Test
    @SmallTest
    public void testCookiesDeleted() throws Exception {
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    getBrowsingDataBridge()
                            .clearBrowsingData(
                                    mListener,
                                    new int[] {BrowsingDataType.SITE_DATA},
                                    TimePeriod.LAST_HOUR);
                });
        mCallbackHelper.waitForCallback(0);
        assertThat(
                mActionTester.toString(),
                getActions(),
                Matchers.containsInAnyOrder(
                        "ClearBrowsingData_LastHour",
                        "ClearBrowsingData_MaskContainsUnprotectedWeb",
                        "ClearBrowsingData_Cookies",
                        "ClearBrowsingData_SiteUsageData",
                        "ClearBrowsingData_ContentLicenses"));
    }

    /** Get ClearBrowsingData related actions, filter all other actions to avoid flakes. */
    private List<String> getActions() {
        List<String> actions = new ArrayList<>(mActionTester.getActions());
        Iterator<String> it = actions.iterator();
        while (it.hasNext()) {
            if (!it.next().startsWith("ClearBrowsingData_")) {
                it.remove();
            }
        }
        return actions;
    }

    /** Test history deletion. */
    @Test
    @SmallTest
    public void testHistoryDeleted() throws Exception {
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    getBrowsingDataBridge()
                            .clearBrowsingData(
                                    mListener,
                                    new int[] {BrowsingDataType.HISTORY},
                                    TimePeriod.LAST_DAY);
                });
        mCallbackHelper.waitForCallback(0);
        assertThat(
                mActionTester.toString(),
                getActions(),
                Matchers.containsInAnyOrder(
                        "ClearBrowsingData_LastDay",
                        "ClearBrowsingData_MaskContainsUnprotectedWeb",
                        "ClearBrowsingData_History"));
    }

    /** Test deleting cache and content settings. */
    @Test
    @SmallTest
    public void testClearingSiteSettingsAndCache() throws Exception {
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    getBrowsingDataBridge()
                            .clearBrowsingData(
                                    mListener,
                                    new int[] {
                                        BrowsingDataType.CACHE, BrowsingDataType.SITE_SETTINGS,
                                    },
                                    TimePeriod.FOUR_WEEKS);
                });
        mCallbackHelper.waitForCallback(0);
        assertThat(
                mActionTester.toString(),
                getActions(),
                Matchers.containsInAnyOrder(
                        "ClearBrowsingData_LastMonth",
                        "ClearBrowsingData_MaskContainsUnprotectedWeb",
                        "ClearBrowsingData_Cache",
                        "ClearBrowsingData_ShaderCache",
                        "ClearBrowsingData_ContentSettings"));
    }

    /** Test deleting cache and content settings with important sites. */
    @Test
    @SmallTest
    public void testClearingSiteSettingsAndCacheWithImportantSites() throws Exception {
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    getBrowsingDataBridge()
                            .clearBrowsingDataExcludingDomains(
                                    mListener,
                                    new int[] {
                                        BrowsingDataType.CACHE, BrowsingDataType.SITE_SETTINGS,
                                    },
                                    TimePeriod.FOUR_WEEKS,
                                    new String[] {"google.com"},
                                    new int[] {1},
                                    new String[0],
                                    new int[0]);
                });
        mCallbackHelper.waitForCallback(0);
        assertThat(
                mActionTester.toString(),
                getActions(),
                Matchers.containsInAnyOrder(
                        "ClearBrowsingData_LastMonth",
                        // ClearBrowsingData_MaskContainsUnprotectedWeb is logged
                        // twice because important storage is deleted separately.
                        "ClearBrowsingData_MaskContainsUnprotectedWeb",
                        "ClearBrowsingData_MaskContainsUnprotectedWeb",
                        "ClearBrowsingData_Cache",
                        "ClearBrowsingData_ShaderCache",
                        "ClearBrowsingData_ContentSettings"));
    }

    /** Test deleting all browsing data. (Except bookmarks, they are deleted in Java code) */
    @Test
    @SmallTest
    public void testClearingAll() throws Exception {
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    getBrowsingDataBridge()
                            .clearBrowsingData(
                                    mListener,
                                    new int[] {
                                        BrowsingDataType.CACHE,
                                        BrowsingDataType.SITE_DATA,
                                        BrowsingDataType.FORM_DATA,
                                        BrowsingDataType.HISTORY,
                                        BrowsingDataType.PASSWORDS,
                                        BrowsingDataType.SITE_SETTINGS,
                                        BrowsingDataType.TABS
                                    },
                                    TimePeriod.LAST_WEEK);
                });
        mCallbackHelper.waitForCallback(0);
        assertThat(
                mActionTester.toString(),
                getActions(),
                Matchers.containsInAnyOrder(
                        "ClearBrowsingData_LastWeek",
                        "ClearBrowsingData_MaskContainsUnprotectedWeb",
                        "ClearBrowsingData_Cache",
                        "ClearBrowsingData_ShaderCache",
                        "ClearBrowsingData_Cookies",
                        "ClearBrowsingData_Autofill",
                        "ClearBrowsingData_History",
                        "ClearBrowsingData_Passwords",
                        "ClearBrowsingData_ContentSettings",
                        "ClearBrowsingData_SiteUsageData",
                        "ClearBrowsingData_ContentLicenses",
                        "ClearBrowsingData_Tabs"));
    }

    /** Tests navigation entries from frozen state are removed by history deletions. */
    @Test
    @MediumTest
    public void testFrozenNavigationDeletion() throws Exception {
        final String url1 = mTestServer.getURL(TEST_FILE_PATH_1);
        final String url2 = mTestServer.getURL(TEST_FILE_PATH_2);

        // Navigate to url1 and url2, close and recreate as frozen tab.
        Tab tab = mActivityTestRule.loadUrlInNewTab(url1);
        mActivityTestRule.loadUrl(url2);
        Tab[] frozen = new Tab[1];
        WebContents[] restored = new WebContents[1];
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    TabState state = TabStateExtractor.from(tab);
                    mActivityTestRule
                            .getActivity()
                            .getCurrentTabModel()
                            .getTabRemover()
                            .closeTabs(
                                    TabClosureParams.closeTab(tab).allowUndo(false).build(),
                                    /* allowDialog= */ false);
                    frozen[0] =
                            mActivityTestRule
                                    .getActivity()
                                    .getCurrentTabCreator()
                                    .createFrozenTab(state, tab.getId(), 1);
                    restored[0] =
                            WebContentsStateBridge.restoreContentsFromByteBuffer(
                                    TabStateExtractor.from(frozen[0]).contentsState, false);
                });

        // Check content of frozen state.
        NavigationController controller = restored[0].getNavigationController();
        assertEquals(1, controller.getLastCommittedEntryIndex());
        assertThat(getUrls(controller), Matchers.contains(url1, url2));
        assertNull(frozen[0].getWebContents());

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    getBrowsingDataBridge()
                            .clearBrowsingData(
                                    mListener,
                                    new int[] {
                                        BrowsingDataType.HISTORY,
                                    },
                                    TimePeriod.LAST_WEEK);
                });

        mCallbackHelper.waitForCallback(0);

        // Check that frozen state was cleaned up.
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    restored[0] =
                            WebContentsStateBridge.restoreContentsFromByteBuffer(
                                    TabStateExtractor.from(frozen[0]).contentsState, false);
                });

        controller = restored[0].getNavigationController();
        assertEquals(0, controller.getLastCommittedEntryIndex());
        assertThat(getUrls(controller), Matchers.contains(url2));
        assertNull(frozen[0].getWebContents());
    }

    /**
     * Tests that calling getContentsStateAsByteBuffer on a tab that has never committed a
     * navigation results in a null ByteBuffer. Regression test for https://crbug.com/1240138.
     */
    @Test
    @MediumTest
    public void testInitialNavigationEntryNotPersisted() throws Exception {
        TestWebServer webServer = TestWebServer.start();
        final String noContentUrl = webServer.setResponseWithNoContentStatus("/nocontent.html");
        Tab tab = mActivityTestRule.loadUrlInNewTab(noContentUrl);
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    assertNull(
                            WebContentsStateBridge.getContentsStateAsByteBuffer(
                                    tab.getWebContents()));
                });
    }

    /** Tests navigation entries are removed by history deletions. */
    @Test
    @MediumTest
    public void testNavigationDeletion() throws Exception {
        final String url1 = mTestServer.getURL(TEST_FILE_PATH_1);
        final String url2 = mTestServer.getURL(TEST_FILE_PATH_2);

        // Navigate to url1 and url2.
        Tab tab = mActivityTestRule.loadUrlInNewTab(url1);
        mActivityTestRule.loadUrl(url2);
        NavigationController controller = tab.getWebContents().getNavigationController();
        assertTrue(tab.canGoBack());
        assertEquals(1, controller.getLastCommittedEntryIndex());
        assertThat(getUrls(controller), Matchers.contains(url1, url2));

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    getBrowsingDataBridge()
                            .clearBrowsingData(
                                    mListener,
                                    new int[] {
                                        BrowsingDataType.HISTORY,
                                    },
                                    TimePeriod.LAST_WEEK);
                });
        mCallbackHelper.waitForCallback(0);

        // Check navigation entries.
        assertFalse(tab.canGoBack());
        assertEquals(0, controller.getLastCommittedEntryIndex());
        assertThat(getUrls(controller), Matchers.contains(url2));
    }

    /** Tests that web apps are cleared when the "cookies and site data" option is selected. */
    @Test
    @MediumTest
    public void testClearingSiteDataClearsWebapps() throws Exception {
        TestFetchStorageCallback callback = new TestFetchStorageCallback();
        WebappRegistry.getInstance().register("first", callback);
        callback.waitForCallback(0);
        Assert.assertEquals(
                new HashSet<>(Arrays.asList("first")),
                WebappRegistry.getRegisteredWebappIdsForTesting());

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    getBrowsingDataBridge()
                            .clearBrowsingData(
                                    mListener,
                                    new int[] {
                                        org.chromium.chrome.browser.browsing_data.BrowsingDataType
                                                .SITE_DATA,
                                    },
                                    TimePeriod.LAST_WEEK);
                });

        Assert.assertTrue(WebappRegistry.getRegisteredWebappIdsForTesting().isEmpty());
    }

    /**
     * Tests that web app scopes and last launch times are cleared when the "history" option is
     * selected. However, the web app is not removed from the registry.
     */
    @Test
    @MediumTest
    public void testClearingHistoryClearsWebappScopesAndLaunchTimes() throws Exception {
        BrowserServicesIntentDataProvider intentDataProvider =
                WebappTestHelper.createIntentDataProvider("id", "url");
        TestFetchStorageCallback callback = new TestFetchStorageCallback();
        WebappRegistry.getInstance().register("first", callback);
        callback.waitForCallback(0);
        callback.getStorage().updateFromWebappIntentDataProvider(intentDataProvider);

        Assert.assertEquals(
                new HashSet<>(Arrays.asList("first")),
                WebappRegistry.getRegisteredWebappIdsForTesting());

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    getBrowsingDataBridge()
                            .clearBrowsingData(
                                    mListener,
                                    new int[] {
                                        org.chromium.chrome.browser.browsing_data.BrowsingDataType
                                                .HISTORY,
                                    },
                                    TimePeriod.LAST_WEEK);
                });
        Assert.assertEquals(
                new HashSet<>(Arrays.asList("first")),
                WebappRegistry.getRegisteredWebappIdsForTesting());

        // URL and scope should be empty, and last used time should be 0.
        WebappDataStorage storage = WebappRegistry.getInstance().getWebappDataStorage("first");
        Assert.assertEquals("", storage.getScope());
        Assert.assertEquals("", storage.getUrl());
        Assert.assertEquals(0, storage.getLastUsedTimeMs());
    }

    /**
     * Tests that the HaTS survey is triggered once on the next page load on any window when
     * requested.
     */
    @Test
    @MediumTest
    @DisableIf.Build(sdk_is_greater_than = Build.VERSION_CODES.R) // https://crbug.com/1297370
    @CommandLineFlags.Add(ChromeSwitches.DISABLE_TAB_MERGING_FOR_TESTING)
    public void testHatsSurveyTriggeredOnNextPageLoad() {
        BrowsingDataBridgeJni.setInstanceForTesting(mBrowsingDataBridgeJniMock);
        doNothing().when(mBrowsingDataBridgeJniMock).triggerHatsSurvey(any(), any(), anyBoolean());

        final String url = mTestServer.getURL(TEST_FILE_PATH_1);

        final ChromeTabbedActivity firstActivity = mActivityTestRule.getActivity();
        final ChromeTabbedActivity secondActivity =
                MultiWindowTestHelper.createSecondChromeTabbedActivity(firstActivity);

        // Wait for the second window to be fully initialized.
        CriteriaHelper.pollUiThread(
                () -> secondActivity.getTabModelSelector().isTabStateInitialized());

        // Request the survey and start the observers.
        ThreadUtils.runOnUiThreadBlocking(() -> getBrowsingDataBridge().requestHatsSurvey(false));

        // Create a new tab in the first activity's TabModel and load a URL.
        ChromeTabUtils.fullyLoadUrlInNewTab(
                InstrumentationRegistry.getInstrumentation(),
                firstActivity,
                url,
                /* incognito= */ false);

        // Survey should be triggered on the first activity.
        WebContents firstWebContents = firstActivity.getCurrentWebContents();
        verify(mBrowsingDataBridgeJniMock, times(1))
                .triggerHatsSurvey(any(), eq(firstWebContents), eq(false));

        // Create a new tab in the second activity's TabModel and load a URL.
        ChromeTabUtils.fullyLoadUrlInNewTab(
                InstrumentationRegistry.getInstrumentation(),
                secondActivity,
                url,
                /* incognito= */ false);

        // No new Survey should be triggered on the second activity.
        verify(mBrowsingDataBridgeJniMock, times(1)).triggerHatsSurvey(any(), any(), anyBoolean());
    }

    private List<String> getUrls(NavigationController controller) {
        List<String> urls = new ArrayList<>();
        int i = 0;
        while (true) {
            NavigationEntry entry = controller.getEntryAtIndex(i++);
            if (entry == null) return urls;
            urls.add(entry.getUrl().getSpec());
        }
    }
}
