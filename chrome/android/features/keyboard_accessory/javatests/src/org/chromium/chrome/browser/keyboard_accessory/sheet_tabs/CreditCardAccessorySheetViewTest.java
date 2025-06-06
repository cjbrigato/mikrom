// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.keyboard_accessory.sheet_tabs;

import static androidx.test.espresso.matcher.ViewMatchers.assertThat;

import static org.hamcrest.Matchers.instanceOf;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.not;
import static org.hamcrest.Matchers.notNullValue;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.annotation.IdRes;
import androidx.recyclerview.widget.RecyclerView;
import androidx.test.filters.MediumTest;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Criteria;
import org.chromium.base.test.util.CriteriaHelper;
import org.chromium.chrome.browser.autofill.AutofillImageFetcher;
import org.chromium.chrome.browser.autofill.AutofillImageFetcherFactory;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.keyboard_accessory.AccessorySuggestionType;
import org.chromium.chrome.browser.keyboard_accessory.AccessoryTabType;
import org.chromium.chrome.browser.keyboard_accessory.R;
import org.chromium.chrome.browser.keyboard_accessory.data.KeyboardAccessoryData;
import org.chromium.chrome.browser.keyboard_accessory.data.KeyboardAccessoryData.LoyaltyCardInfo;
import org.chromium.chrome.browser.keyboard_accessory.data.KeyboardAccessoryData.PromoCodeInfo;
import org.chromium.chrome.browser.keyboard_accessory.data.KeyboardAccessoryData.UserInfo;
import org.chromium.chrome.browser.keyboard_accessory.data.UserInfoField;
import org.chromium.chrome.browser.keyboard_accessory.sheet_component.AccessorySheetCoordinator;
import org.chromium.chrome.browser.keyboard_accessory.sheet_tabs.AccessorySheetTabItemsModel.AccessorySheetDataPiece;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.ChromeTabbedActivityTestRule;
import org.chromium.components.browser_ui.widget.chips.ChipView;
import org.chromium.url.GURL;

import java.util.Optional;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

/** View tests for the credit card accessory sheet. */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
@SuppressWarnings("DoNotMock") // Mocks GURL
public class CreditCardAccessorySheetViewTest {
    private static final String CUSTOM_ICON_URL = "https://www.example.com/image.png";
    private static final Bitmap TEST_CARD_ART_IMAGE =
            Bitmap.createBitmap(100, 200, Bitmap.Config.ARGB_8888);

    private AccessorySheetTabItemsModel mModel;
    private final AtomicReference<RecyclerView> mView = new AtomicReference<>();

    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Rule
    public ChromeTabbedActivityTestRule mActivityTestRule = new ChromeTabbedActivityTestRule();

    @Mock AutofillImageFetcher mMockImageFetcher;

    @Before
    public void setUp() throws InterruptedException {
        mActivityTestRule.startMainActivityOnBlankPage();
        AutofillImageFetcherFactory.setInstanceForTesting(mMockImageFetcher);
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mModel = new AccessorySheetTabItemsModel();
                    AccessorySheetCoordinator accessorySheet =
                            new AccessorySheetCoordinator(
                                    mActivityTestRule
                                            .getActivity()
                                            .findViewById(R.id.keyboard_accessory_sheet_stub),
                                    null);
                    accessorySheet.setTabs(
                            new KeyboardAccessoryData.Tab[] {
                                new KeyboardAccessoryData.Tab(
                                        "Credit Cards",
                                        null,
                                        null,
                                        R.layout.credit_card_accessory_sheet,
                                        AccessoryTabType.CREDIT_CARDS,
                                        new KeyboardAccessoryData.Tab.Listener() {
                                            @Override
                                            public void onTabCreated(ViewGroup view) {
                                                mView.set((RecyclerView) view);
                                                AccessorySheetTabViewBinder.initializeView(
                                                        mView.get(), null);
                                                CreditCardAccessorySheetViewBinder.initializeView(
                                                        mView.get(),
                                                        CreditCardAccessorySheetCoordinator
                                                                .createUiConfiguration(
                                                                        view.getContext(),
                                                                        mMockImageFetcher),
                                                        mModel);
                                            }

                                            @Override
                                            public void onTabShown() {}
                                        })
                            });
                    accessorySheet.setHeight(
                            mActivityTestRule
                                    .getActivity()
                                    .getResources()
                                    .getDimensionPixelSize(
                                            R.dimen.keyboard_accessory_sheet_height));
                    accessorySheet.show();
                });
        CriteriaHelper.pollUiThread(() -> Criteria.checkThat(mView.get(), notNullValue()));
    }

    @After
    public void tearDown() {
        mView.set(null);
    }

    @Test
    @MediumTest
    public void testAddingCaptionsToTheModelRendersThem() {
        assertThat(mView.get().getChildCount(), is(0));

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    "Credit Cards", AccessorySheetDataPiece.Type.TITLE));
                });

        CriteriaHelper.pollUiThread(() -> Criteria.checkThat(mView.get().getChildCount(), is(1)));
        View title = mView.get().findViewById(R.id.tab_title);
        assertThat(title, is(not(nullValue())));
        assertThat(title, instanceOf(TextView.class));
        assertThat(((TextView) title).getText(), is("Credit Cards"));
    }

    @Test
    @MediumTest
    public void testAddingUserInfoToTheModelRendersClickableActions() throws ExecutionException {
        final AtomicBoolean clicked = new AtomicBoolean();
        assertThat(mView.get().getChildCount(), is(0));

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    createInfo(
                                            "visaCC",
                                            "4111111111111111",
                                            "04",
                                            "2034",
                                            "Kirby Puckett",
                                            "123",
                                            null,
                                            clicked),
                                    AccessorySheetDataPiece.Type.CREDIT_CARD_INFO));
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    new KeyboardAccessoryData.FooterCommand(
                                            "Manage credit cards", null),
                                    AccessorySheetDataPiece.Type.FOOTER_COMMAND));
                });

        CriteriaHelper.pollUiThread(() -> Criteria.checkThat(mView.get().getChildCount(), is(2)));

        // Check that the titles are correct:
        assertThat(getChipText(R.id.cc_number), is("4111111111111111"));
        assertThat(getChipText(R.id.exp_month), is("04"));
        assertThat(getChipText(R.id.exp_year), is("2034"));
        assertThat(getChipText(R.id.cardholder), is("Kirby Puckett"));
        // Verify that the icon is correctly set.
        ImageView iconImageView = (ImageView) mView.get().getChildAt(0).findViewById(R.id.icon);
        Drawable expectedIcon =
                mActivityTestRule.getActivity().getDrawable(R.drawable.visa_metadata_card);
        assertTrue(getBitmap(expectedIcon).sameAs(getBitmap(iconImageView.getDrawable())));
        // Chips are clickable:
        ThreadUtils.runOnUiThreadBlocking(findChipView(R.id.cc_number)::performClick);
        assertThat(clicked.get(), is(true));
        clicked.set(false);
        ThreadUtils.runOnUiThreadBlocking(findChipView(R.id.exp_month)::performClick);
        assertThat(clicked.get(), is(true));
    }

    @Test
    @MediumTest
    public void testAddingUserInfoWithIconUrl_iconCachedInImageFetcher() throws ExecutionException {
        GURL iconUrl = mock(GURL.class);
        when(iconUrl.isValid()).thenReturn(true);
        when(iconUrl.getSpec()).thenReturn(CUSTOM_ICON_URL);
        // Return the cached image when AutofillImageFetcher.getImageIfAvailable is called for the
        // above url.
        when(mMockImageFetcher.getImageIfAvailable(any(), any()))
                .thenReturn(Optional.of(TEST_CARD_ART_IMAGE));

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    createInfo(
                                            "",
                                            "4111111111111111",
                                            "04",
                                            "2034",
                                            "Kirby Puckett",
                                            "123",
                                            iconUrl,
                                            new AtomicBoolean()),
                                    AccessorySheetDataPiece.Type.CREDIT_CARD_INFO));
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    new KeyboardAccessoryData.FooterCommand(
                                            "Manage credit cards", null),
                                    AccessorySheetDataPiece.Type.FOOTER_COMMAND));
                });

        CriteriaHelper.pollUiThread(() -> Criteria.checkThat(mView.get().getChildCount(), is(2)));
        // Check that the titles are correct:
        assertThat(getChipText(R.id.cc_number), is("4111111111111111"));
        assertThat(getChipText(R.id.exp_month), is("04"));
        assertThat(getChipText(R.id.exp_year), is("2034"));
        assertThat(getChipText(R.id.cardholder), is("Kirby Puckett"));
        // Verify that the icon is set to the cached image returned by
        // AutofillImageFetcher.getImageIfAvailable.
        ImageView iconImageView = (ImageView) mView.get().getChildAt(0).findViewById(R.id.icon);
        assertTrue(
                ((BitmapDrawable) iconImageView.getDrawable())
                        .getBitmap()
                        .equals(TEST_CARD_ART_IMAGE));
    }

    @Test
    @MediumTest
    public void testAddingUserInfoWithIconUrl_iconNotCachedInAutofillImageFetcher()
            throws ExecutionException {
        GURL iconUrl = mock(GURL.class);
        when(iconUrl.isValid()).thenReturn(true);
        when(iconUrl.getSpec()).thenReturn(CUSTOM_ICON_URL);
        // Return null to AutofillImageFetcher.getImageIfAvailable to indicate that the image is not
        // present in the cache.
        when(mMockImageFetcher.getImageIfAvailable(any(), any())).thenReturn(Optional.empty());

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    createInfo(
                                            "visaCC",
                                            "4111111111111111",
                                            "04",
                                            "2034",
                                            "Kirby Puckett",
                                            "123",
                                            iconUrl,
                                            new AtomicBoolean()),
                                    AccessorySheetDataPiece.Type.CREDIT_CARD_INFO));
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    new KeyboardAccessoryData.FooterCommand(
                                            "Manage credit cards", null),
                                    AccessorySheetDataPiece.Type.FOOTER_COMMAND));
                });

        CriteriaHelper.pollUiThread(() -> Criteria.checkThat(mView.get().getChildCount(), is(2)));
        // Check that the titles are correct:
        assertThat(getChipText(R.id.cc_number), is("4111111111111111"));
        assertThat(getChipText(R.id.exp_month), is("04"));
        assertThat(getChipText(R.id.exp_year), is("2034"));
        assertThat(getChipText(R.id.cardholder), is("Kirby Puckett"));
        // Verify that the icon is set to the drawable corresponding to `visaCC`.
        ImageView iconImageView = (ImageView) mView.get().getChildAt(0).findViewById(R.id.icon);
        Drawable expectedIcon =
                mActivityTestRule.getActivity().getDrawable(R.drawable.visa_metadata_card);
        assertTrue(getBitmap(expectedIcon).sameAs(getBitmap(iconImageView.getDrawable())));
    }

    @Test
    @MediumTest
    public void testAddingUnselectableFieldsRendersUnclickabeChips() {
        assertThat(mView.get().getChildCount(), is(0));

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    UserInfo infoWithUnclickableField = new UserInfo("", false);
                    infoWithUnclickableField.addField(
                            new UserInfoField.Builder()
                                    .setSuggestionType(AccessorySuggestionType.CREDIT_CARD_NUMBER)
                                    .setDisplayText("4111111111111111")
                                    .setA11yDescription("4111111111111111")
                                    .build());
                    infoWithUnclickableField.addField(
                            new UserInfoField.Builder()
                                    .setSuggestionType(
                                            AccessorySuggestionType.CREDIT_CARD_EXPIRATION_MONTH)
                                    .setDisplayText("month")
                                    .setId("month")
                                    .build());
                    infoWithUnclickableField.addField(
                            new UserInfoField.Builder()
                                    .setSuggestionType(
                                            AccessorySuggestionType.CREDIT_CARD_EXPIRATION_YEAR)
                                    .setDisplayText("year")
                                    .setId("year")
                                    .build());
                    infoWithUnclickableField.addField(
                            new UserInfoField.Builder()
                                    .setSuggestionType(
                                            AccessorySuggestionType.CREDIT_CARD_NAME_FULL)
                                    .setDisplayText("name")
                                    .setId("name")
                                    .build());
                    infoWithUnclickableField.addField(
                            new UserInfoField.Builder()
                                    .setSuggestionType(AccessorySuggestionType.CREDIT_CARD_CVC)
                                    .setDisplayText("cvc")
                                    .setId("cvc")
                                    .build());
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    infoWithUnclickableField,
                                    AccessorySheetDataPiece.Type.CREDIT_CARD_INFO));
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    new KeyboardAccessoryData.FooterCommand(
                                            "Manage credit cards", null),
                                    AccessorySheetDataPiece.Type.FOOTER_COMMAND));
                });

        CriteriaHelper.pollUiThread(() -> Criteria.checkThat(mView.get().getChildCount(), is(2)));

        assertThat(getChipText(R.id.cc_number), is("4111111111111111"));
        assertThat(findChipView(R.id.cc_number).isShown(), is(true));
        assertThat(findChipView(R.id.cc_number).isEnabled(), is(false));
    }

    @Test
    @MediumTest
    public void testEmptyChipsAreNotVisible() {
        final AtomicBoolean clicked = new AtomicBoolean();
        assertThat(mView.get().getChildCount(), is(0));

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    // Cardholder name is empty
                                    createInfo(
                                            "",
                                            "4111111111111111",
                                            "04",
                                            "2034",
                                            "",
                                            "",
                                            null,
                                            clicked),
                                    AccessorySheetDataPiece.Type.CREDIT_CARD_INFO));
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    new KeyboardAccessoryData.FooterCommand(
                                            "Manage credit cards", null),
                                    AccessorySheetDataPiece.Type.FOOTER_COMMAND));
                });

        CriteriaHelper.pollUiThread(() -> Criteria.checkThat(mView.get().getChildCount(), is(2)));

        assertThat(findChipView(R.id.cardholder).isShown(), is(false));
        assertThat(findChipView(R.id.cvc).isShown(), is(false));
    }

    @Test
    @MediumTest
    public void testRendersWarning() {
        final String kWarning = "Insecure, so filling is no.";
        assertThat(mView.get().getChildCount(), is(0));

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    kWarning, AccessorySheetDataPiece.Type.WARNING));
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    new KeyboardAccessoryData.FooterCommand(
                                            "Manage credit cards", null),
                                    AccessorySheetDataPiece.Type.FOOTER_COMMAND));
                });

        CriteriaHelper.pollUiThread(() -> Criteria.checkThat(mView.get().getChildCount(), is(2)));

        assertThat(mView.get().getChildAt(0), instanceOf(TextView.class));
        TextView warningView = (TextView) mView.get().getChildAt(0);
        assertThat(warningView.isShown(), is(true));
        assertThat(warningView.getText(), is(kWarning));
    }

    @Test
    @MediumTest
    public void testAddingPromoCodeInfoToTheModelRendersClickableActions()
            throws ExecutionException {
        final String kPromoCode = "$50OFF";
        final String kDetailsText = "Get $50 off when you use this code.";
        final AtomicBoolean clicked = new AtomicBoolean();
        assertThat(mView.get().getChildCount(), is(0));

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    PromoCodeInfo info = new PromoCodeInfo();

                    info.initialize(
                            /* promoCode= */ new UserInfoField.Builder()
                                    .setSuggestionType(AccessorySuggestionType.PROMO_CODE)
                                    .setDisplayText(kPromoCode)
                                    .setA11yDescription("Promo code for test store")
                                    .setCallback(item -> clicked.set(true))
                                    .build(),
                            /* detailsText= */ kDetailsText);
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    info, AccessorySheetDataPiece.Type.PROMO_CODE_INFO));
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    "No payment methods", AccessorySheetDataPiece.Type.TITLE));
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    new KeyboardAccessoryData.FooterCommand(
                                            "Manage credit cards", null),
                                    AccessorySheetDataPiece.Type.FOOTER_COMMAND));
                });

        // mView's child count should be 3: Promo code field, no payment methods message, and footer
        // command.
        CriteriaHelper.pollUiThread(() -> Criteria.checkThat(mView.get().getChildCount(), is(3)));

        // Check that the titles are correct:
        assertThat(getChipText(R.id.promo_code), is(kPromoCode));
        LinearLayout promoCodeLayout = (LinearLayout) mView.get().getChildAt(0);
        assertThat(promoCodeLayout.findViewById(R.id.details_text), instanceOf(TextView.class));
        TextView detailsText = promoCodeLayout.findViewById(R.id.details_text);
        assertThat(detailsText.isShown(), is(true));
        assertThat(detailsText.getText(), is(kDetailsText));

        // Verify that the icon is correctly set.
        ImageView iconImageView = (ImageView) mView.get().getChildAt(0).findViewById(R.id.icon);
        Drawable expectedIcon =
                mActivityTestRule
                        .getActivity()
                        .getResources()
                        .getDrawable(R.drawable.ic_logo_googleg_24dp);
        assertTrue(getBitmap(expectedIcon).sameAs(getBitmap(iconImageView.getDrawable())));
        // Chips are clickable:
        ThreadUtils.runOnUiThreadBlocking(findChipView(R.id.promo_code)::performClick);
        assertThat(clicked.get(), is(true));
    }

    @Test
    @MediumTest
    public void testAddingLoyaltyCardInfo_ProgramLogoIsCached() throws ExecutionException {
        final String kMerchantName = "CVS Pharmacy";
        final String kLoyaltyCardNumber = "987654321";
        final GURL kProgramLogo = new GURL("https:://image.server.com/image.png");
        final AtomicBoolean clicked = new AtomicBoolean();
        assertThat(mView.get().getChildCount(), is(0));

        // Return the cached image when AutofillImageFetcher.getImageIfAvailable is called for the
        // above url.
        when(mMockImageFetcher.getImageIfAvailable(eq(kProgramLogo), any()))
                .thenReturn(Optional.of(TEST_CARD_ART_IMAGE));

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    LoyaltyCardInfo info =
                            new LoyaltyCardInfo(
                                    kMerchantName,
                                    kProgramLogo,
                                    new UserInfoField.Builder()
                                            .setSuggestionType(AccessorySuggestionType.LOYALTY_CARD)
                                            .setDisplayText(kLoyaltyCardNumber)
                                            .setA11yDescription(kLoyaltyCardNumber)
                                            .setCallback(item -> clicked.set(true))
                                            .build());

                    mModel.add(
                            new AccessorySheetDataPiece(
                                    "No payment methods", AccessorySheetDataPiece.Type.TITLE));
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    info, AccessorySheetDataPiece.Type.LOYALTY_CARD_INFO));
                    mModel.add(
                            new AccessorySheetDataPiece(
                                    new KeyboardAccessoryData.FooterCommand(
                                            "Manage loyalty cards", null),
                                    AccessorySheetDataPiece.Type.FOOTER_COMMAND));
                });

        // mView's child count should be 3: no payment methods message, loyalty card field, and
        // footer command.
        CriteriaHelper.pollUiThread(() -> Criteria.checkThat(mView.get().getChildCount(), is(3)));

        // Check that the titles are correct:
        LoyaltyCardInfoView loyaltyCardView = (LoyaltyCardInfoView) mView.get().getChildAt(1);
        assertThat(loyaltyCardView.getMerchantName().isShown(), is(true));
        assertThat(loyaltyCardView.getMerchantName().getText(), is(kMerchantName));
        // The icon must be visible since the image is cached.
        assertThat(loyaltyCardView.findViewById(R.id.loyalty_card_icon).isShown(), is(true));
        assertThat(
                loyaltyCardView.getLoyaltyCardNumber().getPrimaryTextView().getText(),
                is(kLoyaltyCardNumber));

        // Chips are clickable:
        ThreadUtils.runOnUiThreadBlocking(loyaltyCardView.getLoyaltyCardNumber()::performClick);
        assertThat(clicked.get(), is(true));
    }

    @Test
    @MediumTest
    public void testAddingLoyaltyCardInfo_ProgramLogoIsNotCached() throws ExecutionException {
        final String kMerchantName = "CVS Pharmacy";
        final String kLoyaltyCardNumber = "987654321";
        final AtomicBoolean clicked = new AtomicBoolean();
        assertThat(mView.get().getChildCount(), is(0));

        // Return the cached image when AutofillImageFetcher.getImageIfAvailable is called for the
        // above url.
        when(mMockImageFetcher.getImageIfAvailable(any(), any())).thenReturn(Optional.empty());

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    LoyaltyCardInfo info =
                            new LoyaltyCardInfo(
                                    kMerchantName,
                                    new GURL("https:://image.server.com/image.png"),
                                    new UserInfoField.Builder()
                                            .setSuggestionType(AccessorySuggestionType.LOYALTY_CARD)
                                            .setDisplayText(kLoyaltyCardNumber)
                                            .setA11yDescription(kLoyaltyCardNumber)
                                            .setCallback(item -> clicked.set(true))
                                            .build());

                    mModel.add(
                            new AccessorySheetDataPiece(
                                    info, AccessorySheetDataPiece.Type.LOYALTY_CARD_INFO));
                });

        // mView's child count should be 3: no payment methods message, loyalty card field, and
        // footer command.
        CriteriaHelper.pollUiThread(() -> Criteria.checkThat(mView.get().getChildCount(), is(1)));

        // Check that the titles are correct:
        LoyaltyCardInfoView loyaltyCardView = (LoyaltyCardInfoView) mView.get().getChildAt(0);
        assertThat(loyaltyCardView.getMerchantName().isShown(), is(true));
        assertThat(loyaltyCardView.getMerchantName().getText(), is(kMerchantName));
        // Default generated monogram logo icon should be shown.
        assertThat(loyaltyCardView.findViewById(R.id.loyalty_card_icon).isShown(), is(true));
        assertThat(
                loyaltyCardView.getLoyaltyCardNumber().getPrimaryTextView().getText(),
                is(kLoyaltyCardNumber));

        // Chips are clickable:
        ThreadUtils.runOnUiThreadBlocking(loyaltyCardView.getLoyaltyCardNumber()::performClick);
        assertThat(clicked.get(), is(true));
    }

    private UserInfo createInfo(
            String origin,
            String number,
            String month,
            String year,
            String name,
            String cvc,
            GURL iconUrl,
            AtomicBoolean clickRecorder) {
        UserInfo info = new UserInfo(origin, true, iconUrl, false);
        info.addField(
                new UserInfoField.Builder()
                        .setSuggestionType(AccessorySuggestionType.CREDIT_CARD_NUMBER)
                        .setDisplayText(number)
                        .setA11yDescription(number)
                        .setCallback(item -> clickRecorder.set(true))
                        .build());
        info.addField(
                new UserInfoField.Builder()
                        .setSuggestionType(AccessorySuggestionType.CREDIT_CARD_EXPIRATION_MONTH)
                        .setDisplayText(month)
                        .setA11yDescription(month)
                        .setCallback(item -> clickRecorder.set(true))
                        .build());
        info.addField(
                new UserInfoField.Builder()
                        .setSuggestionType(AccessorySuggestionType.CREDIT_CARD_EXPIRATION_YEAR)
                        .setDisplayText(year)
                        .setA11yDescription(year)
                        .setCallback(item -> clickRecorder.set(true))
                        .build());
        info.addField(
                new UserInfoField.Builder()
                        .setSuggestionType(AccessorySuggestionType.CREDIT_CARD_NAME_FULL)
                        .setDisplayText(name)
                        .setA11yDescription(name)
                        .setCallback(item -> clickRecorder.set(true))
                        .build());
        info.addField(
                new UserInfoField.Builder()
                        .setSuggestionType(AccessorySuggestionType.CREDIT_CARD_CVC)
                        .setDisplayText(cvc)
                        .setA11yDescription(cvc)
                        .setCallback(item -> clickRecorder.set(true))
                        .build());
        return info;
    }

    private ChipView findChipView(@IdRes int id) {
        assertThat(mView.get().getChildAt(0), instanceOf(LinearLayout.class));
        LinearLayout layout = (LinearLayout) mView.get().getChildAt(0);
        View view = layout.findViewById(id);
        assertThat(view, is(not(nullValue())));
        assertThat(view, instanceOf(ChipView.class));
        return ((ChipView) view);
    }

    private CharSequence getChipText(@IdRes int id) {
        return findChipView(id).getPrimaryTextView().getText();
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
