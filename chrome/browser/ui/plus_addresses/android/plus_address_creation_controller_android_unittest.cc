// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/plus_addresses/android/plus_address_creation_controller_android.h"

#include <memory>
#include <optional>
#include <string_view>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/test/bind.h"
#include "base/test/gtest_util.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/metrics/user_action_tester.h"
#include "base/test/test_future.h"
#include "chrome/browser/plus_addresses/plus_address_service_factory.h"
#include "chrome/browser/plus_addresses/plus_address_setting_service_factory.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "components/autofill/content/browser/test_autofill_client_injector.h"
#include "components/autofill/content/browser/test_content_autofill_client.h"
#include "components/autofill/core/common/plus_address_survey_type.h"
#include "components/plus_addresses/fake_plus_address_service.h"
#include "components/plus_addresses/features.h"
#include "components/plus_addresses/metrics/plus_address_metrics.h"
#include "components/plus_addresses/plus_address_prefs.h"
#include "components/plus_addresses/plus_address_test_utils.h"
#include "components/plus_addresses/plus_address_types.h"
#include "components/plus_addresses/settings/mock_plus_address_setting_service.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "content/public/browser/browser_context.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace plus_addresses {
namespace {

using ::testing::Optional;
using ::testing::Return;

constexpr std::string_view kPlusAddressModalEventHistogram =
    "PlusAddresses.Modal.Events";
constexpr std::string_view kPlusAddressModalWithNoticeEventHistogram =
    "PlusAddresses.ModalWithNotice.Events";

constexpr base::TimeDelta kDuration = base::Milliseconds(3600);

std::string FormatModalDurationMetrics(
    metrics::PlusAddressModalCompletionStatus status,
    bool notice_shown) {
  return base::ReplaceStringPlaceholders(
      notice_shown ? "PlusAddresses.ModalWithNotice.$1.ShownDuration"
                   : "PlusAddresses.Modal.$1.ShownDuration",
      {metrics::PlusAddressModalCompletionStatusToString(status)},
      /*offsets=*/nullptr);
}

std::string FormatRefreshHistogramNameFor(
    metrics::PlusAddressModalCompletionStatus status,
    bool notice_shown) {
  return base::ReplaceStringPlaceholders(
      notice_shown ? "PlusAddresses.ModalWithNotice.$1.Refreshes"
                   : "PlusAddresses.Modal.$1.Refreshes",
      {metrics::PlusAddressModalCompletionStatusToString(status)},
      /*offsets=*/nullptr);
}

class MockAutofillClient : public autofill::TestContentAutofillClient {
 public:
  using autofill::TestContentAutofillClient::TestContentAutofillClient;
  MOCK_METHOD(void,
              TriggerPlusAddressUserPerceptionSurvey,
              (plus_addresses::hats::SurveyType),
              (override));
};

// Testing very basic functionality for now. As UI complexity increases, this
// class will grow and mutate.
class PlusAddressCreationControllerAndroidEnabledTest
    : public ChromeRenderViewHostTestHarness {
 public:
  PlusAddressCreationControllerAndroidEnabledTest()
      : ChromeRenderViewHostTestHarness(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

  void SetUp() override {
    ChromeRenderViewHostTestHarness::SetUp();
    PlusAddressServiceFactory::GetInstance()->SetTestingFactory(
        browser_context(),
        base::BindLambdaForTesting([&](content::BrowserContext* context)
                                       -> std::unique_ptr<KeyedService> {
          return std::make_unique<FakePlusAddressService>();
        }));
    // TODO: crbug.com/322279583 - Use FakePlusAddressSettingService from the
    // PlusAddressTestEnvironment instead.
    PlusAddressSettingServiceFactory::GetInstance()->SetTestingFactory(
        browser_context(),
        base::BindRepeating([](content::BrowserContext* context)
                                -> std::unique_ptr<KeyedService> {
          return std::make_unique<MockPlusAddressSettingService>();
        }));

    PlusAddressCreationControllerAndroid::CreateForWebContents(web_contents());

    ON_CALL(plus_address_setting_service(), GetHasAcceptedNotice)
        .WillByDefault(Return(true));
  }

  void TearDown() override { ChromeRenderViewHostTestHarness::TearDown(); }

 protected:
  void FastForwardBy(base::TimeDelta delta) {
    task_environment()->FastForwardBy(delta);
  }

  PlusAddressCreationControllerAndroid& controller() {
    return *PlusAddressCreationControllerAndroid::FromWebContents(
        web_contents());
  }

  FakePlusAddressService& plus_address_service() {
    return static_cast<FakePlusAddressService&>(
        *PlusAddressServiceFactory::GetForBrowserContext(browser_context()));
  }

  MockPlusAddressSettingService& plus_address_setting_service() {
    return static_cast<MockPlusAddressSettingService&>(
        *PlusAddressSettingServiceFactory::GetForBrowserContext(
            browser_context()));
  }

  MockAutofillClient& autofill_client() {
    return *autofill_client_injector_[web_contents()];
  }

  base::HistogramTester histogram_tester_;

 private:
  base::test::ScopedFeatureList features_{features::kPlusAddressesEnabled};
  autofill::TestAutofillClientInjector<MockAutofillClient>
      autofill_client_injector_;
};

// Tests that accepting the bottomsheet calls Autofill to fill the plus address
// and records metrics.
TEST_F(PlusAddressCreationControllerAndroidEnabledTest, AcceptCreation) {
  base::UserActionTester user_action_tester;

  // The setting service is not called if the notice is already accepted.
  EXPECT_CALL(plus_address_setting_service(), SetHasAcceptedNotice).Times(0);

  controller().set_suppress_ui_for_testing(true);
  base::test::TestFuture<const std::string&> future;
  controller().OfferCreation(
      url::Origin::Create(GURL("https://mattwashere.example")),
      /*is_manual_fallback=*/true, future.GetCallback());
  FastForwardBy(kDuration);
  EXPECT_CALL(autofill_client(), TriggerPlusAddressUserPerceptionSurvey(
                                     plus_addresses::hats::SurveyType::
                                         kCreatedPlusAddressViaManualFallback));
  controller().OnConfirmed();
  EXPECT_TRUE(future.IsReady());
  EXPECT_THAT(
      histogram_tester_.GetAllSamples(kPlusAddressModalEventHistogram),
      BucketsAre(
          base::Bucket(metrics::PlusAddressModalEvent::kModalShown, 1),
          base::Bucket(metrics::PlusAddressModalEvent::kModalConfirmed, 1)));
  histogram_tester_.ExpectUniqueTimeSample(
      FormatModalDurationMetrics(
          metrics::PlusAddressModalCompletionStatus::kModalConfirmed,
          /*notice_shown=*/false),
      kDuration, 1);
  histogram_tester_.ExpectUniqueSample(
      FormatRefreshHistogramNameFor(
          metrics::PlusAddressModalCompletionStatus::kModalConfirmed,
          /*notice_shown=*/false),
      0, 1);
  // The pref is not set if the first time onboarding notice has been already
  // shown.
  EXPECT_EQ(
      profile()->GetPrefs()->GetTime(prefs::kFirstPlusAddressCreationTime),
      base::Time());
  EXPECT_EQ(user_action_tester.GetActionCount(
                "PlusAddresses.OfferedPlusAddressAccepted"),
            1);
}

// Tests that the notice is shown and its acceptance registered if the notice
// has not been accepted before.
TEST_F(PlusAddressCreationControllerAndroidEnabledTest, ShowNoticeAccept) {
  ON_CALL(plus_address_setting_service(), GetHasAcceptedNotice)
      .WillByDefault(Return(false));
  EXPECT_CALL(plus_address_setting_service(), SetHasAcceptedNotice);

  controller().set_suppress_ui_for_testing(true);
  base::test::TestFuture<const std::string&> future;
  controller().OfferCreation(
      url::Origin::Create(GURL("https://mattwashere.example")),
      /*is_manual_fallback=*/false, future.GetCallback());
  FastForwardBy(kDuration);
  EXPECT_CALL(autofill_client(),
              TriggerPlusAddressUserPerceptionSurvey(
                  plus_addresses::hats::SurveyType::kAcceptedFirstTimeCreate));
  controller().OnConfirmed();
  EXPECT_TRUE(future.IsReady());
  EXPECT_THAT(
      histogram_tester_.GetAllSamples(
          kPlusAddressModalWithNoticeEventHistogram),
      BucketsAre(
          base::Bucket(metrics::PlusAddressModalEvent::kModalShown, 1),
          base::Bucket(metrics::PlusAddressModalEvent::kModalConfirmed, 1)));
  histogram_tester_.ExpectUniqueTimeSample(
      FormatModalDurationMetrics(
          metrics::PlusAddressModalCompletionStatus::kModalConfirmed,
          /*notice_shown=*/true),
      kDuration, 1);
  histogram_tester_.ExpectUniqueSample(
      FormatRefreshHistogramNameFor(
          metrics::PlusAddressModalCompletionStatus::kModalConfirmed,
          /*notice_shown=*/true),
      0, 1);
  // The pref is set only when the first time onboarding notice is shown.
  EXPECT_EQ(profile()->GetTestingPrefService()->GetTime(
                prefs::kFirstPlusAddressCreationTime),
            base::Time::Now());
}

// Tests that the notice is shown if the  `kPlusAddressUserOnboardingEnabled`,
// but cancelling the dialog does not call the settings service.
TEST_F(PlusAddressCreationControllerAndroidEnabledTest, ShowNoticeCancel) {
  ON_CALL(plus_address_setting_service(), GetHasAcceptedNotice)
      .WillByDefault(Return(false));
  EXPECT_CALL(plus_address_setting_service(), SetHasAcceptedNotice).Times(0);

  controller().set_suppress_ui_for_testing(true);
  base::test::TestFuture<const std::string&> future;
  controller().OfferCreation(
      url::Origin::Create(GURL("https://mattwashere.example")),
      /*is_manual_fallback=*/false, future.GetCallback());
  FastForwardBy(kDuration);
  EXPECT_CALL(autofill_client(),
              TriggerPlusAddressUserPerceptionSurvey(
                  plus_addresses::hats::SurveyType::kDeclinedFirstTimeCreate));
  controller().OnCanceled();
  EXPECT_FALSE(future.IsReady());
  EXPECT_THAT(
      histogram_tester_.GetAllSamples(
          kPlusAddressModalWithNoticeEventHistogram),
      BucketsAre(
          base::Bucket(metrics::PlusAddressModalEvent::kModalShown, 1),
          base::Bucket(metrics::PlusAddressModalEvent::kModalCanceled, 1)));
  histogram_tester_.ExpectUniqueTimeSample(
      FormatModalDurationMetrics(
          metrics::PlusAddressModalCompletionStatus::kModalCanceled,
          /*notice_shown=*/true),
      kDuration, 1);
  histogram_tester_.ExpectUniqueSample(
      FormatRefreshHistogramNameFor(
          metrics::PlusAddressModalCompletionStatus::kModalCanceled,
          /*notice_shown=*/true),
      0, 1);
}

// Tests that the dialog can still be accepted after an error occurs and the
// user tries again to create the plus address.
TEST_F(PlusAddressCreationControllerAndroidEnabledTest,
       AcceptAfterErrorWithNotice) {
  base::UserActionTester user_action_tester;

  ON_CALL(plus_address_setting_service(), GetHasAcceptedNotice)
      .WillByDefault(Return(false));
  EXPECT_CALL(plus_address_setting_service(), SetHasAcceptedNotice);

  controller().set_suppress_ui_for_testing(true);
  base::test::TestFuture<const std::string&> future;
  controller().OfferCreation(
      url::Origin::Create(GURL("https://mattwashere.example")),
      /*is_manual_fallback=*/false, future.GetCallback());

  plus_address_service().set_should_fail_to_confirm(true);
  FastForwardBy(kDuration);
  controller().OnConfirmed();
  EXPECT_FALSE(future.IsReady());

  plus_address_service().set_should_fail_to_confirm(false);
  FastForwardBy(kDuration);
  EXPECT_CALL(autofill_client(),
              TriggerPlusAddressUserPerceptionSurvey(
                  plus_addresses::hats::SurveyType::kAcceptedFirstTimeCreate));
  controller().OnConfirmed();
  EXPECT_EQ(user_action_tester.GetActionCount(
                "PlusAddresses.CreateErrorTryAgainClicked"),
            1);
  EXPECT_TRUE(future.IsReady());
  EXPECT_THAT(
      histogram_tester_.GetAllSamples(
          kPlusAddressModalWithNoticeEventHistogram),
      BucketsAre(
          base::Bucket(metrics::PlusAddressModalEvent::kModalShown, 1),
          base::Bucket(metrics::PlusAddressModalEvent::kModalConfirmed, 2)));
  histogram_tester_.ExpectUniqueTimeSample(
      FormatModalDurationMetrics(
          metrics::PlusAddressModalCompletionStatus::kModalConfirmed,
          /*notice_shown=*/true),
      2 * kDuration, 1);
  histogram_tester_.ExpectUniqueSample(
      FormatRefreshHistogramNameFor(
          metrics::PlusAddressModalCompletionStatus::kModalConfirmed,
          /*notice_shown=*/true),
      0, 1);
  // The pref is set only when the first time onboarding notice is shown.
  EXPECT_EQ(profile()->GetTestingPrefService()->GetTime(
                prefs::kFirstPlusAddressCreationTime),
            base::Time::Now());
}

TEST_F(PlusAddressCreationControllerAndroidEnabledTest, RefreshPlusAddress) {
  base::UserActionTester user_action_tester;
  controller().set_suppress_ui_for_testing(true);
  base::test::TestFuture<const std::string&> future;
  controller().OfferCreation(
      url::Origin::Create(GURL("https://mattwashere.example")),
      /*is_manual_fallback=*/false, future.GetCallback());
  controller().OnRefreshClicked();
  EXPECT_EQ(user_action_tester.GetActionCount("PlusAddresses.Refreshed"), 1);
  FastForwardBy(kDuration);
  controller().OnConfirmed();
  EXPECT_TRUE(future.IsReady());
  EXPECT_THAT(
      histogram_tester_.GetAllSamples(kPlusAddressModalEventHistogram),
      BucketsAre(
          base::Bucket(metrics::PlusAddressModalEvent::kModalShown, 1),
          base::Bucket(metrics::PlusAddressModalEvent::kModalConfirmed, 1)));
  histogram_tester_.ExpectUniqueTimeSample(
      FormatModalDurationMetrics(
          metrics::PlusAddressModalCompletionStatus::kModalConfirmed,
          /*notice_shown=*/false),
      kDuration, 1);
  histogram_tester_.ExpectUniqueSample(
      FormatRefreshHistogramNameFor(
          metrics::PlusAddressModalCompletionStatus::kModalConfirmed,
          /*notice_shown=*/false),
      1, 1);
}

TEST_F(PlusAddressCreationControllerAndroidEnabledTest, OnConfirmedError) {
  base::UserActionTester user_action_tester;
  controller().set_suppress_ui_for_testing(true);
  base::test::TestFuture<const std::string&> future;
  controller().OfferCreation(
      url::Origin::Create(GURL("https://mattwashere.example")),
      /*is_manual_fallback=*/false, future.GetCallback());

  plus_address_service().set_should_fail_to_confirm(true);
  FastForwardBy(kDuration);
  EXPECT_CALL(autofill_client(), TriggerPlusAddressUserPerceptionSurvey)
      .Times(0);
  controller().OnConfirmed();
  EXPECT_FALSE(future.IsReady());

  // When `ConfirmPlusAddress` fails, `OnCanceled` may be called after
  // `OnConfirmed`.
  controller().OnCanceled();

  // Ensure that plus address can be canceled after erroneous confirm event and
  // metric is recorded.
  EXPECT_THAT(
      histogram_tester_.GetAllSamples(kPlusAddressModalEventHistogram),
      BucketsAre(
          base::Bucket(metrics::PlusAddressModalEvent::kModalShown, 1),
          base::Bucket(metrics::PlusAddressModalEvent::kModalConfirmed, 1),
          base::Bucket(metrics::PlusAddressModalEvent::kModalCanceled, 1)));
  histogram_tester_.ExpectUniqueTimeSample(
      FormatModalDurationMetrics(
          metrics::PlusAddressModalCompletionStatus::kConfirmPlusAddressError,
          /*notice_shown=*/false),
      kDuration, 1);
  histogram_tester_.ExpectUniqueSample(
      FormatRefreshHistogramNameFor(
          metrics::PlusAddressModalCompletionStatus::kConfirmPlusAddressError,
          /*notice_shown=*/false),
      0, 1);
  EXPECT_EQ(
      user_action_tester.GetActionCount("PlusAddresses.CreateErrorCanceled"),
      1);
}

TEST_F(PlusAddressCreationControllerAndroidEnabledTest, OnReservedError) {
  base::UserActionTester user_action_tester;
  controller().set_suppress_ui_for_testing(true);
  base::test::TestFuture<const std::string&> future;

  plus_address_service().set_should_fail_to_reserve(true);

  controller().OfferCreation(
      url::Origin::Create(GURL("https://mattwashere.example")),
      /*is_manual_fallback=*/false, future.GetCallback());
  FastForwardBy(kDuration);
  EXPECT_CALL(autofill_client(), TriggerPlusAddressUserPerceptionSurvey)
      .Times(0);
  controller().OnCanceled();

  // Ensure that plus address can be canceled after erroneous reserve event and
  // metric is recorded.
  EXPECT_THAT(
      histogram_tester_.GetAllSamples(kPlusAddressModalEventHistogram),
      BucketsAre(
          base::Bucket(metrics::PlusAddressModalEvent::kModalShown, 1),
          base::Bucket(metrics::PlusAddressModalEvent::kModalCanceled, 1)));
  histogram_tester_.ExpectUniqueTimeSample(
      FormatModalDurationMetrics(
          metrics::PlusAddressModalCompletionStatus::kReservePlusAddressError,
          /*notice_shown=*/false),
      kDuration, 1);
  histogram_tester_.ExpectUniqueSample(
      FormatRefreshHistogramNameFor(
          metrics::PlusAddressModalCompletionStatus::kReservePlusAddressError,
          /*notice_shown=*/false),
      0, 1);
  EXPECT_EQ(
      user_action_tester.GetActionCount("PlusAddresses.ReserveErrorCanceled"),
      1);
}

TEST_F(PlusAddressCreationControllerAndroidEnabledTest, TriesAgainToReserve) {
  base::UserActionTester user_action_tester;

  controller().set_suppress_ui_for_testing(true);
  plus_address_service().set_should_fail_to_reserve(true);

  controller().TryAgainToReservePlusAddress();
  EXPECT_EQ(user_action_tester.GetActionCount(
                "PlusAddresses.ReserveErrorTryAgainClicked"),
            1);
}

TEST_F(PlusAddressCreationControllerAndroidEnabledTest,
       CancelAfterAffiliationError) {
  base::UserActionTester user_action_tester;
  controller().set_suppress_ui_for_testing(true);

  base::test::TestFuture<const std::string&> future;
  controller().OfferCreation(
      url::Origin::Create(GURL("https://timofeywashere.example")),
      /*is_manual_fallback=*/false, future.GetCallback());
  ASSERT_FALSE(future.IsReady());

  plus_address_service().set_should_return_affiliated_plus_profile_on_confirm(
      true);
  task_environment()->FastForwardBy(kDuration);
  controller().OnConfirmed();
  EXPECT_FALSE(future.IsReady());

  task_environment()->FastForwardBy(kDuration);

  controller().OnCanceled();
  EXPECT_FALSE(future.IsReady());
  EXPECT_EQ(user_action_tester.GetActionCount(
                "PlusAddresses.AffiliationErrorCanceled"),
            1);
}

TEST_F(PlusAddressCreationControllerAndroidEnabledTest,
       AcceptedAfterAffiliationError) {
  base::UserActionTester user_action_tester;
  controller().set_suppress_ui_for_testing(true);

  base::test::TestFuture<const std::string&> future;
  controller().OfferCreation(
      url::Origin::Create(GURL("https://timofeywashere.example")),
      /*is_manual_fallback=*/false, future.GetCallback());
  ASSERT_FALSE(future.IsReady());

  plus_address_service().set_should_return_affiliated_plus_profile_on_confirm(
      true);
  task_environment()->FastForwardBy(kDuration);
  controller().OnConfirmed();
  EXPECT_FALSE(future.IsReady());

  task_environment()->FastForwardBy(kDuration);

  controller().OnConfirmed();
  EXPECT_TRUE(future.IsReady());
  EXPECT_EQ(user_action_tester.GetActionCount(
                "PlusAddresses.AffiliationErrorFilledExisting"),
            1);
}

TEST_F(PlusAddressCreationControllerAndroidEnabledTest,
       AcceptedAfterQuotaError) {
  base::UserActionTester user_action_tester;
  controller().set_suppress_ui_for_testing(true);

  base::test::TestFuture<const std::string&> future;
  controller().OfferCreation(
      url::Origin::Create(GURL("https://timofeywashere.example")),
      /*is_manual_fallback=*/false, future.GetCallback());
  ASSERT_FALSE(future.IsReady());

  plus_address_service().set_should_return_quota_error(true);
  task_environment()->FastForwardBy(kDuration);
  controller().OnConfirmed();
  EXPECT_FALSE(future.IsReady());

  task_environment()->FastForwardBy(kDuration);
  controller().OnCanceled();
  ASSERT_FALSE(future.IsReady());
  EXPECT_EQ(
      user_action_tester.GetActionCount("PlusAddresses.QuotaErrorAccepted"), 1);
}

TEST_F(PlusAddressCreationControllerAndroidEnabledTest,
       StoredPlusProfileClearedOnDialogDestroyed) {
  controller().set_suppress_ui_for_testing(true);

  EXPECT_FALSE(controller().get_plus_profile_for_testing().has_value());
  // Offering creation calls Reserve() and sets the profile.
  controller().OfferCreation(url::Origin::Create(GURL("https://foo.example")),
                             /*is_manual_fallback=*/false, base::DoNothing());
  // Destroying the modal clears the profile
  EXPECT_TRUE(controller().get_plus_profile_for_testing().has_value());
  controller().OnDialogDestroyed();
  EXPECT_FALSE(controller().get_plus_profile_for_testing().has_value());
}

TEST_F(PlusAddressCreationControllerAndroidEnabledTest, ModalCanceled) {
  base::UserActionTester user_action_tester;
  controller().set_suppress_ui_for_testing(true);

  base::test::TestFuture<const std::string&> future;
  controller().OfferCreation(
      url::Origin::Create(GURL("https://mattwashere.example")),
      /*is_manual_fallback=*/false, future.GetCallback());
  FastForwardBy(kDuration);
  EXPECT_CALL(autofill_client(), TriggerPlusAddressUserPerceptionSurvey)
      .Times(0);
  controller().OnCanceled();
  EXPECT_FALSE(future.IsReady());
  EXPECT_THAT(
      histogram_tester_.GetAllSamples(kPlusAddressModalEventHistogram),
      BucketsAre(
          base::Bucket(metrics::PlusAddressModalEvent::kModalShown, 1),
          base::Bucket(metrics::PlusAddressModalEvent::kModalCanceled, 1)));
  histogram_tester_.ExpectUniqueTimeSample(
      FormatModalDurationMetrics(
          metrics::PlusAddressModalCompletionStatus::kModalCanceled,
          /*notice_shown=*/false),
      kDuration, 1);
  histogram_tester_.ExpectUniqueSample(
      FormatRefreshHistogramNameFor(
          metrics::PlusAddressModalCompletionStatus::kModalCanceled,
          /*notice_shown=*/false),
      0, 1);
  EXPECT_EQ(user_action_tester.GetActionCount(
                "PlusAddresses.OfferedPlusAddressDeclined"),
            1);
}

TEST_F(PlusAddressCreationControllerAndroidEnabledTest,
       ReserveGivesConfirmedAddress_DoesntCallService) {
  controller().set_suppress_ui_for_testing(true);

  // Setup fake service behavior.
  base::test::TestFuture<const PlusProfileOrError&> confirm_future;
  plus_address_service().set_is_confirmed(true);
  plus_address_service().set_confirm_callback(confirm_future.GetCallback());

  base::test::TestFuture<const std::string&> autofill_future;
  controller().OfferCreation(
      url::Origin::Create(GURL("https://kirubelwashere.example")),
      /*is_manual_fallback=*/false, autofill_future.GetCallback());
  ASSERT_FALSE(autofill_future.IsReady());

  // Confirmation should fill the field, but not call ConfirmPlusAddress.
  FastForwardBy(kDuration);
  controller().OnConfirmed();
  EXPECT_TRUE(autofill_future.IsReady());
  EXPECT_FALSE(confirm_future.IsReady());

  // Verify that the plus address modal is still shown when this happens.
  EXPECT_THAT(
      histogram_tester_.GetAllSamples(kPlusAddressModalEventHistogram),
      BucketsAre(
          base::Bucket(metrics::PlusAddressModalEvent::kModalShown, 1),
          base::Bucket(metrics::PlusAddressModalEvent::kModalConfirmed, 1)));
  histogram_tester_.ExpectUniqueTimeSample(
      FormatModalDurationMetrics(
          metrics::PlusAddressModalCompletionStatus::kModalConfirmed,
          /*notice_shown=*/false),
      kDuration, 1);
  histogram_tester_.ExpectUniqueSample(
      FormatRefreshHistogramNameFor(
          metrics::PlusAddressModalCompletionStatus::kModalConfirmed,
          /*notice_shown=*/false),
      0, 1);
}
// With the feature disabled, the `KeyedService` is not present; ensure this is
// handled. While this code path should not be called in that case, it is
// validated here for safety.
class PlusAddressCreationControllerAndroidDisabledTest
    : public ChromeRenderViewHostTestHarness {
 public:
  void SetUp() override {
    features_.InitAndDisableFeature(features::kPlusAddressesEnabled);
    ChromeRenderViewHostTestHarness::SetUp();
    PlusAddressServiceFactory::GetInstance()->SetTestingFactory(
        browser_context(),
        base::BindRepeating(
            [](content::BrowserContext* profile)
                -> std::unique_ptr<KeyedService> { return nullptr; }));

    PlusAddressCreationControllerAndroid::CreateForWebContents(web_contents());
  }

  PlusAddressCreationControllerAndroid& controller() {
    return *PlusAddressCreationControllerAndroid::FromWebContents(
        web_contents());
  }

 private:
  base::test::ScopedFeatureList features_;
};

TEST_F(PlusAddressCreationControllerAndroidDisabledTest, ConfirmedNullService) {
  controller().set_suppress_ui_for_testing(true);

  base::test::TestFuture<const std::string&> future;
  controller().OfferCreation(url::Origin::Create(GURL("https://test.example")),
                             /*is_manual_fallback=*/false,
                             future.GetCallback());
  EXPECT_CHECK_DEATH(controller().OnConfirmed());
  EXPECT_FALSE(future.IsReady());
}

}  // namespace
}  // namespace plus_addresses
