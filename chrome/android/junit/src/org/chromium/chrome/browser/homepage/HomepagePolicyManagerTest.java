// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.homepage;

import androidx.annotation.Nullable;
import androidx.test.filters.SmallTest;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.annotation.Config;

import org.chromium.base.shared_preferences.SharedPreferencesManager;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.base.test.util.Features;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.homepage.HomepagePolicyManager.HomepagePolicyStateListener;
import org.chromium.chrome.browser.init.ChromeBrowserInitializer;
import org.chromium.chrome.browser.preferences.ChromePreferenceKeys;
import org.chromium.chrome.browser.preferences.ChromeSharedPreferences;
import org.chromium.chrome.browser.preferences.Pref;
import org.chromium.components.prefs.PrefChangeRegistrar;
import org.chromium.components.prefs.PrefService;
import org.chromium.url.GURL;
import org.chromium.url.JUnitTestGURLs;

/** Tests for the {@link HomepagePolicyManager}. */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
@Features.EnableFeatures({
    ChromeFeatureList.SHOW_HOME_BUTTON_POLICY_ANDROID,
    ChromeFeatureList.HOMEPAGE_IS_NEW_TAB_PAGE_POLICY_ANDROID
})
public class HomepagePolicyManagerTest {
    public static final String TEST_URL = JUnitTestGURLs.EXAMPLE_URL.getSpec();
    public static final String CHROME_NTP = JUnitTestGURLs.NTP_URL.getSpec();

    @Rule public final MockitoRule mMockitoRule = MockitoJUnit.rule();
    private HomepagePolicyManager mHomepagePolicyManager;

    @Mock private PrefService mMockPrefService;
    @Mock private PrefChangeRegistrar mMockRegistrar;
    @Mock private ChromeBrowserInitializer mChromeBrowserInitializer;

    @Mock private HomepagePolicyStateListener mListener;

    private SharedPreferencesManager mSharedPreferenceManager;

    @Before
    public void setup() {

        // Reset shared preference
        mSharedPreferenceManager = ChromeSharedPreferences.getInstance();
        setHomepageInSharedPreference(GURL.emptyGURL());

        ChromeBrowserInitializer.setForTesting(mChromeBrowserInitializer);

        // Disable the policy during setup
        HomepagePolicyManager.setPrefServiceForTesting(mMockPrefService);
        setupNewHomepagePolicyManagerForTests(
                /* isHomeLocationPolicyManaged= */ false,
                /* homepageLocation= */ "",
                /* isButtonPolicyManaged= */ false,
                /* buttonPolicyValue= */ false,
                /* homepageIsNtpPolicyManaged= */ false,
                /* homepageIsNtpPolicyValue= */ false,
                null);

        // Verify setup
        Assert.assertFalse(
                "#isHomepageLocationManaged == true without homepage pref setup",
                mHomepagePolicyManager.isHomepageLocationPolicyManaged());
        Assert.assertFalse(
                "#isShowHomeButtonPolicyManaged == true without pref setup",
                mHomepagePolicyManager.isShowHomeButtonPolicyManaged());
        Assert.assertFalse(
                "isHomepageNewTabPageManaged == true without pref setup",
                mHomepagePolicyManager.isHomepageIsNtpPolicyManaged());
    }

    private void setupNewHomepagePolicyManagerForTests(
            boolean isHomeLocationPolicyManaged,
            String homepageLocation,
            boolean isButtonPolicyManaged,
            boolean buttonPolicyValue,
            boolean homepageIsNtpPolicyManaged,
            boolean homepageIsNtpPolicyValue,
            @Nullable HomepagePolicyStateListener listener) {
        Mockito.when(mMockPrefService.isManagedPreference(Pref.HOME_PAGE))
                .thenReturn(isHomeLocationPolicyManaged);
        Mockito.when(mMockPrefService.getString(Pref.HOME_PAGE)).thenReturn(homepageLocation);

        Mockito.when(mMockPrefService.isManagedPreference(Pref.SHOW_HOME_BUTTON))
                .thenReturn(isButtonPolicyManaged);
        Mockito.when(mMockPrefService.getBoolean(Pref.SHOW_HOME_BUTTON))
                .thenReturn(buttonPolicyValue);

        Mockito.when(mMockPrefService.isManagedPreference(Pref.HOME_PAGE_IS_NEW_TAB_PAGE))
                .thenReturn(homepageIsNtpPolicyManaged);
        Mockito.when(mMockPrefService.getBoolean(Pref.HOME_PAGE_IS_NEW_TAB_PAGE))
                .thenReturn(homepageIsNtpPolicyValue);

        mHomepagePolicyManager = new HomepagePolicyManager(mMockRegistrar, listener);
        HomepagePolicyManager.setInstanceForTests(mHomepagePolicyManager);
    }

    private void setHomepageInSharedPreference(GURL homepageLocation) {
        mSharedPreferenceManager.writeString(
                ChromePreferenceKeys.HOMEPAGE_LOCATION_POLICY_GURL, homepageLocation.serialize());
    }

    @Test
    @SmallTest
    public void testEmptyInstance_GetFromSharedPreference() {
        // Create a new empty instance
        HomepagePolicyManager manager = new HomepagePolicyManager();

        // Test if policy reflects the setting of shared preference
        Assert.assertFalse(
                "HomepagePolicyManager should be not initialized yet", manager.isInitialized());
        Assert.assertFalse(
                "#isHomepageLocationPolicyManaged not consistent with test setting",
                manager.isHomepageLocationPolicyManaged());
        Assert.assertFalse(
                "#isShowHomeButtonPolicyManaged not consistent with test setting",
                manager.isShowHomeButtonPolicyManaged());
        Assert.assertFalse(
                "#isHomepageIsNtpPolicyManaged not consistent with test setting",
                manager.isHomepageIsNtpPolicyManaged());
    }

    @Test
    @SmallTest
    public void testEmptyInstance_EnabledFromSharedPreference() {
        setHomepageInSharedPreference(new GURL(CHROME_NTP));

        // Create a new empty instance
        HomepagePolicyManager manager = new HomepagePolicyManager();

        Assert.assertFalse(
                "HomepagePolicyManager should be not initialized yet", manager.isInitialized());
        Assert.assertTrue(
                "#isHomepageLocationPolicyManaged not consistent with test setting",
                manager.isHomepageLocationPolicyManaged());
        Assert.assertEquals(
                "#getHomepageUrl not consistent with test setting",
                CHROME_NTP,
                manager.getHomepageLocationPolicyUrl().getSpec());
    }

    @Test
    @SmallTest
    public void testEmptyInstance_InitializeAfterwards() {
        // Create a new empty instance
        HomepagePolicyManager manager = new HomepagePolicyManager();

        manager.initializeWithNative(mMockRegistrar);
        Assert.assertTrue("HomepagePolicyManager should be initialized", manager.isInitialized());
        Mockito.verify(mMockRegistrar, Mockito.times(1)).addObserver(Pref.HOME_PAGE, manager);
        Mockito.verify(mMockRegistrar, Mockito.times(1))
                .addObserver(Pref.SHOW_HOME_BUTTON, manager);
        Mockito.verify(mMockRegistrar, Mockito.times(1))
                .addObserver(Pref.HOME_PAGE_IS_NEW_TAB_PAGE, manager);
    }

    @Test
    @SmallTest
    public void testInitialization() {
        setupNewHomepagePolicyManagerForTests(
                /* isHomeLocationPolicyManaged= */ true,
                /* homepageLocation= */ TEST_URL,
                /* isButtonPolicyManaged= */ true,
                /* buttonPolicyValue= */ false,
                /* homepageIsNtpPolicyManaged= */ true,
                /* homepageIsNtpPolicyValue= */ false,
                null);

        Assert.assertTrue(
                "#isHomepageLocationManaged not consistent with test setting",
                HomepagePolicyManager.isHomepageLocationManaged());
        Assert.assertEquals(
                "#getHomepageUrl not consistent with test setting",
                TEST_URL,
                HomepagePolicyManager.getHomepageUrl().getSpec());

        String homepageGurlSerialized =
                mSharedPreferenceManager.readString(
                        ChromePreferenceKeys.HOMEPAGE_LOCATION_POLICY_GURL, "");
        GURL homepageGurl = GURL.deserialize(homepageGurlSerialized);
        Assert.assertEquals(
                "Updated HomepageLocation should be stored in shared preference",
                TEST_URL,
                homepageGurl.getSpec());

        Assert.assertTrue(
                "#ShowHomeButton policy is disabled but should be enabled",
                HomepagePolicyManager.isShowHomeButtonManaged());
        Assert.assertFalse(
                "#ShowHomeButton policy has wrong value in test",
                HomepagePolicyManager.getShowHomeButtonValue());

        Assert.assertTrue(
                "#HomepageIsNTP policy is disabled but should be enabled",
                HomepagePolicyManager.isHomepageNewTabPageManaged());
        Assert.assertFalse(
                "#HomepageIsNTP policy has wrong value in test",
                HomepagePolicyManager.getHomepageNewTabPageValue());
    }

    @Test
    @SmallTest
    public void testInitialization_NTP() {
        setupNewHomepagePolicyManagerForTests(
                /* isHomeLocationPolicyManaged= */ true,
                /* homepageLocation= */ CHROME_NTP,
                /* isButtonPolicyManaged= */ false,
                /* buttonPolicyValue= */ false,
                /* homepageIsNtpPolicyManaged= */ false,
                /* homepageIsNtpPolicyValue= */ false,
                null);

        Assert.assertTrue(
                "#isHomepageLocationManaged not consistent with test setting",
                HomepagePolicyManager.isHomepageLocationManaged());
        Assert.assertEquals(
                "#getHomepageUrl not consistent with test setting",
                CHROME_NTP,
                HomepagePolicyManager.getHomepageUrl().getSpec());

        String homepageGurlSerialized =
                mSharedPreferenceManager.readString(
                        ChromePreferenceKeys.HOMEPAGE_LOCATION_POLICY_GURL, "");
        GURL homepageGurl = GURL.deserialize(homepageGurlSerialized);
        Assert.assertEquals(
                "Updated HomepageLocation should be stored in shared preference",
                CHROME_NTP,
                homepageGurl.getSpec());
    }

    @Test
    @SmallTest
    public void testDestroy() {
        HomepagePolicyManager.destroy();
        Mockito.verify(mMockRegistrar).destroy();
        Assert.assertTrue(
                "Listeners are not remove completely",
                mHomepagePolicyManager.getListenersForTesting().isEmpty());
    }

    @Test
    @SmallTest
    public void testPrefRefreshToEnablePolicy() {
        Assert.assertFalse(
                "Policy should be disabled after set up",
                mHomepagePolicyManager.isHomepageLocationPolicyManaged());

        // Add listener
        mHomepagePolicyManager.addListener(mListener);

        // A new policy URL is set, which triggers the refresh of native manager.
        final String newUrl = JUnitTestGURLs.URL_1.getSpec();
        Mockito.when(mMockPrefService.isManagedPreference(Pref.HOME_PAGE)).thenReturn(true);
        Mockito.when(mMockPrefService.getString(Pref.HOME_PAGE)).thenReturn(newUrl);

        // Update the preference, so that the policy will be enabled.
        mHomepagePolicyManager.onPreferenceChange();

        // The homepage retrieved from homepage manager should be in sync with pref setting
        Assert.assertTrue(
                "Policy should be enabled after refresh",
                mHomepagePolicyManager.isHomepageLocationPolicyManaged());
        Assert.assertEquals(
                "#getHomepageUrl not consistent with test setting",
                newUrl,
                mHomepagePolicyManager.getHomepageLocationPolicyUrl().getSpec());
        Mockito.verify(mListener, Mockito.times(1)).onHomepagePolicyUpdate();

        // Change ShowHomeButton policy same as above.
        Assert.assertFalse(
                "Policy should be disabled after set up",
                mHomepagePolicyManager.isShowHomeButtonPolicyManaged());

        Mockito.when(mMockPrefService.isManagedPreference(Pref.SHOW_HOME_BUTTON)).thenReturn(true);
        Mockito.when(mMockPrefService.getBoolean(Pref.SHOW_HOME_BUTTON)).thenReturn(true);

        mHomepagePolicyManager.onPreferenceChange();

        Assert.assertTrue(
                "#ShowHomeButton policy is disabled but should be enabled",
                mHomepagePolicyManager.isShowHomeButtonPolicyManaged());
        Assert.assertTrue(
                "#ShowHomeButton policy has wrong value in test",
                mHomepagePolicyManager.getShowHomeButtonPolicyValue());
        Mockito.verify(mListener, Mockito.times(2)).onHomepagePolicyUpdate();

        // Change HomepageIsNTP policy same as above.
        Assert.assertFalse(
                "Policy should be disabled after set up",
                mHomepagePolicyManager.isHomepageIsNtpPolicyManaged());

        Mockito.when(mMockPrefService.isManagedPreference(Pref.HOME_PAGE_IS_NEW_TAB_PAGE))
                .thenReturn(true);
        Mockito.when(mMockPrefService.getBoolean(Pref.HOME_PAGE_IS_NEW_TAB_PAGE)).thenReturn(true);

        mHomepagePolicyManager.onPreferenceChange();

        Assert.assertTrue(
                "#HomepageIsNTP policy is disabled but should be enabled",
                mHomepagePolicyManager.isHomepageIsNtpPolicyManaged());
        Assert.assertTrue(
                "#HomepageIsNTP policy has wrong value in test",
                mHomepagePolicyManager.getHomepageIsNtpPolicyValue());
        Mockito.verify(mListener, Mockito.times(3)).onHomepagePolicyUpdate();
    }

    @Test
    @SmallTest
    public void testPrefRefreshToDisablePolicy() {
        // Set a new HomepagePolicyManager with policy enabled.
        setupNewHomepagePolicyManagerForTests(
                /* isHomeLocationPolicyManaged= */ true,
                /* homepageLocation= */ TEST_URL,
                /* isButtonPolicyManaged= */ true,
                /* buttonPolicyValue= */ true,
                /* homepageIsNtpPolicyManaged= */ true,
                /* homepageIsNtpPolicyValue= */ true,
                null);
        mHomepagePolicyManager.addListener(mListener);

        // The verify policyEnabled
        Assert.assertTrue(
                "Policy should be enabled after set up",
                mHomepagePolicyManager.isHomepageLocationPolicyManaged());

        // Update the preference, so that the policy will be disabled.
        Mockito.when(mMockPrefService.isManagedPreference(Pref.HOME_PAGE)).thenReturn(false);
        Mockito.when(mMockPrefService.getString(Pref.HOME_PAGE)).thenReturn("");
        mHomepagePolicyManager.onPreferenceChange();

        // The homepage retrieved from homepage manager should be in sync with pref setting.
        Assert.assertFalse(
                "Policy should be disabled after refresh",
                mHomepagePolicyManager.isHomepageLocationPolicyManaged());
        Mockito.verify(mListener, Mockito.times(1)).onHomepagePolicyUpdate();

        // Same as above for ShowHomeButton policy.
        Assert.assertTrue(
                "#ShowHomeButton policy is disabled but should be enabled",
                mHomepagePolicyManager.isShowHomeButtonPolicyManaged());
        Mockito.when(mMockPrefService.isManagedPreference(Pref.SHOW_HOME_BUTTON)).thenReturn(false);
        mHomepagePolicyManager.onPreferenceChange();
        Assert.assertFalse(
                "#ShowHomeButton policy is enabled but should be disabled",
                mHomepagePolicyManager.isShowHomeButtonPolicyManaged());
        Mockito.verify(mListener, Mockito.times(2)).onHomepagePolicyUpdate();

        // Same as above for HomepageIsNTP policy.
        Assert.assertTrue(
                "#HomepageIsNTP policy is disabled but should be enabled",
                mHomepagePolicyManager.isHomepageIsNtpPolicyManaged());
        Mockito.when(mMockPrefService.isManagedPreference(Pref.HOME_PAGE_IS_NEW_TAB_PAGE))
                .thenReturn(false);
        mHomepagePolicyManager.onPreferenceChange();
        Assert.assertFalse(
                "#HomepageIsNTP policy is enabled but should be disabled",
                mHomepagePolicyManager.isHomepageIsNtpPolicyManaged());
        Mockito.verify(mListener, Mockito.times(3)).onHomepagePolicyUpdate();
    }

    @Test
    @SmallTest
    public void testPrefRefreshWithoutChanges() {
        // Set a new HomepagePolicyManager with policy enabled.
        setupNewHomepagePolicyManagerForTests(
                /* isHomeLocationPolicyManaged= */ true,
                /* homepageLocation= */ TEST_URL,
                /* isButtonPolicyManaged= */ true,
                /* buttonPolicyValue= */ true,
                /* homepageIsNtpPolicyManaged= */ true,
                /* homepageIsNtpPolicyValue= */ true,
                null);

        // The verify policyEnabled
        Assert.assertTrue(
                "Policy should be enabled after set up",
                mHomepagePolicyManager.isHomepageLocationPolicyManaged());
        Assert.assertTrue(
                "#ShowHomeButton policy is disabled but should be enabled",
                mHomepagePolicyManager.isShowHomeButtonPolicyManaged());
        Assert.assertTrue(
                "#HomepageIsNTP policy is disabled but should be enabled",
                mHomepagePolicyManager.isHomepageIsNtpPolicyManaged());

        // Perform an debounce - creating a new homepage manager with same setting, and add the
        // listener.
        setupNewHomepagePolicyManagerForTests(
                /* isHomeLocationPolicyManaged= */ true,
                /* homepageLocation= */ TEST_URL,
                /* isButtonPolicyManaged= */ true,
                /* buttonPolicyValue= */ true,
                /* homepageIsNtpPolicyManaged= */ true,
                /* homepageIsNtpPolicyValue= */ true,
                mListener);

        // Verify listeners should not receive updates.
        Assert.assertTrue(
                "Policy should be enabled after refresh",
                mHomepagePolicyManager.isHomepageLocationPolicyManaged());
        Assert.assertEquals(
                "#getHomepageUrl not consistent with test setting",
                TEST_URL,
                mHomepagePolicyManager.getHomepageLocationPolicyUrl().getSpec());
        Assert.assertTrue(
                "#ShowHomeButton policy is disabled but should be enabled",
                mHomepagePolicyManager.isShowHomeButtonPolicyManaged());
        Assert.assertTrue(
                "#ShowHomeButton policy has wrong value in test",
                mHomepagePolicyManager.getShowHomeButtonPolicyValue());
        Assert.assertTrue(
                "#HomepageIsNTP policy is disabled but should be enabled",
                mHomepagePolicyManager.isHomepageIsNtpPolicyManaged());
        Assert.assertTrue(
                "#HomepageIsNTP policy has wrong value in test",
                mHomepagePolicyManager.getHomepageIsNtpPolicyValue());

        // However, because the native setting is consistent with cached value in SharedPreference,
        // listeners will not receive update.
        Mockito.verify(mListener, Mockito.never()).onHomepagePolicyUpdate();
    }

    @Test(expected = AssertionError.class)
    @SmallTest
    public void testIllegal_GetHomepageUrl() {
        setupNewHomepagePolicyManagerForTests(
                /* isHomeLocationPolicyManaged= */ false,
                /* homepageLocation= */ "",
                /* isButtonPolicyManaged= */ false,
                /* buttonPolicyValue= */ false,
                /* homepageIsNtpPolicyManaged= */ false,
                /* homepageIsNtpPolicyValue= */ false,
                null);
        HomepagePolicyManager.getHomepageUrl();
    }

    @Test
    @SmallTest
    public void testGurlPreferenceKeysMigrationInConstructor() {
        ChromeSharedPreferences.getInstance()
                .writeString(ChromePreferenceKeys.DEPRECATED_HOMEPAGE_LOCATION_POLICY, null);
        ChromeSharedPreferences.getInstance()
                .writeString(ChromePreferenceKeys.HOMEPAGE_LOCATION_POLICY_GURL, null);
        mHomepagePolicyManager = new HomepagePolicyManager();
        Assert.assertFalse(mHomepagePolicyManager.isHomepageLocationPolicyManaged());

        final String url1 = JUnitTestGURLs.URL_1.getSpec();
        final String url2 = JUnitTestGURLs.URL_2.getSpec();
        ChromeSharedPreferences.getInstance()
                .writeString(ChromePreferenceKeys.DEPRECATED_HOMEPAGE_LOCATION_POLICY, url1);
        ChromeSharedPreferences.getInstance()
                .writeString(ChromePreferenceKeys.HOMEPAGE_LOCATION_POLICY_GURL, null);
        mHomepagePolicyManager = new HomepagePolicyManager();
        Assert.assertEquals(url1, mHomepagePolicyManager.getHomepageLocationPolicyUrl().getSpec());

        ChromeSharedPreferences.getInstance()
                .writeString(ChromePreferenceKeys.DEPRECATED_HOMEPAGE_LOCATION_POLICY, null);
        ChromeSharedPreferences.getInstance()
                .writeString(
                        ChromePreferenceKeys.HOMEPAGE_LOCATION_POLICY_GURL,
                        new GURL(url1).serialize());
        mHomepagePolicyManager = new HomepagePolicyManager();
        Assert.assertEquals(url1, mHomepagePolicyManager.getHomepageLocationPolicyUrl().getSpec());

        ChromeSharedPreferences.getInstance()
                .writeString(ChromePreferenceKeys.DEPRECATED_HOMEPAGE_LOCATION_POLICY, url1);
        ChromeSharedPreferences.getInstance()
                .writeString(
                        ChromePreferenceKeys.HOMEPAGE_LOCATION_POLICY_GURL,
                        new GURL(url2).serialize());
        mHomepagePolicyManager = new HomepagePolicyManager();
        Assert.assertEquals(url2, mHomepagePolicyManager.getHomepageLocationPolicyUrl().getSpec());
    }
}
