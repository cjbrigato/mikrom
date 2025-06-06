// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.signin;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;

import androidx.test.filters.MediumTest;

import org.junit.Assert;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.mockito.quality.Strictness;

import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.CriteriaHelper;
import org.chromium.base.test.util.UserActionTester;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.transit.ChromeTransitTestRules;
import org.chromium.chrome.test.transit.FreshCtaTransitTestRule;
import org.chromium.chrome.test.util.browser.signin.SigninTestRule;
import org.chromium.chrome.test.util.browser.sync.SyncTestUtil;
import org.chromium.components.externalauth.ExternalAuthUtils;
import org.chromium.components.signin.identitymanager.ConsentLevel;
import org.chromium.components.signin.test.util.TestAccounts;

/**
 * This class tests the sign-in checks done at Chrome start-up or when accounts change on device.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
public class SigninCheckerTest {
    @Rule
    public final MockitoRule mMockitoRule = MockitoJUnit.rule().strictness(Strictness.STRICT_STUBS);

    @Rule public final SigninTestRule mSigninTestRule = new SigninTestRule();

    @Rule
    public final FreshCtaTransitTestRule mActivityTestRule =
            ChromeTransitTestRules.freshChromeTabbedActivityRule();

    @Mock private ExternalAuthUtils mExternalAuthUtilsMock;

    private void signinWhenChildAccountIsTheOnlyAccount() {
        mActivityTestRule.startOnBlankPage();

        mSigninTestRule.addAccount(TestAccounts.CHILD_ACCOUNT);

        CriteriaHelper.pollUiThread(
                () -> {
                    return TestAccounts.CHILD_ACCOUNT.equals(
                            mSigninTestRule.getPrimaryAccount(ConsentLevel.SIGNIN));
                });
        Assert.assertEquals(
                2,
                SigninCheckerProvider.get(mActivityTestRule.getProfile(false))
                        .getNumOfChildAccountChecksDoneForTests());
        Assert.assertFalse(SyncTestUtil.isSyncFeatureEnabled());
    }

    @Test
    @MediumTest
    public void signinWhenChildAccountIsTheOnlyAccountWithCapabilities() {
        signinWhenChildAccountIsTheOnlyAccount();
    }

    private void noSigninWhenChildAccountIsTheOnlyAccountButSigninIsNotAllowed() {
        mActivityTestRule.startOnBlankPage();
        UserActionTester actionTester = new UserActionTester();
        when(mExternalAuthUtilsMock.isGooglePlayServicesMissing(any())).thenReturn(true);
        ExternalAuthUtils.setInstanceForTesting(mExternalAuthUtilsMock);

        mSigninTestRule.addAccount(TestAccounts.CHILD_ACCOUNT);

        Assert.assertEquals(
                1,
                SigninCheckerProvider.get(mActivityTestRule.getProfile(false))
                        .getNumOfChildAccountChecksDoneForTests());
        Assert.assertNull(mSigninTestRule.getPrimaryAccount(ConsentLevel.SIGNIN));
        Assert.assertFalse(
                actionTester.getActions().contains("Signin_Signin_WipeDataOnChildAccountSignin2"));
    }

    @Test
    @MediumTest
    public void noSigninWhenChildAccountIsTheOnlyAccountButSigninIsNotAllowedWithCapabilities() {
        noSigninWhenChildAccountIsTheOnlyAccountButSigninIsNotAllowed();
    }

    private void noSigninWhenChildAccountIsTheSecondaryAccount() {
        // If a child account co-exists with another account on the device, then the child account
        // must be the first device (this is enforced by the Kids Module).  The behaviour in this
        // test case therefore is not currently hittable on a real device; however it is included
        // here for completeness.
        mSigninTestRule.addAccount("the.default.account@gmail.com");
        mSigninTestRule.addAccount(TestAccounts.CHILD_ACCOUNT);

        mActivityTestRule.startOnBlankPage();
        UserActionTester actionTester = new UserActionTester();

        Assert.assertEquals(
                0,
                SigninCheckerProvider.get(mActivityTestRule.getProfile(false))
                        .getNumOfChildAccountChecksDoneForTests());
        Assert.assertNull(mSigninTestRule.getPrimaryAccount(ConsentLevel.SIGNIN));
        Assert.assertFalse(
                actionTester.getActions().contains("Signin_Signin_WipeDataOnChildAccountSignin2"));
    }

    @Test
    @MediumTest
    public void noSigninWhenChildAccountIsTheSecondaryAccountWithCapabilities() {
        noSigninWhenChildAccountIsTheSecondaryAccount();
    }

    private void signinWhenChildAccountIsFirstAccount() {
        mActivityTestRule.startOnBlankPage();
        mSigninTestRule.addAccount(TestAccounts.CHILD_ACCOUNT);
        mSigninTestRule.addAccount("the.second.account@gmail.com");

        CriteriaHelper.pollUiThread(
                () -> {
                    return TestAccounts.CHILD_ACCOUNT.equals(
                            mSigninTestRule.getPrimaryAccount(ConsentLevel.SIGNIN));
                });

        // The check should be done twice at account addition and once during force sign-in.
        Assert.assertEquals(
                3,
                SigninCheckerProvider.get(mActivityTestRule.getProfile(false))
                        .getNumOfChildAccountChecksDoneForTests());
    }

    @Test
    @MediumTest
    public void signinWhenChildAccountIsFirstAccountWithCapabilities() {
        signinWhenChildAccountIsFirstAccount();
    }
}
