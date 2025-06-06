// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/metrics/quality_metrics.h"

#include <optional>

#include "base/base64.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/metrics/user_action_tester.h"
#include "components/autofill/core/browser/data_manager/addresses/address_data_manager.h"
#include "components/autofill/core/browser/form_structure.h"
#include "components/autofill/core/browser/foundations/browser_autofill_manager_test_api.h"
#include "components/autofill/core/browser/metrics/autofill_metrics_test_base.h"
#include "components/autofill/core/browser/metrics/prediction_quality_metrics.h"
#include "components/autofill/core/browser/metrics/ukm_metrics_test_utils.h"
#include "components/autofill/core/browser/test_utils/autofill_form_test_utils.h"
#include "components/autofill/core/browser/test_utils/autofill_test_utils.h"
#include "components/autofill/core/common/form_data_test_api.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace autofill::autofill_metrics {

// This is defined in the prediction_quality_metrics.cc implementation file.
int GetFieldTypeGroupPredictionQualityMetric(FieldType field_type,
                                             FieldTypeQualityMetric metric);

namespace {

using ::autofill::test::AddFieldPredictionToForm;
using ::autofill::test::CreateTestFormField;
using ::base::Bucket;
using ::base::BucketsAre;
using ::base::BucketsInclude;

using ExpectedUkmMetricsRecord = std::vector<ExpectedUkmMetricsPair>;
using ExpectedUkmMetrics = std::vector<ExpectedUkmMetricsRecord>;
using UkmFieldTypeValidationType = ukm::builders::Autofill_FieldTypeValidation;

std::string SerializeAndEncode(const AutofillQueryResponse& response) {
  std::string unencoded_response_string;
  if (!response.SerializeToString(&unencoded_response_string)) {
    LOG(ERROR) << "Cannot serialize the response proto";
    return "";
  }
  return base::Base64Encode(unencoded_response_string);
}

}  // namespace
// The anonymous namespace needs to end here because of `friend`ships between
// the tests and the production code.

class QualityMetricsTest : public AutofillMetricsBaseTest,
                           public testing::Test {
 public:
  void SetUp() override { SetUpHelper(); }
  void TearDown() override { TearDownHelper(); }
};

// Test that we log quality metrics appropriately.
TEST_F(QualityMetricsTest, QualityMetrics) {
  // Set up our form data.
  test::FormDescription form_description = {
      .description_for_logging = "QualityMetrics",
      .fields = {{.role = NAME_FIRST,
                  .heuristic_type = NAME_FULL,
                  .value = u"Elvis Aaron Presley",
                  .is_autofilled = true},
                 {.role = EMAIL_ADDRESS,
                  .heuristic_type = PHONE_HOME_NUMBER,
                  .value = u"buddy@gmail.com",
                  .is_autofilled = false},
                 {.role = NAME_FIRST,
                  .heuristic_type = NAME_FULL,
                  .value = u"",
                  .is_autofilled = false},
                 {.role = EMAIL_ADDRESS,
                  .heuristic_type = PHONE_HOME_NUMBER,
                  .value = u"garbage",
                  .is_autofilled = false},
                 {.role = NO_SERVER_DATA,
                  .heuristic_type = UNKNOWN_TYPE,
                  .value = u"USA",
                  .form_control_type = FormControlType::kSelectOne,
                  .is_autofilled = false},
                 {.role = PHONE_HOME_CITY_AND_NUMBER,
                  .heuristic_type = PHONE_HOME_CITY_AND_NUMBER,
                  .value = u"2345678901",
                  .form_control_type = FormControlType::kInputTelephone,
                  .is_autofilled = true}},
      .renderer_id = test::MakeFormRendererId(),
      .main_frame_origin = url::Origin::Create(autofill_driver_->url())};

  std::vector<FieldType> heuristic_types = {
      NAME_FULL,         PHONE_HOME_NUMBER, NAME_FULL,
      PHONE_HOME_NUMBER, UNKNOWN_TYPE,      PHONE_HOME_CITY_AND_NUMBER};
  std::vector<FieldType> server_types = {
      NAME_FIRST,    EMAIL_ADDRESS,  NAME_FIRST,
      EMAIL_ADDRESS, NO_SERVER_DATA, PHONE_HOME_CITY_AND_NUMBER};

  FormData form = GetAndAddSeenForm(form_description);

  base::HistogramTester histogram_tester;
  SubmitForm(form);

  // Auxiliary function for GetAllSamples() expectations.
  auto b = [](FieldType field_type, FieldTypeQualityMetric metric,
              base::HistogramBase::Count32 count) {
    return Bucket(GetFieldTypeGroupPredictionQualityMetric(field_type, metric),
                  count);
  };

  // Heuristic predictions.
  EXPECT_THAT(
      histogram_tester.GetAllSamples(
          "Autofill.FieldPredictionQuality.Aggregate.Heuristic"),
      BucketsAre(Bucket(FALSE_NEGATIVE_UNKNOWN, 1), Bucket(TRUE_POSITIVE, 2),
                 Bucket(FALSE_POSITIVE_EMPTY, 1),
                 Bucket(FALSE_POSITIVE_UNKNOWN, 1),
                 Bucket(FALSE_NEGATIVE_MISMATCH, 1)));
  EXPECT_THAT(histogram_tester.GetAllSamples(
                  "Autofill.FieldPredictionQuality.ByFieldType.Heuristic"),
              BucketsAre(b(ADDRESS_HOME_COUNTRY, FALSE_NEGATIVE_UNKNOWN, 1),
                         b(NAME_FULL, TRUE_POSITIVE, 1),
                         b(PHONE_HOME_CITY_AND_NUMBER, TRUE_POSITIVE, 1),
                         b(EMAIL_ADDRESS, FALSE_NEGATIVE_MISMATCH, 1),
                         b(PHONE_HOME_NUMBER, FALSE_POSITIVE_MISMATCH, 1),
                         b(PHONE_HOME_NUMBER, FALSE_POSITIVE_UNKNOWN, 1),
                         b(NAME_FULL, FALSE_POSITIVE_EMPTY, 1)));

  EXPECT_THAT(
      histogram_tester.GetAllSamples(
          "Autofill.FieldPredictionQuality.Aggregate.Server"),
      BucketsAre(Bucket(FALSE_NEGATIVE_UNKNOWN, 1), Bucket(TRUE_POSITIVE, 2),
                 Bucket(FALSE_NEGATIVE_MISMATCH, 1),
                 Bucket(FALSE_POSITIVE_UNKNOWN, 1),
                 Bucket(FALSE_POSITIVE_EMPTY, 1)));

  EXPECT_THAT(histogram_tester.GetAllSamples(
                  "Autofill.FieldPredictionQuality.ByFieldType.Server"),
              BucketsAre(b(ADDRESS_HOME_COUNTRY, FALSE_NEGATIVE_UNKNOWN, 1),
                         b(EMAIL_ADDRESS, TRUE_POSITIVE, 1),
                         b(PHONE_HOME_WHOLE_NUMBER, TRUE_POSITIVE, 1),
                         b(NAME_FULL, FALSE_NEGATIVE_MISMATCH, 1),
                         b(NAME_FIRST, FALSE_POSITIVE_MISMATCH, 1),
                         b(EMAIL_ADDRESS, FALSE_POSITIVE_UNKNOWN, 1),
                         b(NAME_FIRST, FALSE_POSITIVE_EMPTY, 1)));

  // Server overrides heuristic so Overall and Server are the same predictions
  // (as there were no test fields where server == NO_SERVER_DATA and heuristic
  // != UNKNOWN_TYPE).
  EXPECT_EQ(histogram_tester.GetAllSamples(
                "Autofill.FieldPredictionQuality.Aggregate.Server"),
            histogram_tester.GetAllSamples(
                "Autofill.FieldPredictionQuality.Aggregate.Overall"));
  EXPECT_EQ(histogram_tester.GetAllSamples(
                "Autofill.FieldPredictionQuality.ByFieldType.Server"),
            histogram_tester.GetAllSamples(
                "Autofill.FieldPredictionQuality.ByFieldType.Overall"));
}

struct AlternativeNameFieldValueCharacterSetTestRecord {
  std::u16string name;
  std::optional<AutofillAlternativeNameFieldValueCharacterSet>
      expected_character_set;
};

class AlternativeNameFieldValueCharacterSetTest
    : public QualityMetricsTest,
      public testing::WithParamInterface<
          AlternativeNameFieldValueCharacterSetTestRecord> {};

// Test that the metric for the alternative name field value character set is
// recorded correctly.
TEST_P(AlternativeNameFieldValueCharacterSetTest, LoggedCorrectly) {
  base::test::ScopedFeatureList features{
      autofill::features::kAutofillSupportPhoneticNameForJP};

  test::FormDescription form_description = {
      .fields = {{.role = ALTERNATIVE_FULL_NAME,
                  .value = GetParam().name,
                  .is_autofilled = true}},
      .renderer_id = test::MakeFormRendererId(),
      .main_frame_origin = url::Origin::Create(autofill_driver_->url())};

  FormData form = GetAndAddSeenForm(form_description);

  base::HistogramTester histogram_tester;
  SubmitForm(form);

  if (GetParam().expected_character_set.has_value()) {
    // Check for the expected enum value in the histogram
    histogram_tester.ExpectUniqueSample(
        "Autofill.SubmittedAlternativeNameFieldValueCharacterSet",
        GetParam().expected_character_set.value(), 1);
  } else {
    histogram_tester.ExpectTotalCount(
        "Autofill.SubmittedAlternativeNameFieldValueCharacterSet", 0);
  }
}

INSTANTIATE_TEST_SUITE_P(
    LoggedCorrectly,
    AlternativeNameFieldValueCharacterSetTest,
    testing::Values(
        AlternativeNameFieldValueCharacterSetTestRecord{
            u"ヤマモト アオイ",
            AutofillAlternativeNameFieldValueCharacterSet::kKatakana},
        AlternativeNameFieldValueCharacterSetTestRecord{
            u"やまもと あおい",
            AutofillAlternativeNameFieldValueCharacterSet::kHiragana},
        AlternativeNameFieldValueCharacterSetTestRecord{
            u"Elvis Aaron Presley",
            AutofillAlternativeNameFieldValueCharacterSet::kOther},
        // If value was empty metric should not be recorded.
        AlternativeNameFieldValueCharacterSetTestRecord{u""}));

// Test that we log quality metrics appropriately with fields having
// only_fill_when_focused and are supposed to log RATIONALIZATION_OK.
TEST_F(QualityMetricsTest, LoggedCorrectlyForRationalizationOk) {
  FormData form = CreateForm(
      {CreateTestFormField("Name", "name", "Elvis Aaron Presley",
                           FormControlType::kInputText),
       CreateTestFormField("Address", "address", "3734 Elvis Presley Blvd.",
                           FormControlType::kInputText),
       CreateTestFormField("Phone", "phone", "2345678901",
                           FormControlType::kInputText),
       // RATIONALIZATION_OK because it's ambiguous value.
       CreateTestFormField("Phone1", "phone1", "nonsense value",
                           FormControlType::kInputText),
       // RATIONALIZATION_OK because it's same type but different to what is in
       // the profile.
       CreateTestFormField("Phone2", "phone2", "2345678902",
                           FormControlType::kInputText),
       // RATIONALIZATION_OK because it's a type mismatch.
       CreateTestFormField("Phone3", "phone3", "Elvis Aaron Presley",
                           FormControlType::kInputText)});
  test_api(form).field(2).set_is_autofilled(true);

  std::vector<FieldType> heuristic_types = {NAME_FULL,
                                            ADDRESS_HOME_LINE1,
                                            PHONE_HOME_CITY_AND_NUMBER,
                                            PHONE_HOME_CITY_AND_NUMBER,
                                            PHONE_HOME_CITY_AND_NUMBER,
                                            PHONE_HOME_CITY_AND_NUMBER};
  std::vector<FieldType> server_types = {NAME_FULL,
                                         ADDRESS_HOME_LINE1,
                                         PHONE_HOME_CITY_AND_NUMBER,
                                         PHONE_HOME_WHOLE_NUMBER,
                                         PHONE_HOME_CITY_AND_NUMBER,
                                         PHONE_HOME_WHOLE_NUMBER};

  base::UserActionTester user_action_tester;
  autofill_manager().AddSeenForm(form, heuristic_types, server_types);
  FormStructure* form_structure =
      autofill_manager().FindCachedFormById(form.global_id());
  ASSERT_TRUE(form_structure);
  form_structure->RationalizePhoneNumberFieldsForFilling();

  base::HistogramTester histogram_tester;
  SubmitForm(form);

  // Rationalization quality.
  {
    std::string rationalization_histogram =
        "Autofill.Rationalization.OnlyFillWhenFocused.Quality";
    histogram_tester.ExpectBucketCount(rationalization_histogram,
                                       RATIONALIZATION_OK, 3);
  }
}

// Test that we log quality metrics appropriately with fields having
// only_fill_when_focused and are supposed to log RATIONALIZATION_GOOD.
TEST_F(QualityMetricsTest, LoggedCorrectlyForRationalizationGood) {
  FormData form = CreateForm(
      {CreateTestFormField("Name", "name", "Elvis Aaron Presley",
                           FormControlType::kInputText),
       CreateTestFormField("Address", "address", "3734 Elvis Presley Blvd.",
                           FormControlType::kInputText),
       CreateTestFormField("Phone", "phone", "2345678901",
                           FormControlType::kInputText),
       // RATIONALIZATION_GOOD because it's empty.
       CreateTestFormField("Phone1", "phone1", "",
                           FormControlType::kInputText)});
  test_api(form).field(2).set_is_autofilled(true);

  std::vector<FieldType> field_types = {NAME_FULL, ADDRESS_HOME_LINE1,
                                        PHONE_HOME_CITY_AND_NUMBER,
                                        PHONE_HOME_CITY_AND_NUMBER};

  base::UserActionTester user_action_tester;
  autofill_manager().AddSeenForm(form, field_types);
  FormStructure* form_structure =
      autofill_manager().FindCachedFormById(form.global_id());
  ASSERT_TRUE(form_structure);
  form_structure->RationalizePhoneNumberFieldsForFilling();

  base::HistogramTester histogram_tester;
  SubmitForm(form);

  // Rationalization quality.
  {
    std::string rationalization_histogram =
        "Autofill.Rationalization.OnlyFillWhenFocused.Quality";
    histogram_tester.ExpectBucketCount(rationalization_histogram,
                                       RATIONALIZATION_GOOD, 1);
  }
}

// Test that we log quality metrics appropriately with fields having
// only_fill_when_focused and are supposed to log RATIONALIZATION_BAD.
TEST_F(QualityMetricsTest, LoggedCorrectlyForRationalizationBad) {
  FormData form = CreateForm({
      CreateTestFormField("Name", "name", "Elvis Aaron Presley",
                          FormControlType::kInputText),
      CreateTestFormField("Address", "address", "3734 Elvis Presley Blvd.",
                          FormControlType::kInputText),
      CreateTestFormField("Phone", "phone", "12345678901",
                          FormControlType::kInputText),
      // RATIONALIZATION_BAD because it's filled with the same value as filled
      // previously.
      CreateTestFormField("Phone1", "phone1", "12345678901",
                          FormControlType::kInputText),
  });
  test_api(form).field(2).set_is_autofilled(true);

  std::vector<FieldType> heuristic_types = {NAME_FULL, ADDRESS_HOME_LINE1,
                                            PHONE_HOME_CITY_AND_NUMBER,
                                            PHONE_HOME_CITY_AND_NUMBER};
  std::vector<FieldType> server_types = {NAME_FULL, ADDRESS_HOME_LINE1,
                                         PHONE_HOME_CITY_AND_NUMBER,
                                         PHONE_HOME_WHOLE_NUMBER};

  base::UserActionTester user_action_tester;
  autofill_manager().AddSeenForm(form, heuristic_types, server_types);
  FormStructure* form_structure =
      autofill_manager().FindCachedFormById(form.global_id());
  ASSERT_TRUE(form_structure);
  form_structure->RationalizePhoneNumberFieldsForFilling();

  base::HistogramTester histogram_tester;
  SubmitForm(form);

  histogram_tester.ExpectBucketCount(
      "Autofill.Rationalization.OnlyFillWhenFocused.Quality",
      RATIONALIZATION_BAD, 1);
}

// Test that we log quality metrics appropriately with fields having
// only_fill_when_focused set to true.
TEST_F(QualityMetricsTest, LoggedCorrectlyForOnlyFillWhenFocusedField) {
  FormData form = CreateForm(
      {// TRUE_POSITIVE + no rationalization logging
       CreateTestFormField("Name", "name", "Elvis Aaron Presley",
                           FormControlType::kInputText),
       // TRUE_POSITIVE + no rationalization logging
       CreateTestFormField("Address", "address", "3734 Elvis Presley Blvd.",
                           FormControlType::kInputText),
       // TRUE_POSITIVE + no rationalization logging
       CreateTestFormField("Phone", "phone", "2345678901",
                           FormControlType::kInputText),
       // TRUE_NEGATIVE_EMPTY + RATIONALIZATION_GOOD
       CreateTestFormField("Phone1", "phone1", "", FormControlType::kInputText),
       // TRUE_POSITIVE + RATIONALIZATION_BAD
       CreateTestFormField("Phone2", "phone2", "2345678901",
                           FormControlType::kInputText),
       // FALSE_NEGATIVE_MISMATCH + RATIONALIZATION_OK
       CreateTestFormField("Phone3", "phone3", "Elvis Aaron Presley",
                           FormControlType::kInputText)});
  test_api(form).field(2).set_is_autofilled(true);

  std::vector<FieldType> heuristic_types = {NAME_FULL,
                                            ADDRESS_HOME_LINE1,
                                            PHONE_HOME_CITY_AND_NUMBER,
                                            PHONE_HOME_CITY_AND_NUMBER,
                                            PHONE_HOME_CITY_AND_NUMBER,
                                            PHONE_HOME_CITY_AND_NUMBER};
  std::vector<FieldType> server_types = {NAME_FULL,
                                         ADDRESS_HOME_LINE1,
                                         PHONE_HOME_CITY_AND_NUMBER,
                                         PHONE_HOME_WHOLE_NUMBER,
                                         PHONE_HOME_CITY_AND_NUMBER,
                                         PHONE_HOME_WHOLE_NUMBER};

  base::UserActionTester user_action_tester;
  autofill_manager().AddSeenForm(form, heuristic_types, server_types);
  FormStructure* form_structure =
      autofill_manager().FindCachedFormById(form.global_id());
  ASSERT_TRUE(form_structure);
  form_structure->RationalizePhoneNumberFieldsForFilling();

  base::HistogramTester histogram_tester;
  SubmitForm(form);

  // Auxiliary function for GetAllSamples() expectations.
  auto b = [](FieldType field_type, FieldTypeQualityMetric metric,
              base::HistogramBase::Count32 count) {
    return Bucket(GetFieldTypeGroupPredictionQualityMetric(field_type, metric),
                  count);
  };

  // Rationalization quality.
  EXPECT_THAT(
      histogram_tester.GetAllSamples(
          "Autofill.Rationalization.OnlyFillWhenFocused.Quality"),
      BucketsAre(Bucket(RATIONALIZATION_GOOD, 1), Bucket(RATIONALIZATION_OK, 1),
                 Bucket(RATIONALIZATION_BAD, 1)));

  EXPECT_THAT(
      histogram_tester.GetAllSamples(
          "Autofill.FieldPredictionQuality.Aggregate.Heuristic"),
      BucketsAre(Bucket(TRUE_POSITIVE, 4), Bucket(TRUE_NEGATIVE_EMPTY, 1),
                 Bucket(FALSE_NEGATIVE_MISMATCH, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples(
          "Autofill.FieldPredictionQuality.ByFieldType.Heuristic"),
      BucketsAre(b(NAME_FULL, TRUE_POSITIVE, 1),
                 b(ADDRESS_HOME_LINE1, TRUE_POSITIVE, 1),
                 b(PHONE_HOME_CITY_AND_NUMBER, TRUE_POSITIVE, 2),
                 b(PHONE_HOME_WHOLE_NUMBER, FALSE_NEGATIVE_MISMATCH, 1)));

  EXPECT_THAT(
      histogram_tester.GetAllSamples(
          "Autofill.FieldPredictionQuality.Aggregate.Server"),
      BucketsAre(Bucket(TRUE_POSITIVE, 4), Bucket(TRUE_NEGATIVE_EMPTY, 1),
                 Bucket(FALSE_NEGATIVE_MISMATCH, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples(
          "Autofill.FieldPredictionQuality.ByFieldType.Server"),
      BucketsAre(b(NAME_FULL, TRUE_POSITIVE, 1),
                 b(ADDRESS_HOME_LINE1, TRUE_POSITIVE, 1),
                 b(PHONE_HOME_CITY_AND_NUMBER, TRUE_POSITIVE, 2),
                 b(PHONE_HOME_WHOLE_NUMBER, FALSE_NEGATIVE_MISMATCH, 1)));

  // Server overrides heuristic so Overall and Server are the same predictions
  // (as there were no test fields where server == NO_SERVER_DATA and heuristic
  // != UNKNOWN_TYPE).
  EXPECT_EQ(histogram_tester.GetAllSamples(
                "Autofill.FieldPredictionQuality.Aggregate.Server"),
            histogram_tester.GetAllSamples(
                "Autofill.FieldPredictionQuality.Aggregate.Overall"));
  EXPECT_EQ(histogram_tester.GetAllSamples(
                "Autofill.FieldPredictionQuality.FieldType.Server"),
            histogram_tester.GetAllSamples(
                "Autofill.FieldPredictionQuality.FieldType.Overall"));
}

// Tests the true negatives (empty + no prediction and unknown + no prediction)
// and false positives (empty + bad prediction and unknown + bad prediction)
// are counted correctly.

struct PredictionQualityMetricsTestCase {
  const FieldType predicted_field_type;
  const FieldType actual_field_type;
};

class PredictionQualityMetricsTest
    : public QualityMetricsTest,
      public testing::WithParamInterface<PredictionQualityMetricsTestCase> {
 public:
  const char* ValueForType(FieldType type) {
    switch (type) {
      case EMPTY_TYPE:
        return "";
      case NO_SERVER_DATA:
      case UNKNOWN_TYPE:
        return "unknown";
      case COMPANY_NAME:
        return "RCA";
      case NAME_FIRST:
        return "Elvis";
      case NAME_MIDDLE:
        return "Aaron";
      case NAME_LAST:
        return "Presley";
      case NAME_FULL:
        return "Elvis Aaron Presley";
      case EMAIL_ADDRESS:
        return "buddy@gmail.com";
      case PHONE_HOME_NUMBER:
      case PHONE_HOME_WHOLE_NUMBER:
      case PHONE_HOME_CITY_AND_NUMBER:
        return "2345678901";
      case ADDRESS_HOME_STREET_ADDRESS:
        return "123 Apple St.\nunit 6";
      case ADDRESS_HOME_LINE1:
        return "123 Apple St.";
      case ADDRESS_HOME_LINE2:
        return "unit 6";
      case ADDRESS_HOME_CITY:
        return "Lubbock";
      case ADDRESS_HOME_STATE:
        return "Texas";
      case ADDRESS_HOME_ZIP:
        return "79401";
      case ADDRESS_HOME_COUNTRY:
        return "US";
      case AMBIGUOUS_TYPE:
        // This occurs as both a company name and a middle name once ambiguous
        // profiles are created.
        CreateAmbiguousProfiles();
        return "Decca";

      default:
        NOTREACHED();
    }
  }

  bool IsExampleOf(FieldTypeQualityMetric metric,
                   FieldType predicted_type,
                   FieldType actual_type) {
    // The server can send either NO_SERVER_DATA or UNKNOWN_TYPE to indicate
    // that a field is not autofillable:
    //
    //   NO_SERVER_DATA
    //     => A type cannot be determined based on available data.
    //   UNKNOWN_TYPE
    //     => field is believed to not have an autofill type.
    //
    // Both of these are tabulated as "negative" predictions; so, to simplify
    // the logic below, map them both to UNKNOWN_TYPE.
    if (predicted_type == NO_SERVER_DATA) {
      predicted_type = UNKNOWN_TYPE;
    }
    switch (metric) {
      case TRUE_POSITIVE:
        return unknown_equivalent_types_.count(actual_type) == 0 &&
               predicted_type == actual_type;

      case TRUE_NEGATIVE_AMBIGUOUS:
        return actual_type == AMBIGUOUS_TYPE && predicted_type == UNKNOWN_TYPE;

      case TRUE_NEGATIVE_UNKNOWN:
        return actual_type == UNKNOWN_TYPE && predicted_type == UNKNOWN_TYPE;

      case TRUE_NEGATIVE_EMPTY:
        return actual_type == EMPTY_TYPE && predicted_type == UNKNOWN_TYPE;

      case FALSE_POSITIVE_AMBIGUOUS:
        return actual_type == AMBIGUOUS_TYPE && predicted_type != UNKNOWN_TYPE;

      case FALSE_POSITIVE_UNKNOWN:
        return actual_type == UNKNOWN_TYPE && predicted_type != UNKNOWN_TYPE;

      case FALSE_POSITIVE_EMPTY:
        return actual_type == EMPTY_TYPE && predicted_type != UNKNOWN_TYPE;

      // False negative mismatch and false positive mismatch trigger on the same
      // conditions:
      //   - False positive prediction of predicted type
      //   - False negative prediction of actual type
      case FALSE_POSITIVE_MISMATCH:
      case FALSE_NEGATIVE_MISMATCH:
        return unknown_equivalent_types_.count(actual_type) == 0 &&
               actual_type != predicted_type && predicted_type != UNKNOWN_TYPE;

      case FALSE_NEGATIVE_UNKNOWN:
        return unknown_equivalent_types_.count(actual_type) == 0 &&
               actual_type != predicted_type && predicted_type == UNKNOWN_TYPE;

      default:
        NOTREACHED();
    }
  }

  static int FieldTypeCross(FieldType predicted_type, FieldType actual_type) {
    EXPECT_LE(predicted_type, UINT16_MAX);
    EXPECT_LE(actual_type, UINT16_MAX);
    return (predicted_type << 16) | actual_type;
  }

  const FieldTypeSet unknown_equivalent_types_{UNKNOWN_TYPE, EMPTY_TYPE,
                                               AMBIGUOUS_TYPE};
};

TEST_P(PredictionQualityMetricsTest, Classification) {
  const std::vector<std::string> prediction_sources{"Heuristic", "Server",
                                                    "Overall"};
  // Setup the test parameters.
  FieldType actual_field_type = GetParam().actual_field_type;
  FieldType predicted_type = GetParam().predicted_field_type;

  DVLOG(2) << "Test Case = Predicted: " << FieldTypeToStringView(predicted_type)
           << "; "
           << "Actual: " << FieldTypeToStringView(actual_field_type);

  FormData form = CreateForm(
      {CreateTestFormField("first", "first", ValueForType(NAME_FIRST),
                           FormControlType::kInputText),
       CreateTestFormField("last", "last", ValueForType(NAME_LAST),
                           FormControlType::kInputText),
       CreateTestFormField("Unknown", "Unknown",
                           ValueForType(actual_field_type),
                           FormControlType::kInputText)});

  // Resolve any field type ambiguity.
  if (actual_field_type == AMBIGUOUS_TYPE) {
    if (predicted_type == COMPANY_NAME || predicted_type == NAME_MIDDLE) {
      actual_field_type = predicted_type;
    }
  }

  std::vector<FieldType> heuristic_types = {
      NAME_FIRST, NAME_LAST,
      predicted_type == NO_SERVER_DATA ? UNKNOWN_TYPE : predicted_type};
  std::vector<FieldType> server_types = {NAME_FIRST, NAME_LAST, predicted_type};
  std::vector<FieldType> actual_types = {NAME_FIRST, NAME_LAST,
                                         actual_field_type};

  autofill_manager().AddSeenForm(form, heuristic_types, server_types);

  // Run the form submission code while tracking the histograms.
  base::HistogramTester histogram_tester;
  SubmitForm(form);

  ExpectedUkmMetrics expected_ukm_metrics;
  AppendFieldTypeUkm(form, heuristic_types, server_types, actual_types,
                     &expected_ukm_metrics);
  VerifyUkm(&test_ukm_recorder(), form, UkmFieldTypeValidationType::kEntryName,
            expected_ukm_metrics);

  // Validate the total samples and the crossed (predicted-to-actual) samples.
  for (const auto& source : prediction_sources) {
    const std::string crossed_histogram = "Autofill.FieldPrediction." + source;
    const std::string aggregate_histogram =
        "Autofill.FieldPredictionQuality.Aggregate." + source;
    const std::string by_field_type_histogram =
        "Autofill.FieldPredictionQuality.ByFieldType." + source;

    // Sanity Check:
    histogram_tester.ExpectTotalCount(crossed_histogram, 3);
    histogram_tester.ExpectTotalCount(aggregate_histogram, 3);
    histogram_tester.ExpectTotalCount(
        by_field_type_histogram,
        2 +
            (predicted_type != UNKNOWN_TYPE &&
             predicted_type != NO_SERVER_DATA &&
             predicted_type != actual_field_type) +
            (unknown_equivalent_types_.count(actual_field_type) == 0));

    // The Crossed Histogram:
    EXPECT_THAT(histogram_tester.GetAllSamples(crossed_histogram),
                BucketsInclude(
                    Bucket(FieldTypeCross(NAME_FIRST, NAME_FIRST), 1),
                    Bucket(FieldTypeCross(NAME_LAST, NAME_LAST), 1),
                    Bucket(FieldTypeCross((predicted_type == NO_SERVER_DATA &&
                                                   source != "Server"
                                               ? UNKNOWN_TYPE
                                               : predicted_type),
                                          actual_field_type),
                           1)));
  }

  // Validate the individual histogram counter values.
  for (int i = 0; i < NUM_FIELD_TYPE_QUALITY_METRICS; ++i) {
    // The metric enum value we're currently examining.
    auto metric = static_cast<FieldTypeQualityMetric>(i);

    // The type specific expected count is 1 if (predicted, actual) is an
    // example
    int basic_expected_count =
        IsExampleOf(metric, predicted_type, actual_field_type) ? 1 : 0;

    // For aggregate metrics don't capture aggregate FALSE_POSITIVE_MISMATCH.
    // Note there are two true positive values (first and last name) hard-
    // coded into the test.
    int aggregate_expected_count =
        (metric == TRUE_POSITIVE ? 2 : 0) +
        (metric == FALSE_POSITIVE_MISMATCH ? 0 : basic_expected_count);

    // If this test exercises the ambiguous middle name match, then validation
    // of the name-specific metrics must include the true-positives created by
    // the first and last name fields.
    if (metric == TRUE_POSITIVE && predicted_type == NAME_MIDDLE &&
        actual_field_type == NAME_MIDDLE) {
      basic_expected_count += 2;
    }

    // For metrics keyed to the actual field type, we don't capture unknown,
    // empty or ambiguous and we don't capture false positive mismatches.
    int expected_count_for_actual_type =
        (unknown_equivalent_types_.count(actual_field_type) == 0 &&
         metric != FALSE_POSITIVE_MISMATCH)
            ? basic_expected_count
            : 0;

    // For metrics keyed to the predicted field type, we don't capture unknown
    // (empty is not a predictable value) and we don't capture false negative
    // mismatches.
    int expected_count_for_predicted_type =
        (predicted_type != UNKNOWN_TYPE && predicted_type != NO_SERVER_DATA &&
         metric != FALSE_NEGATIVE_MISMATCH)
            ? basic_expected_count
            : 0;

    for (const auto& source : prediction_sources) {
      std::string aggregate_histogram =
          "Autofill.FieldPredictionQuality.Aggregate." + source;
      std::string by_field_type_histogram =
          "Autofill.FieldPredictionQuality.ByFieldType." + source;
      histogram_tester.ExpectBucketCount(aggregate_histogram, metric,
                                         aggregate_expected_count);
      histogram_tester.ExpectBucketCount(
          by_field_type_histogram,
          GetFieldTypeGroupPredictionQualityMetric(actual_field_type, metric),
          expected_count_for_actual_type);
      histogram_tester.ExpectBucketCount(
          by_field_type_histogram,
          GetFieldTypeGroupPredictionQualityMetric(predicted_type, metric),
          expected_count_for_predicted_type);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    QualityMetricsTest,
    PredictionQualityMetricsTest,
    testing::Values(
        PredictionQualityMetricsTestCase{NO_SERVER_DATA, EMPTY_TYPE},
        PredictionQualityMetricsTestCase{NO_SERVER_DATA, UNKNOWN_TYPE},
        PredictionQualityMetricsTestCase{NO_SERVER_DATA, AMBIGUOUS_TYPE},
        PredictionQualityMetricsTestCase{NO_SERVER_DATA, EMAIL_ADDRESS},
        PredictionQualityMetricsTestCase{EMAIL_ADDRESS, EMPTY_TYPE},
        PredictionQualityMetricsTestCase{EMAIL_ADDRESS, UNKNOWN_TYPE},
        PredictionQualityMetricsTestCase{EMAIL_ADDRESS, AMBIGUOUS_TYPE},
        PredictionQualityMetricsTestCase{EMAIL_ADDRESS, EMAIL_ADDRESS},
        PredictionQualityMetricsTestCase{EMAIL_ADDRESS, COMPANY_NAME},
        PredictionQualityMetricsTestCase{COMPANY_NAME, EMAIL_ADDRESS},
        PredictionQualityMetricsTestCase{NAME_MIDDLE, AMBIGUOUS_TYPE},
        PredictionQualityMetricsTestCase{COMPANY_NAME, AMBIGUOUS_TYPE},
        PredictionQualityMetricsTestCase{UNKNOWN_TYPE, EMPTY_TYPE},
        PredictionQualityMetricsTestCase{UNKNOWN_TYPE, UNKNOWN_TYPE},
        PredictionQualityMetricsTestCase{UNKNOWN_TYPE, AMBIGUOUS_TYPE},
        PredictionQualityMetricsTestCase{UNKNOWN_TYPE, EMAIL_ADDRESS}));

// Test that we log quality metrics appropriately when an upload is triggered
// but no submission event is sent.
TEST_F(QualityMetricsTest, NoSubmission) {
  FormData form = CreateForm(
      {CreateTestFormField("Autofilled", "autofilled", "Elvis",
                           FormControlType::kInputText),
       CreateTestFormField("Autofill Failed", "autofillfailed",
                           "buddy@gmail.com", FormControlType::kInputText),
       CreateTestFormField("Empty", "empty", "", FormControlType::kInputText),
       CreateTestFormField("Unknown", "unknown", "garbage",
                           FormControlType::kInputText),
       CreateTestFormField("Select", "select", "USA",
                           FormControlType::kSelectOne),
       CreateTestFormField("Phone", "phone", "2345678901",
                           FormControlType::kInputTelephone)});
  test_api(form).field(0).set_is_autofilled(true);
  test_api(form).field(-1).set_is_autofilled(true);

  std::vector<FieldType> heuristic_types = {
      NAME_FULL,         PHONE_HOME_NUMBER, NAME_FULL,
      PHONE_HOME_NUMBER, UNKNOWN_TYPE,      PHONE_HOME_CITY_AND_NUMBER};

  std::vector<FieldType> server_types = {
      NAME_FIRST,    EMAIL_ADDRESS,  NAME_FIRST,
      EMAIL_ADDRESS, NO_SERVER_DATA, PHONE_HOME_CITY_AND_NUMBER};

  autofill_manager().AddSeenForm(form, heuristic_types, server_types);
  // Changes the name field to match the full name.
  SimulateUserChangedFieldTo(form, form.fields()[0], u"Elvis Aaron Presley");

  base::HistogramTester histogram_tester;

  // Triggers the metrics.
  test_api(autofill_client().GetAutofillDriverFactory())
      .Reset(autofill_driver());

  auto Buck = [](FieldType field_type, FieldTypeQualityMetric metric,
                 size_t n) {
    return Bucket(GetFieldTypeGroupPredictionQualityMetric(field_type, metric),
                  n);
  };

  for (const std::string source : {"Heuristic", "Server", "Overall"}) {
    EXPECT_THAT(
        histogram_tester.GetAllSamples(
            "Autofill.FieldPredictionQuality.Aggregate." + source +
            ".NoSubmission"),
        BucketsAre(Bucket(FALSE_NEGATIVE_UNKNOWN, 1), Bucket(TRUE_POSITIVE, 2),
                   Bucket(FALSE_NEGATIVE_MISMATCH, 1),
                   Bucket(FALSE_POSITIVE_EMPTY, 1),
                   Bucket(FALSE_POSITIVE_UNKNOWN, 1)))
        << "source=" << source;
  }

  // Heuristic predictions.
  EXPECT_THAT(
      histogram_tester.GetAllSamples(
          "Autofill.FieldPredictionQuality.ByFieldType.Heuristic.NoSubmission"),
      BucketsAre(Buck(ADDRESS_HOME_COUNTRY, FALSE_NEGATIVE_UNKNOWN, 1),
                 Buck(NAME_FULL, TRUE_POSITIVE, 1),
                 Buck(PHONE_HOME_WHOLE_NUMBER, TRUE_POSITIVE, 1),
                 Buck(EMAIL_ADDRESS, FALSE_NEGATIVE_MISMATCH, 1),
                 Buck(PHONE_HOME_NUMBER, FALSE_POSITIVE_MISMATCH, 1),
                 Buck(NAME_FULL, FALSE_POSITIVE_EMPTY, 1),
                 Buck(PHONE_HOME_NUMBER, FALSE_POSITIVE_UNKNOWN, 1)));

  // Server predictions override heuristics, so server and overall will be the
  // same.
  for (const std::string source : {"Server", "Overall"}) {
    EXPECT_THAT(
        histogram_tester.GetAllSamples(
            "Autofill.FieldPredictionQuality.ByFieldType." + source +
            ".NoSubmission"),
        BucketsAre(Buck(ADDRESS_HOME_COUNTRY, FALSE_NEGATIVE_UNKNOWN, 1),
                   Buck(EMAIL_ADDRESS, TRUE_POSITIVE, 1),
                   Buck(PHONE_HOME_WHOLE_NUMBER, TRUE_POSITIVE, 1),
                   Buck(NAME_FULL, FALSE_NEGATIVE_MISMATCH, 1),
                   Buck(NAME_FIRST, FALSE_POSITIVE_MISMATCH, 1),
                   Buck(NAME_FIRST, FALSE_POSITIVE_EMPTY, 1),
                   Buck(EMAIL_ADDRESS, FALSE_POSITIVE_UNKNOWN, 1)))
        << "source=" << source;
  }
}

// Test that we log quality metrics for heuristics and server predictions based
// on autocomplete attributes present on the fields.
TEST_F(QualityMetricsTest, BasedOnAutocomplete) {
  FormData form = CreateForm(
      {// Heuristic value will match with Autocomplete attribute.
       CreateTestFormField("Last Name", "lastname", "",
                           FormControlType::kInputText, "family-name"),
       // Heuristic value will NOT match with Autocomplete attribute.
       CreateTestFormField("First Name", "firstname", "",
                           FormControlType::kInputText, "additional-name"),
       // Heuristic value will be unknown.
       CreateTestFormField("Garbage label", "garbage", "",
                           FormControlType::kInputText, "postal-code"),
       // No autocomplete attribute. No metric logged.
       CreateTestFormField("Address", "address", "",
                           FormControlType::kInputText, "")});

  std::unique_ptr<FormStructure> form_structure =
      std::make_unique<FormStructure>(form);
  FormStructure* form_structure_ptr = form_structure.get();
  form_structure->DetermineHeuristicTypes(GeoIpCountryCode(""), nullptr);
  ASSERT_TRUE(
      test_api(autofill_manager())
          .mutable_form_structures()
          ->emplace(form_structure_ptr->global_id(), std::move(form_structure))
          .second);

  AutofillQueryResponse response;
  auto* form_suggestion = response.add_form_suggestions();
  // Server response will match with autocomplete.
  AddFieldPredictionToForm(form.fields()[0], NAME_LAST, form_suggestion);
  // Server response will NOT match with autocomplete.
  AddFieldPredictionToForm(form.fields()[1], NAME_FIRST, form_suggestion);
  // Server response will have no data.
  AddFieldPredictionToForm(form.fields()[2], NO_SERVER_DATA, form_suggestion);
  // Not logged.
  AddFieldPredictionToForm(form.fields()[3], NAME_MIDDLE, form_suggestion);

  std::string response_string = SerializeAndEncode(response);
  base::HistogramTester histogram_tester;
  test_api(autofill_manager())
      .OnLoadedServerPredictions(
          response_string, test::GetEncodedSignatures(*form_structure_ptr));

  // Verify that ParseServerPredictionsQueryResponse was called (here and
  // below).
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.ServerQueryResponse"),
      BucketsInclude(Bucket(AutofillMetrics::QUERY_RESPONSE_RECEIVED, 1),
                     Bucket(AutofillMetrics::QUERY_RESPONSE_PARSED, 1)));

  // Autocomplete-derived types are eventually what's inferred.
  EXPECT_EQ(NAME_LAST, form_structure_ptr->field(0)->Type().GetStorableType());
  EXPECT_EQ(NAME_MIDDLE,
            form_structure_ptr->field(1)->Type().GetStorableType());
  EXPECT_EQ(ADDRESS_HOME_ZIP,
            form_structure_ptr->field(2)->Type().GetStorableType());

  for (const std::string source : {"Heuristic", "Server"}) {
    std::string aggregate_histogram =
        "Autofill.FieldPredictionQuality.Aggregate." + source +
        ".BasedOnAutocomplete";
    std::string by_field_type_histogram =
        "Autofill.FieldPredictionQuality.ByFieldType." + source +
        ".BasedOnAutocomplete";

    // Unknown:
    histogram_tester.ExpectBucketCount(aggregate_histogram,
                                       FALSE_NEGATIVE_UNKNOWN, 1);
    histogram_tester.ExpectBucketCount(
        by_field_type_histogram,
        GetFieldTypeGroupPredictionQualityMetric(ADDRESS_HOME_ZIP,
                                                 FALSE_NEGATIVE_UNKNOWN),
        1);
    // Match:
    histogram_tester.ExpectBucketCount(aggregate_histogram, TRUE_POSITIVE, 1);
    histogram_tester.ExpectBucketCount(
        by_field_type_histogram,
        GetFieldTypeGroupPredictionQualityMetric(NAME_LAST, TRUE_POSITIVE), 1);
    // Mismatch:
    histogram_tester.ExpectBucketCount(aggregate_histogram,
                                       FALSE_NEGATIVE_MISMATCH, 1);
    histogram_tester.ExpectBucketCount(by_field_type_histogram,
                                       GetFieldTypeGroupPredictionQualityMetric(
                                           NAME_FIRST, FALSE_POSITIVE_MISMATCH),
                                       1);
    histogram_tester.ExpectBucketCount(
        by_field_type_histogram,
        GetFieldTypeGroupPredictionQualityMetric(NAME_MIDDLE,
                                                 FALSE_POSITIVE_MISMATCH),
        1);

    // Sanity check.
    histogram_tester.ExpectTotalCount(aggregate_histogram, 3);
    histogram_tester.ExpectTotalCount(by_field_type_histogram, 4);
  }
}

// Tests that the Autofill.LabelInference.InferredLabelSource.AtSubmission2
// metric is emitted correctly.
TEST_F(QualityMetricsTest, InferredLabelSourceAtSubmissionMetric) {
  const AutofillProfile& profile =
      *personal_data().address_data_manager().GetProfileByGUID(kTestProfileId);

  // Create a form and fill the `name_field` and `country_field` with values
  // from the `profile`, ensuring that they have a possible type. The
  // `street_field` is filled with an unknown value, which makes sure that it
  // doesn't have a possible type.
  // The `FormFieldData::label_source` of the fields is set manually, since
  // this test doesn't run label inference.
  FormFieldData name_field;
  name_field.set_value(profile.GetInfo(
      NAME_FULL, personal_data().address_data_manager().app_locale()));
  name_field.set_label_source(FormFieldData::LabelSource::kUnknown);
  FormFieldData street_field;
  street_field.set_value(u"unknown");
  street_field.set_label_source(FormFieldData::LabelSource::kForId);
  FormFieldData country_field;
  country_field.set_value(
      profile.GetInfo(ADDRESS_HOME_COUNTRY,
                      personal_data().address_data_manager().app_locale()));
  country_field.set_label_source(FormFieldData::LabelSource::kLabelTag);
  const FormData form = CreateForm({name_field, street_field, country_field});
  autofill_manager().AddSeenForm(
      form, {NAME_FIRST, ADDRESS_HOME_LINE1, ADDRESS_HOME_COUNTRY});

  // Expect that the label source of all fields with a possible type is logged
  // on form submission.
  base::HistogramTester histogram_tester;
  SubmitForm(form);
  EXPECT_THAT(histogram_tester.GetAllSamples(
                  "Autofill.LabelInference.InferredLabelSource.AtSubmission2"),
              BucketsAre(Bucket(name_field.label_source(), 1),
                         Bucket(country_field.label_source(), 1)));
}

// Tests that precision metric is recorded for email field predictions.
TEST_F(QualityMetricsTest, EmailPredictionCorrectnessPrecisionMetric) {
  FormData form = CreateForm(
      {CreateTestFormField("Name", "name", "Elvis Aaron Presley",
                           FormControlType::kInputText),
       CreateTestFormField("Address", "address", "3734 Elvis Presley Blvd.",
                           FormControlType::kInputText),
       CreateTestFormField("Email", "email", "buddy@gmail.com",
                           FormControlType::kInputText)});

  std::vector<FieldType> field_types = {NAME_FULL, ADDRESS_HOME_LINE1,
                                        EMAIL_ADDRESS};
  autofill_manager().AddSeenForm(form, field_types);

  std::string precision_histogram =
      "Autofill.EmailPredictionCorrectness.Precision";

  // Check that the metric records true positive.
  {
    base::HistogramTester histogram_tester;
    FillTestProfile(form);
    SubmitForm(form);

    EXPECT_THAT(
        histogram_tester.GetAllSamples(precision_histogram),
        BucketsAre(Bucket(EmailPredictionConfusionMatrix::kTruePositive, 1)));
  }

  // Check that the metric records false positive. (The input value is not an
  // email).
  {
    base::HistogramTester histogram_tester;
    test_api(form).field(2).set_value(u"notemailtext");
    FillTestProfile(form);
    SubmitForm(form);

    EXPECT_THAT(
        histogram_tester.GetAllSamples(precision_histogram),
        BucketsAre(Bucket(EmailPredictionConfusionMatrix::kFalsePositive, 1)));
  }
  // Check that the metric is not recorded for empty values.
  {
    base::HistogramTester histogram_tester;
    test_api(form).field(2).set_value(u"");
    FillTestProfile(form);
    SubmitForm(form);
    histogram_tester.ExpectTotalCount(precision_histogram, 0);
  }
}

// Tests that recall metric is recorded for email field predictions.
TEST_F(QualityMetricsTest, EmailPredictionCorrectnessRecallMetric) {
  FormData form = CreateForm(
      {CreateTestFormField("Name", "name", "Elvis Aaron Presley",
                           FormControlType::kInputText),
       CreateTestFormField("Address", "address", "3734 Elvis Presley Blvd.",
                           FormControlType::kInputText),
       CreateTestFormField("Email", "email", "buddy@gmail.com",
                           FormControlType::kInputText)});

  std::vector<FieldType> field_types = {NAME_FULL, ADDRESS_HOME_LINE1,
                                        EMAIL_ADDRESS};
  autofill_manager().AddSeenForm(form, field_types);

  std::string precision_histogram =
      "Autofill.EmailPredictionCorrectness.Recall";

  // Check that the metric records true positive.
  {
    base::HistogramTester histogram_tester;
    FillTestProfile(form);
    SubmitForm(form);

    EXPECT_THAT(
        histogram_tester.GetAllSamples(precision_histogram),
        BucketsAre(Bucket(EmailPredictionConfusionMatrix::kTruePositive, 1)));
  }

  // Check that the metric records false negative. (The predicted type is not
  // email).
  {
    base::HistogramTester histogram_tester;
    autofill_manager().ClearFormStructures();
    // Wrong field type predicted (i.e. not email).
    field_types[2] = COMPANY_NAME;
    autofill_manager().AddSeenForm(form, field_types);
    FillTestProfile(form);
    SubmitForm(form);

    EXPECT_THAT(
        histogram_tester.GetAllSamples(precision_histogram),
        BucketsAre(Bucket(EmailPredictionConfusionMatrix::kFalseNegative, 1)));
  }
  // Check that the metric is not recorded for empty values.
  {
    base::HistogramTester histogram_tester;
    test_api(form).field(2).set_value(u"");
    FillTestProfile(form);
    SubmitForm(form);
    histogram_tester.ExpectTotalCount(precision_histogram, 0);
  }
}

}  // namespace autofill::autofill_metrics
