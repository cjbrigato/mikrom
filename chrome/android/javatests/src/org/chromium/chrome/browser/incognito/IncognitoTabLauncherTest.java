// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.incognito;

import static org.mockito.Mockito.any;
import static org.mockito.Mockito.doReturn;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;

import androidx.browser.customtabs.CustomTabsIntent;
import androidx.browser.customtabs.CustomTabsSession;
import androidx.core.app.BundleCompat;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.filters.MediumTest;
import androidx.test.filters.SmallTest;

import org.junit.Assert;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.DisabledTest;
import org.chromium.base.test.util.Feature;
import org.chromium.base.test.util.Restriction;
import org.chromium.chrome.browser.ChromeTabbedActivity;
import org.chromium.chrome.browser.customtabs.CustomTabsConnection;
import org.chromium.chrome.browser.customtabs.CustomTabsTestUtils;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.ChromeTabbedActivityTestRule;
import org.chromium.chrome.test.transit.ChromeTransitTestRules;
import org.chromium.chrome.test.transit.FreshCtaTransitTestRule;
import org.chromium.components.externalauth.ExternalAuthUtils;
import org.chromium.ui.test.util.DeviceRestriction;

import java.util.concurrent.TimeoutException;

/** Tests for {@link IncognitoTabLauncher}. */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
public class IncognitoTabLauncherTest {
    @Rule
    public final FreshCtaTransitTestRule mActivityRule =
            ChromeTransitTestRules.freshChromeTabbedActivityRule();

    @Test
    @Feature("Incognito")
    @SmallTest
    public void testEnableComponent() throws TimeoutException {
        Context context = ApplicationProvider.getApplicationContext();
        IncognitoTabLauncher.setComponentEnabled(true);
        Assert.assertNotNull(
                context.getPackageManager().resolveActivity(createLaunchIntent(context), 0));
    }

    @Test
    @Feature("Incognito")
    @SmallTest
    public void testDisableComponent() throws TimeoutException {
        Context context = ApplicationProvider.getApplicationContext();
        IncognitoTabLauncher.setComponentEnabled(false);
        Assert.assertNull(
                context.getPackageManager().resolveActivity(createLaunchIntent(context), 0));
    }

    @Test
    @Feature("Incognito")
    @MediumTest
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testLaunchIncognitoNewTab() throws TimeoutException {
        ChromeTabbedActivity activity = launchIncognitoTab(false);
        assertIncognitoTabLaunched(activity, false);
    }

    @Test
    @Feature("Incognito")
    @MediumTest
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testLaunchIncognitoNewTab_omniboxFocused_enabled_thirdParty()
            throws TimeoutException {
        ChromeTabbedActivity activity = launchIncognitoTab(false);
        assertIncognitoTabLaunched(activity, false);
    }

    @Test
    @Feature("Incognito")
    @MediumTest
    @DisabledTest(message = "crbug.com/1237504")
    public void testLaunchIncognitoNewTab_omniboxFocused_enabled_firstParty()
            throws TimeoutException {
        ChromeTabbedActivity activity = launchIncognitoTab(true);
        assertIncognitoTabLaunched(activity, true);
    }

    private ChromeTabbedActivity launchIncognitoTab(boolean asFirstParty) throws TimeoutException {
        ExternalAuthUtils spy = Mockito.spy(ExternalAuthUtils.getInstance());
        doReturn(asFirstParty).when(spy).isGoogleSigned(any());
        ExternalAuthUtils.setInstanceForTesting(spy);

        Context context = ApplicationProvider.getApplicationContext();
        IncognitoTabLauncher.setComponentEnabled(true);
        Intent intent = createLaunchIntent(context);

        // We need FLAG_ACTIVITY_NEW_TASK because we're calling from the application context (not an
        // Activity context). This is fine though because ChromeActivityTestRule.waitFor uses
        // ApplicationStatus internally, which ignores Tasks and tracks all Chrome Activities.
        intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);

        ThreadUtils.runOnUiThreadBlocking(() -> context.startActivity(intent));
        return ChromeTabbedActivityTestRule.waitFor(ChromeTabbedActivity.class);
    }

    private Intent createLaunchIntent(Context context) throws TimeoutException {
        // To emulate first party we create a CustomTabIntent with an associated
        // session token. Then, we create a normal intent and copy the session token
        // from CustomTabIntent to the normal intent.
        CustomTabsConnection connection = CustomTabsTestUtils.setUpConnection();
        CustomTabsSession session = CustomTabsTestUtils.bindWithCallback(null).session;

        CustomTabsIntent custom_tab_intent = new CustomTabsIntent.Builder(session).build();

        // Restrict ourselves to Chrome's package, on the off chance the testing device has
        // another application that answers to the ACTION_LAUNCH_NEW_INCOGNITO_TAB action.
        Intent intent = new Intent(IncognitoTabLauncher.ACTION_LAUNCH_NEW_INCOGNITO_TAB);
        intent.setPackage(context.getPackageName());

        Bundle extras = new Bundle();
        BundleCompat.putBinder(
                extras,
                CustomTabsIntent.EXTRA_SESSION,
                custom_tab_intent.intent.getExtras().getBinder(CustomTabsIntent.EXTRA_SESSION));

        intent.putExtras(extras);
        return intent;
    }

    private void assertIncognitoTabLaunched(
            ChromeTabbedActivity activity, boolean isUrlBarFocused) {
        Assert.assertTrue(activity.getTabModelSelector().isIncognitoSelected());
        Assert.assertTrue(IncognitoTabLauncher.didCreateIntent(activity.getIntent()));
        Assert.assertEquals(isUrlBarFocused, activity.getToolbarManager().isUrlBarFocused());
    }
}
