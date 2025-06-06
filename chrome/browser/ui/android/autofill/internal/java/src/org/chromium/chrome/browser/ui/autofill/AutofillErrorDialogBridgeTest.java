// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ui.autofill;

import static com.google.common.truth.Truth.assertThat;

import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.reset;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.drawable.Drawable;
import android.view.View;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.core.content.res.ResourcesCompat;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.filters.SmallTest;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.ui.autofill.internal.R;
import org.chromium.ui.modaldialog.ModalDialogManager.ModalDialogType;
import org.chromium.ui.modaldialog.ModalDialogProperties;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.test.util.modaldialog.FakeModalDialogManager;

/** Unit tests for {@link AutofillErrorDialogBridge} */
@RunWith(BaseRobolectricTestRunner.class)
public class AutofillErrorDialogBridgeTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    private static final String ERROR_DIALOG_TITLE = "title";
    private static final String ERROR_DIALOG_DESCRIPTION = "description";
    private static final String ERROR_DIALOG_BUTTON_LABEL = "Close";
    private static final long NATIVE_AUTOFILL_ERROR_DIALOG_VIEW = 100L;

    @Mock private AutofillErrorDialogBridge.Natives mNativeMock;

    private AutofillErrorDialogBridge mAutofillErrorDialogBridge;
    private FakeModalDialogManager mModalDialogManager;
    private Resources mResources;

    @Before
    public void setUp() {
        reset(mNativeMock);
        mModalDialogManager = new FakeModalDialogManager(ModalDialogType.TAB);
        mResources = ApplicationProvider.getApplicationContext().getResources();
        mAutofillErrorDialogBridge =
                new AutofillErrorDialogBridge(
                        NATIVE_AUTOFILL_ERROR_DIALOG_VIEW,
                        mModalDialogManager,
                        ApplicationProvider.getApplicationContext());
        AutofillErrorDialogBridgeJni.setInstanceForTesting(mNativeMock);
    }

    @Test
    @SmallTest
    public void testBasic() throws Exception {
        showErrorDialog();
        Assert.assertNotNull(mModalDialogManager.getShownDialogModel());

        mAutofillErrorDialogBridge.dismiss();
        // Verify that no dialog is shown and that the callback is triggered on dismissal.
        Assert.assertNull(mModalDialogManager.getShownDialogModel());
        verify(mNativeMock, times(1)).onDismissed(NATIVE_AUTOFILL_ERROR_DIALOG_VIEW);
    }

    @Test
    @SmallTest
    public void testDismissedCalledOnButtonClick() throws Exception {
        showErrorDialog();

        mModalDialogManager.clickPositiveButton();

        verify(mNativeMock, times(1)).onDismissed(NATIVE_AUTOFILL_ERROR_DIALOG_VIEW);
    }

    @Test
    @SmallTest
    public void testTitleView() throws Exception {
        showErrorDialog();

        PropertyModel model = mModalDialogManager.getShownDialogModel();
        Assert.assertNotNull(model);

        View customView = model.get(ModalDialogProperties.CUSTOM_VIEW);

        // Verify that the title set by custom view is correct.
        TextView title = customView.findViewById(R.id.title);
        assertThat(title.getVisibility()).isEqualTo(View.VISIBLE);
        assertThat(title.getText()).isEqualTo(ERROR_DIALOG_TITLE);

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

    private void showErrorDialog() {
        mAutofillErrorDialogBridge.show(
                ERROR_DIALOG_TITLE, ERROR_DIALOG_DESCRIPTION, ERROR_DIALOG_BUTTON_LABEL);
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
