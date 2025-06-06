// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.autofill;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.verify;

import android.widget.Button;
import android.widget.TextView;

import androidx.test.filters.SmallTest;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.ui.messages.snackbar.Snackbar;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.R;
import org.chromium.chrome.test.transit.ChromeTransitTestRules;
import org.chromium.chrome.test.transit.FreshCtaTransitTestRule;

import java.util.concurrent.ExecutionException;

/** Instrumentation tests for {@link AutofillSnackbar} */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
public class AutofillSnackbarControllerTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Rule
    public FreshCtaTransitTestRule mActivityTestRule =
            ChromeTransitTestRules.freshChromeTabbedActivityRule();

    private static final String SNACKBAR_MESSAGE_TEXT = "message_text";
    private static final String SNACKBAR_ACTION_TEXT = "action_text";
    private static final int SNACKBAR_DURATION = 10000;
    private static final long NATIVE_AUTOFILL_SNACKBAR_VIEW = 100L;

    @Mock private AutofillSnackbarController.Natives mNativeMock;

    private AutofillSnackbarController mAutofillSnackbarController;
    private SnackbarManager mSnackbarManager;

    @Before
    public void setUp() {
        mActivityTestRule.startOnBlankPage();
        mSnackbarManager = mActivityTestRule.getActivity().getSnackbarManager();
        mAutofillSnackbarController =
                new AutofillSnackbarController(NATIVE_AUTOFILL_SNACKBAR_VIEW, mSnackbarManager);
        AutofillSnackbarControllerJni.setInstanceForTesting(mNativeMock);
    }

    @Test
    @SmallTest
    public void testShow() throws Exception {
        showSnackbar();

        Snackbar currentSnackbar = getCurrentSnackbar();
        assertEquals(
                "Incorrect snackbar message text", SNACKBAR_MESSAGE_TEXT, getSnackbarMessageText());
        assertEquals(
                "Incorrect snackbar action text", SNACKBAR_ACTION_TEXT, getSnackbarActionText());
        assertEquals(
                "Incorrect snackbar duration", SNACKBAR_DURATION, currentSnackbar.getDuration());

        assertTrue(
                "Incorrect SnackbarController type",
                currentSnackbar.getController() instanceof AutofillSnackbarController);
    }

    @Test
    @SmallTest
    public void testDismiss() throws Exception {
        showSnackbar();

        dismissSnackbar();

        assertNull(getCurrentSnackbar());
        verify(mNativeMock).onDismissed(NATIVE_AUTOFILL_SNACKBAR_VIEW);
    }

    @Test
    @SmallTest
    public void testOnSnackbarActionClicked() throws Exception {
        showSnackbar();

        clickSnackbarAction();

        verify(mNativeMock).onActionClicked(NATIVE_AUTOFILL_SNACKBAR_VIEW);
        verify(mNativeMock).onDismissed(NATIVE_AUTOFILL_SNACKBAR_VIEW);
    }

    @Test
    @SmallTest
    public void testOnSnackbarAutoDismissed() throws Exception {
        showSnackbar();

        timeoutSnackbar();

        verify(mNativeMock).onDismissed(NATIVE_AUTOFILL_SNACKBAR_VIEW);
    }

    private void showSnackbar() {
        ThreadUtils.runOnUiThreadBlocking(
                () ->
                        mAutofillSnackbarController.show(
                                SNACKBAR_MESSAGE_TEXT, SNACKBAR_ACTION_TEXT, SNACKBAR_DURATION));
    }

    private void dismissSnackbar() {
        ThreadUtils.runOnUiThreadBlocking(() -> mAutofillSnackbarController.dismiss());
    }

    private void clickSnackbarAction() {
        ThreadUtils.runOnUiThreadBlocking(
                () ->
                        mSnackbarManager.onClick(
                                mActivityTestRule
                                        .getActivity()
                                        .findViewById(R.id.snackbar_button)));
    }

    private void timeoutSnackbar() {
        ThreadUtils.runOnUiThreadBlocking(
                () ->
                        mSnackbarManager.dismissSnackbars(
                                mSnackbarManager.getCurrentSnackbarForTesting().getController()));
    }

    private String getSnackbarMessageText() {
        return ((TextView) mActivityTestRule.getActivity().findViewById(R.id.snackbar_message))
                .getText()
                .toString();
    }

    private String getSnackbarActionText() {
        return ((Button) mActivityTestRule.getActivity().findViewById(R.id.snackbar_button))
                .getText()
                .toString();
    }

    private Snackbar getCurrentSnackbar() throws ExecutionException {
        return ThreadUtils.runOnUiThreadBlocking(
                () -> mSnackbarManager.getCurrentSnackbarForTesting());
    }
}
