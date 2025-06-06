// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ui.device_lock;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import static org.chromium.chrome.browser.ui.device_lock.DeviceLockProperties.ALL_KEYS;
import static org.chromium.chrome.browser.ui.device_lock.DeviceLockProperties.DEVICE_SUPPORTS_PIN_CREATION_INTENT;
import static org.chromium.chrome.browser.ui.device_lock.DeviceLockProperties.ON_CREATE_DEVICE_LOCK_CLICKED;
import static org.chromium.chrome.browser.ui.device_lock.DeviceLockProperties.ON_DISMISS_CLICKED;
import static org.chromium.chrome.browser.ui.device_lock.DeviceLockProperties.ON_GO_TO_OS_SETTINGS_CLICKED;
import static org.chromium.chrome.browser.ui.device_lock.DeviceLockProperties.ON_USER_UNDERSTANDS_CLICKED;
import static org.chromium.chrome.browser.ui.device_lock.DeviceLockProperties.PREEXISTING_DEVICE_LOCK;
import static org.chromium.chrome.browser.ui.device_lock.DeviceLockProperties.SOURCE;
import static org.chromium.chrome.browser.ui.device_lock.DeviceLockProperties.UI_ENABLED;

import android.app.Activity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;

import androidx.test.annotation.UiThreadTest;
import androidx.test.filters.SmallTest;

import org.junit.After;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.ClassRule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.BaseActivityTestRule;
import org.chromium.base.test.util.Batch;
import org.chromium.base.test.util.DisabledTest;
import org.chromium.base.test.util.Features;
import org.chromium.chrome.R;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.components.browser_ui.device_lock.DeviceLockActivityLauncher;
import org.chromium.components.signin.SigninFeatures;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.modelutil.PropertyModelChangeProcessor;
import org.chromium.ui.test.util.BlankUiTestActivity;

import java.util.concurrent.atomic.AtomicBoolean;

/** Tests for {@link DeviceLockViewBinder}. */
@RunWith(ChromeJUnit4ClassRunner.class)
@Batch(Batch.UNIT_TESTS)
@Features.EnableFeatures(SigninFeatures.UNO_FOR_AUTO)
public class DeviceLockViewBinderTest {
    @ClassRule
    public static BaseActivityTestRule<BlankUiTestActivity> sActivityTestRule =
            new BaseActivityTestRule<>(BlankUiTestActivity.class);

    private static Activity sActivity;

    private final AtomicBoolean mCreateDeviceLockButtonClicked = new AtomicBoolean();
    private final AtomicBoolean mGoToOSSettingsButtonClicked = new AtomicBoolean();
    private final AtomicBoolean mUserUnderstandsButtonClicked = new AtomicBoolean();
    private final AtomicBoolean mDismissButtonClicked = new AtomicBoolean();

    private DeviceLockView mView;
    private PropertyModel mViewModel;
    private PropertyModelChangeProcessor mModelChangeProcessor;

    @BeforeClass
    public static void setupSuite() {
        sActivity = sActivityTestRule.launchActivity(null);
    }

    @Before
    public void setUp() {
        ViewGroup view = new LinearLayout(sActivity);

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    sActivity.setContentView(view);

                    mView = DeviceLockView.create(sActivity.getLayoutInflater());
                    view.addView(mView);

                    mViewModel =
                            new PropertyModel.Builder(ALL_KEYS)
                                    .with(PREEXISTING_DEVICE_LOCK, false)
                                    .with(DEVICE_SUPPORTS_PIN_CREATION_INTENT, true)
                                    .with(SOURCE, DeviceLockActivityLauncher.Source.AUTOFILL)
                                    .with(UI_ENABLED, true)
                                    .with(
                                            ON_CREATE_DEVICE_LOCK_CLICKED,
                                            v -> mCreateDeviceLockButtonClicked.set(true))
                                    .with(
                                            ON_GO_TO_OS_SETTINGS_CLICKED,
                                            v -> mGoToOSSettingsButtonClicked.set(true))
                                    .with(
                                            ON_USER_UNDERSTANDS_CLICKED,
                                            v -> mUserUnderstandsButtonClicked.set(true))
                                    .with(ON_DISMISS_CLICKED, v -> mDismissButtonClicked.set(true))
                                    .build();

                    mModelChangeProcessor =
                            PropertyModelChangeProcessor.create(
                                    mViewModel, mView, DeviceLockViewBinder::bind);
                });
    }

    @After
    public void tearDown() throws Exception {
        ThreadUtils.runOnUiThreadBlocking(mModelChangeProcessor::destroy);
    }

    @Test
    @UiThreadTest
    @SmallTest
    @DisabledTest(message = "crbug.com/347214230")
    public void testDeviceLockView_preExistingLock_showsAppropriateTexts() {
        mViewModel.set(PREEXISTING_DEVICE_LOCK, true);

        assertEquals(
                "The title text should match the version for a pre-existing device lock.",
                sActivity.getResources().getString(R.string.device_lock_existing_lock_title),
                mView.getTitle().getText());
        assertEquals(
                "The description text should match the version for a pre-existing device lock.",
                sActivity
                        .getResources()
                        .getString(R.string.device_lock_existing_lock_description_for_signin),
                mView.getDescription().getText());
        assertEquals(
                "The notice text should match the version for a pre-existing device lock..",
                sActivity.getResources().getString(R.string.device_lock_notice),
                mView.getNoticeText().getText());
        assertEquals(
                "The continue button text should match the version for a pre-existing device lock.",
                sActivity.getResources().getString(R.string.got_it),
                mView.getContinueButton().getText());
        assertEquals(
                "The continue button should always be visible.",
                View.VISIBLE,
                mView.getContinueButton().getVisibility());
        assertEquals(
                "The dismiss button should always be visible",
                View.VISIBLE,
                mView.getDismissButton().getVisibility());
    }

    @Test
    @UiThreadTest
    @SmallTest
    public void testDeviceLockView_noPreExistingLock_showsAppropriateTexts() {
        mViewModel.set(PREEXISTING_DEVICE_LOCK, false);

        assertEquals(
                "The title text should match the version for creating a device lock.",
                sActivity.getResources().getString(R.string.device_lock_title),
                mView.getTitle().getText());
        assertEquals(
                "The description text should match the version for creating a " + "device lock.",
                sActivity.getResources().getString(R.string.device_lock_description),
                mView.getDescription().getText());
        assertEquals(
                "The notice text should match the version for creating a device lock.",
                sActivity.getResources().getString(R.string.device_lock_creation_notice),
                mView.getNoticeText().getText());
        assertEquals(
                "The continue button should match the version for creating a device lock.",
                sActivity.getResources().getString(R.string.history_sync_primary_action),
                mView.getContinueButton().getText());
        assertEquals(
                "The continue button should always be visible.",
                View.VISIBLE,
                mView.getContinueButton().getVisibility());
        assertEquals(
                "The dismiss button should always be visible",
                View.VISIBLE,
                mView.getDismissButton().getVisibility());
    }

    @Test
    @UiThreadTest
    @SmallTest
    public void testDeviceLockView_uiEnabled_showsEnabledUi() {
        mViewModel.set(UI_ENABLED, true);

        assertEquals(
                "The progress bar should not be visible.",
                View.INVISIBLE,
                mView.getProgressBar().getVisibility());
        assertTrue("The continue button should be enabled.", mView.getContinueButton().isEnabled());
        assertTrue("The dismiss button should be enabled.", mView.getDismissButton().isEnabled());
    }

    @Test
    @UiThreadTest
    @SmallTest
    public void testDeviceLockView_uiDisabled_showsDisabledUi() {
        mViewModel.set(UI_ENABLED, false);

        assertEquals(
                "The progress bar should be visible.",
                View.VISIBLE,
                mView.getProgressBar().getVisibility());
        assertFalse(
                "The continue button should be disabled.", mView.getContinueButton().isEnabled());
        assertFalse("The dismiss button should be disabled.", mView.getDismissButton().isEnabled());
    }

    @Test
    @UiThreadTest
    @SmallTest
    public void
            testDeviceLockView_inSignInFlowWithPreExistingLock_dismissButtonHasDismissedSignInText() {
        mViewModel.set(SOURCE, DeviceLockActivityLauncher.Source.ACCOUNT_PICKER);
        mViewModel.set(PREEXISTING_DEVICE_LOCK, true);

        assertEquals(
                "The dismiss button should show fre dismissal text when in the sign in flow.",
                mView.getDismissButton().getText(),
                sActivity.getResources().getString(R.string.signin_fre_dismiss_button));
    }

    @Test
    @UiThreadTest
    @SmallTest
    public void
            testDeviceLockView_inSignInFlowWithNoPreExistingLock_dismissButtonHasNoThanksText() {
        mViewModel.set(SOURCE, DeviceLockActivityLauncher.Source.ACCOUNT_PICKER);
        mViewModel.set(PREEXISTING_DEVICE_LOCK, false);

        assertEquals(
                "The dismiss button should show 'not now' text when in the sign in flow.",
                mView.getDismissButton().getText(),
                sActivity.getResources().getString(R.string.history_sync_secondary_action));
    }

    @Test
    @UiThreadTest
    @SmallTest
    public void testDeviceLockView_notInSignInFlow_dismissButtonHasNoThanksText() {
        mViewModel.set(SOURCE, DeviceLockActivityLauncher.Source.AUTOFILL);

        assertEquals(
                "The dismiss button should show 'no thanks' text when not in the sign in flow.",
                mView.getDismissButton().getText(),
                sActivity.getResources().getString(R.string.no_thanks));
    }

    @Test
    @UiThreadTest
    @SmallTest
    public void testDeviceLockView_createDeviceLockButtonClicked_triggersOnClick() {
        mViewModel.set(PREEXISTING_DEVICE_LOCK, false);
        mViewModel.set(DEVICE_SUPPORTS_PIN_CREATION_INTENT, true);
        mCreateDeviceLockButtonClicked.set(false);

        mView.getContinueButton().performClick();
        assertTrue(
                "A click on the continue button should trigger the device lock creation handler.",
                mCreateDeviceLockButtonClicked.get());
    }

    @Test
    @UiThreadTest
    @SmallTest
    public void testDeviceLockView_goToOSSettingsButtonClicked_triggersOnClick() {
        mViewModel.set(PREEXISTING_DEVICE_LOCK, false);
        mViewModel.set(DEVICE_SUPPORTS_PIN_CREATION_INTENT, false);
        mGoToOSSettingsButtonClicked.set(false);

        mView.getContinueButton().performClick();
        assertTrue(
                "A click on the continue button should trigger the 'go to OS settings' handler.",
                mGoToOSSettingsButtonClicked.get());
    }

    @Test
    @UiThreadTest
    @SmallTest
    public void testDeviceLockView_userUnderstandsButtonClicked_triggersOnClick() {
        mViewModel.set(PREEXISTING_DEVICE_LOCK, true);
        mUserUnderstandsButtonClicked.set(false);

        mView.getContinueButton().performClick();
        assertTrue(
                "A click on the continue button should trigger the 'user understands' handler.",
                mUserUnderstandsButtonClicked.get());
    }

    @Test
    @UiThreadTest
    @SmallTest
    public void testDeviceLockView_dismissButtonClicked_triggersOnClick() {
        mDismissButtonClicked.set(false);

        mView.getDismissButton().performClick();
        assertTrue(
                "A click on the dismiss button should trigger the dismissal handler.",
                mDismissButtonClicked.get());
    }
}
