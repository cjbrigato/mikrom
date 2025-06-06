// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.dom_distiller;

import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;

import android.content.Context;
import android.content.res.Resources;
import android.graphics.drawable.Drawable;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.FeatureOverrides;
import org.chromium.base.UserDataHost;
import org.chromium.base.supplier.Supplier;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.toolbar.optional_button.ButtonData;
import org.chromium.ui.modaldialog.ModalDialogManager;

/** This class tests the behavior of the {@link ReaderModeToolbarButtonController}. */
@RunWith(BaseRobolectricTestRunner.class)
public class ReaderModeToolbarButtonControllerTest {
    @Rule public final MockitoRule mMockitoRule = MockitoJUnit.rule();
    @Mock private Tab mMockTab;
    @Mock private ReaderModeManager mMockReaderModeManager;
    @Mock private Supplier<Tab> mMockTabSupplier;
    @Mock private ModalDialogManager mMockModalDialogManager;
    private UserDataHost mUserDataHost;

    @Before
    public void setUp() throws Exception {
        mUserDataHost = new UserDataHost();

        Context mockContext = mock(Context.class);
        Resources mockResources = mock(Resources.class);

        doReturn(mockResources).when(mockContext).getResources();
        doReturn(mockContext).when(mMockTab).getContext();
        doReturn(mMockTab).when(mMockTabSupplier).get();
        doReturn(mUserDataHost).when(mMockTab).getUserDataHost();
        mUserDataHost.setUserData(ReaderModeManager.USER_DATA_KEY, mMockReaderModeManager);

        FeatureOverrides.enable(ChromeFeatureList.ADAPTIVE_BUTTON_IN_TOP_TOOLBAR_CUSTOMIZATION_V2);
    }

    private ReaderModeToolbarButtonController createController() {
        return new ReaderModeToolbarButtonController(
                mMockTab.getContext(),
                mMockTabSupplier,
                mMockModalDialogManager,
                mock(Drawable.class));
    }

    @Test
    public void testButtonClickEnablesReaderMode() {
        ReaderModeToolbarButtonController controller = createController();

        ButtonData readerModeButton = controller.get(mMockTab);
        readerModeButton.getButtonSpec().getOnClickListener().onClick(null);

        verify(mMockReaderModeManager).activateReaderMode();
    }
}
