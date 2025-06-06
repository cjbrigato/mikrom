// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.webapps;

import android.app.Instrumentation.ActivityMonitor;
import android.app.Notification;
import android.app.NotificationManager;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.service.notification.StatusBarNotification;

import androidx.annotation.Nullable;
import androidx.test.filters.SmallTest;
import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.ContextUtils;
import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.CriteriaHelper;
import org.chromium.base.test.util.DisabledTest;
import org.chromium.base.test.util.Feature;
import org.chromium.chrome.browser.browserservices.intents.WebappConstants;
import org.chromium.chrome.browser.customtabs.CustomTabsTestUtils;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.notifications.NotificationConstants;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.R;
import org.chromium.net.test.EmbeddedTestServer;

/**
 * Tests for a standalone Web App notification governed by {@link WebappActionsNotificationManager}.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
public class WebappActionsNotificationTest {
    private static final String WEB_APP_PATH = "/chrome/test/data/banners/manifest_test_page.html";

    @Rule public final WebappActivityTestRule mActivityTestRule = new WebappActivityTestRule();

    private static class TestContext extends ContextWrapper {
        public TestContext(Context baseContext) {
            super(baseContext);
        }

        @Override
        public PackageManager getPackageManager() {
            return CustomTabsTestUtils.getDefaultBrowserOverridingPackageManager(
                    getPackageName(), super.getPackageManager());
        }
    }

    private EmbeddedTestServer mTestServer;

    @Before
    public void startWebapp() {
        Context appContext = ContextUtils.getApplicationContext();
        TestContext testContext = new TestContext(appContext);
        ContextUtils.initApplicationContextForTests(testContext);
        mTestServer = EmbeddedTestServer.createAndStartServer(testContext);
        mActivityTestRule.startWebappActivity(
                mActivityTestRule
                        .createIntent()
                        .putExtra(WebappConstants.EXTRA_URL, mTestServer.getURL(WEB_APP_PATH)));
    }

    @Test
    @SmallTest
    @Feature({"Webapps"})
    @DisabledTest(message = "https://crbug.com/411465437")
    public void testNotification_openInChrome() throws Exception {
        Notification notification = getWebappNotification();

        Assert.assertNotNull(notification);
        Assert.assertEquals(
                "webapp short name", notification.extras.getString(Notification.EXTRA_TITLE));
        Assert.assertEquals(
                mActivityTestRule.getActivity().getString(R.string.webapp_tap_to_copy_url),
                notification.extras.getString(Notification.EXTRA_TEXT));
        Assert.assertEquals("Share", notification.actions[0].title);
        Assert.assertEquals("Open in Chrome browser", notification.actions[1].title);

        IntentFilter filter = new IntentFilter(Intent.ACTION_VIEW);
        filter.addDataScheme("http");
        final ActivityMonitor monitor =
                InstrumentationRegistry.getInstrumentation().addMonitor(filter, null, false);

        notification.actions[1].actionIntent.send();
        CriteriaHelper.pollInstrumentationThread(
                () -> {
                    return InstrumentationRegistry.getInstrumentation().checkMonitorHit(monitor, 1);
                });

        Assert.assertNull("Notification should no longer be shown", getWebappNotification());
    }

    @Test
    @SmallTest
    @Feature({"Webapps"})
    @DisabledTest(message = "crbug.com/774491")
    public void testNotification_copyUrl() throws Exception {
        Notification notification = getWebappNotification();
        Assert.assertNotNull(notification);

        notification.contentIntent.send();

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    ClipboardManager clipboard =
                            (ClipboardManager)
                                    mActivityTestRule
                                            .getActivity()
                                            .getSystemService(Context.CLIPBOARD_SERVICE);
                    Assert.assertEquals(
                            mActivityTestRule.getTestServer().getURL(WEB_APP_PATH),
                            clipboard.getPrimaryClip().getItemAt(0).getText().toString());
                });
    }

    private @Nullable Notification getWebappNotification() {
        NotificationManager nm =
                (NotificationManager)
                        mActivityTestRule
                                .getActivity()
                                .getSystemService(Context.NOTIFICATION_SERVICE);
        for (StatusBarNotification sbn : nm.getActiveNotifications()) {
            if (sbn.getId() == NotificationConstants.NOTIFICATION_ID_WEBAPP_ACTIONS) {
                return sbn.getNotification();
            }
        }
        return null;
    }
}
