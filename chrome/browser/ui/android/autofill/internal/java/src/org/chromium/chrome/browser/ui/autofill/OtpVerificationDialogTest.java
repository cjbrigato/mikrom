// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ui.autofill;

import static com.google.common.truth.Truth.assertThat;

import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.drawable.Drawable;
import android.text.SpannableString;
import android.text.style.ClickableSpan;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.core.content.res.ResourcesCompat;
import androidx.test.core.app.ApplicationProvider;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.ui.autofill.internal.R;
import org.chromium.ui.modaldialog.DialogDismissalCause;
import org.chromium.ui.modaldialog.ModalDialogManager.ModalDialogType;
import org.chromium.ui.modaldialog.ModalDialogProperties;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.test.util.modaldialog.FakeModalDialogManager;

import java.util.Optional;

/** Unit tests for {@link OtpVerificationDialogView}. */
@RunWith(BaseRobolectricTestRunner.class)
public class OtpVerificationDialogTest {

    private static final String ERROR_MESSAGE = "Error message";
    private static final String VALID_OTP = "123456";

    private OtpVerificationDialogView mOtpVerificationDialogView;
    private FakeModalDialogManager mModalDialogManager;
    private OtpVerificationDialogCoordinator mOtpVerificationDialogCoordinator;
    private Resources mResources;

    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();
    @Mock private OtpVerificationDialogCoordinator.Delegate mDelegate;

    @Before
    public void setUp() {
        mModalDialogManager = new FakeModalDialogManager(ModalDialogType.TAB);
        mResources = ApplicationProvider.getApplicationContext().getResources();
        mOtpVerificationDialogView =
                (OtpVerificationDialogView)
                        LayoutInflater.from(ApplicationProvider.getApplicationContext())
                                .inflate(R.layout.otp_verification_dialog, null);
        mOtpVerificationDialogCoordinator =
                new OtpVerificationDialogCoordinator(
                        ApplicationProvider.getApplicationContext(),
                        mModalDialogManager,
                        mOtpVerificationDialogView,
                        mDelegate);
    }

    @Test
    public void testDefaultState() {
        int otpLength = 6;

        mOtpVerificationDialogCoordinator.show(otpLength);

        PropertyModel model = mModalDialogManager.getShownDialogModel();
        View view = model.get(ModalDialogProperties.CUSTOM_VIEW);
        // Verify that the error message is not shown.
        assertThat(view.findViewById(R.id.otp_error_message).getVisibility()).isEqualTo(View.GONE);
        // Verify that the hint shown in the OTP input field contains the value of the otpLength set
        // above.
        assertThat(((EditText) view.findViewById(R.id.otp_input)).getHint())
                .isEqualTo(
                        ApplicationProvider.getApplicationContext()
                                .getString(
                                        R.string
                                                .autofill_payments_otp_verification_dialog_otp_input_hint,
                                        otpLength));
        // Verify that the positive button is disabled.
        assertThat(model.get(ModalDialogProperties.POSITIVE_BUTTON_DISABLED)).isTrue();
    }

    @Test
    public void testShowHideErrorMessage() {
        mOtpVerificationDialogCoordinator.show(/* otpLength= */ 6);
        mOtpVerificationDialogView.showOtpErrorMessage(Optional.of(ERROR_MESSAGE));

        PropertyModel model = mModalDialogManager.getShownDialogModel();
        View view = model.get(ModalDialogProperties.CUSTOM_VIEW);
        // Verify that the error message is shown.
        TextView errorMessageTextView = view.findViewById(R.id.otp_error_message);
        assertThat(errorMessageTextView.getVisibility()).isEqualTo(View.VISIBLE);
        assertThat(errorMessageTextView.getText()).isEqualTo(ERROR_MESSAGE);

        // Verify that editing the error test, hides the error message.
        EditText otpInputEditText = view.findViewById(R.id.otp_input);
        otpInputEditText.setText("123");
        assertThat(errorMessageTextView.getVisibility()).isEqualTo(View.GONE);
    }

    @Test
    public void testPositiveButtonDisabledState() {
        mOtpVerificationDialogCoordinator.show(/* otpLength= */ 6);
        PropertyModel model = mModalDialogManager.getShownDialogModel();
        View view = model.get(ModalDialogProperties.CUSTOM_VIEW);

        EditText otpInputEditText = view.findViewById(R.id.otp_input);

        // Verify that the positive button is disabled for input length < otpLength.
        otpInputEditText.setText("123");
        assertThat(model.get(ModalDialogProperties.POSITIVE_BUTTON_DISABLED)).isTrue();

        // Verify that the positive button is enabled for input length == otpLength.
        otpInputEditText.setText("123456");
        assertThat(model.get(ModalDialogProperties.POSITIVE_BUTTON_DISABLED)).isFalse();

        // Verify that the positive button is disabled for input length > otpLength.
        otpInputEditText.setText("1234567");
        assertThat(model.get(ModalDialogProperties.POSITIVE_BUTTON_DISABLED)).isTrue();
    }

    @Test
    public void testOtpSubmission() {
        mOtpVerificationDialogCoordinator.show(/* otpLength= */ 6);
        PropertyModel model = mModalDialogManager.getShownDialogModel();
        View view = model.get(ModalDialogProperties.CUSTOM_VIEW);
        EditText otpInputEditText = view.findViewById(R.id.otp_input);
        otpInputEditText.setText(VALID_OTP);

        mModalDialogManager.clickPositiveButton();

        // Verify that the listener is called with the text entered in the OTP input field.
        verify(mDelegate, times(1)).onConfirm(VALID_OTP);
        // Verify that the progress bar is shown.
        View progressBarOverlayView = view.findViewById(R.id.progress_bar_overlay);
        assertThat(progressBarOverlayView.getVisibility()).isEqualTo(View.VISIBLE);

        // Verify that expected messages are showing during code verification.
        assertThat(progressBarOverlayView.findViewById(R.id.progress_bar).getVisibility())
                .isEqualTo(View.VISIBLE);
        TextView progressBarMessage =
                progressBarOverlayView.findViewById(R.id.progress_bar_message);
        assertThat(progressBarMessage.getText())
                .isEqualTo(
                        mResources.getString(
                                R.string.autofill_card_unmask_otp_input_dialog_pending_message));
    }

    @Test
    public void testGetNewCode() {
        mOtpVerificationDialogCoordinator.show(/* otpLength= */ 6);
        PropertyModel model = mModalDialogManager.getShownDialogModel();
        View view = model.get(ModalDialogProperties.CUSTOM_VIEW);
        TextView otpResendMessageTextView = view.findViewById(R.id.otp_resend_message);
        SpannableString otpResendMessage = (SpannableString) otpResendMessageTextView.getText();
        ClickableSpan getNewCodeSpan =
                otpResendMessage.getSpans(0, otpResendMessage.length(), ClickableSpan.class)[0];

        getNewCodeSpan.onClick(otpResendMessageTextView);

        verify(mDelegate, times(1)).onNewOtpRequested();
    }

    @Test
    public void testDialogDismissal() {
        mOtpVerificationDialogCoordinator.show(/* otpLength= */ 6);

        mModalDialogManager.dismissDialog(
                mModalDialogManager.getShownDialogModel(),
                DialogDismissalCause.NEGATIVE_BUTTON_CLICKED);

        verify(mDelegate, times(1)).onDialogDismissed();
    }

    @Test
    public void testTitleView() throws Exception {
        mOtpVerificationDialogCoordinator.show(/* otpLength= */ 6);

        PropertyModel model = mModalDialogManager.getShownDialogModel();
        assertThat(model).isNotNull();

        View customView = model.get(ModalDialogProperties.CUSTOM_VIEW);

        // Verify that the title set by custom view is correct.
        TextView title = customView.findViewById(R.id.title);
        assertThat(title.getVisibility()).isEqualTo(View.VISIBLE);
        assertThat(title.getText())
                .isEqualTo(
                        mResources.getString(R.string.autofill_card_unmask_otp_input_dialog_title));

        // Verify that the title icon set by custom view is correct.
        ImageView title_icon = customView.findViewById(R.id.title_icon);
        Drawable expectedDrawable =
                ResourcesCompat.getDrawable(
                        mResources,
                        R.drawable.google_pay,
                        ApplicationProvider.getApplicationContext().getTheme());
        assertThat(title_icon.getVisibility()).isEqualTo(View.VISIBLE);
        assertTrue(getBitmap(expectedDrawable).sameAs(getBitmap(title_icon.getDrawable())));
    }

    // Convert a drawable to a Bitmap for comparison.
    private static Bitmap getBitmap(Drawable drawable) {
        Bitmap bitmap =
                Bitmap.createBitmap(
                        drawable.getIntrinsicWidth(),
                        drawable.getIntrinsicHeight(),
                        Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        drawable.setBounds(0, 0, canvas.getWidth(), canvas.getHeight());
        drawable.draw(canvas);
        return bitmap;
    }
}
