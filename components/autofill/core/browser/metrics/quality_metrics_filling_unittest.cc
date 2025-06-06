// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/metrics/quality_metrics_filling.h"

#include "base/test/metrics/histogram_tester.h"
#include "components/autofill/core/browser/autofill_field.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/test_utils/autofill_form_test_utils.h"
#include "components/autofill/core/common/autofill_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace autofill::autofill_metrics {

namespace {

constexpr char kUmaAutomationRate[] = "Autofill.AutomationRate.Address";

std::unique_ptr<FormStructure> GetFormStructure(
    const test::FormDescription& form_description) {
  auto form_structure =
      std::make_unique<FormStructure>(test::GetFormData(form_description));
  form_structure->DetermineHeuristicTypes(GeoIpCountryCode(""), nullptr);
  return form_structure;
}

class QualityMetricsFillingTest : public testing::Test {
 protected:
  test::AutofillUnitTestEnvironment autofill_test_environment_;
  base::HistogramTester histogram_tester_;
};

// Tests that no metrics are reported for
// Autofill.AutomationRate.Address if a form is submitted with empty
// fields because it's not possible to calculate a filling rate if the
// denominator is 0.
TEST_F(QualityMetricsFillingTest, AutomationRateNotEmittedForEmptyForm) {
  std::unique_ptr<FormStructure> form_structure =
      GetFormStructure({.fields = {{.role = NAME_FIRST},
                                   {.role = NAME_LAST},
                                   {.role = ADDRESS_HOME_LINE1}}});

  LogFillingQualityMetrics(*form_structure);

  EXPECT_TRUE(histogram_tester_.GetAllSamples(kUmaAutomationRate).empty());
}

// Tests that Autofill.AutomationRate.Address is reported as 0 if all
// input was generated via manual typing.
TEST_F(QualityMetricsFillingTest, AutomationRate0EmittedForManuallyFilledForm) {
  std::unique_ptr<FormStructure> form_structure =
      GetFormStructure({.fields = {{.role = NAME_FIRST},
                                   {.role = NAME_LAST},
                                   {.role = ADDRESS_HOME_LINE1}}});

  form_structure->fields()[0]->set_value(u"Jane");
  form_structure->fields()[1]->set_value(u"Doe");

  LogFillingQualityMetrics(*form_structure);

  histogram_tester_.ExpectUniqueSample(kUmaAutomationRate, 0, 1);
}

// Tests that Autofill.AutomationRate.Address is reported as 100% if all
// input was generated via autofilling typing.
TEST_F(QualityMetricsFillingTest, AutomationRate100EmittedForAutofilledForm) {
  std::unique_ptr<FormStructure> form_structure = GetFormStructure(
      {.fields = {{.role = NAME_FIRST, .heuristic_type = NAME_FIRST},
                  {
                      .role = NAME_LAST,
                      .heuristic_type = NAME_LAST,
                  },
                  {.role = ADDRESS_HOME_LINE1}}});

  form_structure->fields()[0]->set_value(u"Jane");
  form_structure->fields()[1]->set_value(u"Doe");
  form_structure->fields()[0]->set_is_autofilled(true);
  form_structure->fields()[1]->set_is_autofilled(true);

  LogFillingQualityMetrics(*form_structure);

  histogram_tester_.ExpectUniqueSample(kUmaAutomationRate, 100, 1);
}

// Tests that Autofill.AutomationRate.Address is reported as 57% if
// 4 out of 7 submitted characters are are autofilled.
TEST_F(QualityMetricsFillingTest,
       AutomationRateEmittedWithCorrectCalculationForPartiallyAutofilledForm) {
  std::unique_ptr<FormStructure> form_structure = GetFormStructure(
      {.fields = {{.role = NAME_FIRST, .heuristic_type = NAME_FIRST},
                  {.role = NAME_LAST},
                  {.role = ADDRESS_HOME_LINE1}}});

  form_structure->fields()[0]->set_value(u"Jane");
  form_structure->fields()[1]->set_value(u"Doe");
  form_structure->fields()[0]->set_is_autofilled(true);

  LogFillingQualityMetrics(*form_structure);

  histogram_tester_.ExpectUniqueSample(kUmaAutomationRate, 57, 1);
}

// Tests that fields with a lot of input are ignored in the calculation of
// Autofill.AutomationRate.Address. This is to prevent outliers in
// metrics where a user types a long essay in a single field.
TEST_F(QualityMetricsFillingTest, AutomationRateEmittedIgnoringLongValues) {
  std::unique_ptr<FormStructure> form_structure = GetFormStructure(
      {.fields = {{.role = NAME_FIRST, .heuristic_type = NAME_FIRST},
                  {.role = NAME_LAST, .heuristic_type = NAME_LAST},
                  {.role = ADDRESS_HOME_LINE1}}});

  form_structure->fields()[0]->set_value(u"Jane");
  form_structure->fields()[1]->set_value(
      u"Very very very very very very very very very very very "
      u"very very very very very very very very very very very "
      u"very very very very very very very long text");
  form_structure->fields()[0]->set_is_autofilled(true);

  LogFillingQualityMetrics(*form_structure);

  histogram_tester_.ExpectUniqueSample(kUmaAutomationRate, 100, 1);
}

// Tests that fields that were already filled at page load time and not modified
// by the user are ignored in the calculation of
// Autofill.AutomationRate.Address.
TEST_F(QualityMetricsFillingTest,
       AutomationRateEmittedIgnoringUnchangedPreFilledValues) {
  std::unique_ptr<FormStructure> form_structure =
      GetFormStructure({.fields = {{.role = NAME_FIRST, .value = u"Jane"},
                                   {.role = NAME_LAST, .value = u"Fonda"},
                                   {.role = ADDRESS_HOME_LINE1}}});

  form_structure->fields()[1]->set_value(u"Doe");
  form_structure->fields()[1]->set_is_autofilled(true);

  LogFillingQualityMetrics(*form_structure);

  histogram_tester_.ExpectUniqueSample(kUmaAutomationRate, 100, 1);
}

// Tests that no Autofill.DataUtilization* metric is emitted for a field with
// possible types {UNKNOWN_TYPE}.
TEST_F(QualityMetricsFillingTest, DataUtilizationNotEmittedForUnknownType) {
  std::unique_ptr<FormStructure> form_structure =
      GetFormStructure({.fields = {{}}});
  form_structure->field(0)->set_possible_types({UNKNOWN_TYPE});

  LogFillingQualityMetrics(*form_structure);

  // Autofill.DataUtilization.AllFieldTypes.Aggregate is always recorded if any
  // data utilization metric is recorded so it suffices to check that it's not
  // emitted here.
  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples("Autofill.DataUtilization.AllFieldTypes.Aggregate")
          .empty());
}

// Tests that no Autofill.DataUtilization* metric is emitted for a field with
// possible types {EMPTY_TYPE}.
TEST_F(QualityMetricsFillingTest, DataUtilizationNotEmittedForEmptyType) {
  std::unique_ptr<FormStructure> form_structure =
      GetFormStructure({.fields = {{}}});
  form_structure->field(0)->set_possible_types({EMPTY_TYPE});

  LogFillingQualityMetrics(*form_structure);

  // Autofill.DataUtilization.AllFieldTypes.Aggregate is always recorded if any
  // data utilization metric is recorded so it suffices to check that it's not
  // emitted here.
  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples("Autofill.DataUtilization.AllFieldTypes.Aggregate")
          .empty());
}

// Tests that no Autofill.DataUtilization* metric is emitted for a field with
// an unchanged initial value.
TEST_F(QualityMetricsFillingTest,
       DataUtilizationNotEmittedForUnchangedPreFilledFields) {
  std::unique_ptr<FormStructure> form_structure =
      GetFormStructure({.fields = {{.value = u"initial value"}}});
  form_structure->field(0)->set_possible_types({NAME_FIRST});

  LogFillingQualityMetrics(*form_structure);

  // Autofill.DataUtilization.AllFieldTypes.Aggregate is always recorded if any
  // data utilization metric is recorded so it suffices to check that it's not
  // emitted here.
  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples("Autofill.DataUtilization.AllFieldTypes.Aggregate")
          .empty());
}

// Tests that all "Autofill.DataUtilization*" variants, except for
// "*HadPrediction", are recorded for a fillable, non-numeric field type with
// autocomplete="garbage".
TEST_F(QualityMetricsFillingTest,
       DataUtilizationEmittedWithVariantsGarbageAndNoPrediction) {
  std::unique_ptr<FormStructure> form_structure =
      GetFormStructure({.fields = {{.value = u"initial value",
                                    .autocomplete_attribute = "garbage"}}});
  form_structure->field(0)->set_possible_types({NAME_FIRST});
  form_structure->field(0)->set_value(u"later value");

  LogFillingQualityMetrics(*form_structure);

  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.AllFieldTypes.Aggregate",
      AutofillDataUtilization::kNotAutofilled, 1);
  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.SelectedFieldTypes.Aggregate",
      AutofillDataUtilization::kNotAutofilled, 1);

  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.AllFieldTypes.Garbage",
      AutofillDataUtilization::kNotAutofilled, 1);
  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.SelectedFieldTypes.Garbage",
      AutofillDataUtilization::kNotAutofilled, 1);

  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.AllFieldTypes.NoPrediction",
      AutofillDataUtilization::kNotAutofilled, 1);
  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.SelectedFieldTypes.NoPrediction",
      AutofillDataUtilization::kNotAutofilled, 1);

  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.ByPossibleType", (NAME_FIRST << 6) | 0, 1);
  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.NoPrediction.ByPossibleType",
      (NAME_FIRST << 6) | 0, 1);
  histogram_tester_.ExpectTotalCount(
      "Autofill.DataUtilization.HadPrediction.ByPossibleType", 0);
  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.GarbageNoPrediction.ByPossibleType",
      (NAME_FIRST << 6) | 0, 1);
  histogram_tester_.ExpectTotalCount(
      "Autofill.DataUtilization.GarbageHadPrediction.ByPossibleType", 0);

  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples("Autofill.DataUtilization.AllFieldTypes.HadPrediction")
          .empty());
  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples(
              "Autofill.DataUtilization.SelectedFieldTypes.HadPrediction")
          .empty());

  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples(
              "Autofill.DataUtilization.AllFieldTypes.GarbageHadPrediction")
          .empty());
  EXPECT_TRUE(histogram_tester_
                  .GetAllSamples("Autofill.DataUtilization.SelectedFieldTypes."
                                 "GarbageHadPrediction")
                  .empty());
}

// Tests that
// "Autofill.DataUtilization.AutocompleteOffNoPrediction.ByPossibleType" is
// emitted when the field has autocomplete="off".
TEST_F(QualityMetricsFillingTest,
       DataUtilizationEmittedWithVariantsAutocompleteOffAndNoPrediction) {
  std::unique_ptr<FormStructure> form_structure =
      GetFormStructure({.fields = {{.value = u"initial value",
                                    .autocomplete_attribute = "off"}}});
  form_structure->field(0)->set_possible_types({NAME_FIRST});
  form_structure->field(0)->set_value(u"later value");

  LogFillingQualityMetrics(*form_structure);

  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.NoPrediction.ByPossibleType",
      (NAME_FIRST << 6) | 0, 1);
  histogram_tester_.ExpectTotalCount(
      "Autofill.DataUtilization.HadPrediction.ByPossibleType", 0);
  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.AutocompleteOffNoPrediction.ByPossibleType",
      (NAME_FIRST << 6) | 0, 1);
  histogram_tester_.ExpectTotalCount(
      "Autofill.DataUtilization.AutocompleteOffHadPrediction.ByPossibleType",
      0);
}

// Tests that metrics "Autofill.DataUtilization.*.{Aggregate, HadPrediction}"
// are recorded for a field with a `NAME_FIRST` field type prediction.
TEST_F(QualityMetricsFillingTest,
       DataUtilizationEmittedWithVariantHasPrediction) {
  std::unique_ptr<FormStructure> form_structure =
      GetFormStructure({.fields = {{}}});

  form_structure->field(0)->set_value(u"Foo");
  form_structure->field(0)->set_is_autofilled(true);
  form_structure->field(0)->set_possible_types({NAME_FIRST});
  form_structure->field(0)->SetTypeTo(AutofillType(NAME_FIRST),
                                      AutofillPredictionSource::kHeuristics);

  LogFillingQualityMetrics(*form_structure);

  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.AllFieldTypes.Aggregate",
      AutofillDataUtilization::kAutofilled, 1);
  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.SelectedFieldTypes.Aggregate",
      AutofillDataUtilization::kAutofilled, 1);

  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.AllFieldTypes.HadPrediction",
      AutofillDataUtilization::kAutofilled, 1);
  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.SelectedFieldTypes.HadPrediction",
      AutofillDataUtilization::kAutofilled, 1);

  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.ByPossibleType", (NAME_FIRST << 6) | 1, 1);
  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.HadPrediction.ByPossibleType",
      (NAME_FIRST << 6) | 1, 1);
  histogram_tester_.ExpectTotalCount(
      "Autofill.DataUtilization.NoPrediction.ByPossibleType", 0);

  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples("Autofill.DataUtilization.AllFieldTypes.Garbage")
          .empty());
  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples("Autofill.DataUtilization.SelectedFieldTypes.Garbage")
          .empty());

  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples("Autofill.DataUtilization.AllFieldTypes.NoPrediction")
          .empty());
  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples(
              "Autofill.DataUtilization.SelectedFieldTypes.NoPrediction")
          .empty());

  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples(
              "Autofill.DataUtilization.AllFieldTypes.GarbageHadPrediction")
          .empty());
  EXPECT_TRUE(histogram_tester_
                  .GetAllSamples("Autofill.DataUtilization.SelectedFieldTypes."
                                 "GarbageHadPrediction")
                  .empty());
}

// Tests that no "SelectedFieldTypes" variants are recorded for a field with
// possible types {CREDIT_CARD_EXP_MONTH}.
TEST_F(QualityMetricsFillingTest,
       DataUtilizationEmittedForAllFieldTypesVariantOnlyWhenTypeIsNumeric) {
  std::unique_ptr<FormStructure> form_structure =
      GetFormStructure({.fields = {{}}});
  form_structure->field(0)->set_value(u"05");
  form_structure->field(0)->set_possible_types({CREDIT_CARD_EXP_MONTH});

  LogFillingQualityMetrics(*form_structure);

  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.AllFieldTypes.Aggregate",
      AutofillDataUtilization::kNotAutofilled, 1);

  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.AllFieldTypes.NoPrediction",
      AutofillDataUtilization::kNotAutofilled, 1);

  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.ByPossibleType",
      (CREDIT_CARD_EXP_MONTH << 6) | 0, 1);
  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.NoPrediction.ByPossibleType",
      (CREDIT_CARD_EXP_MONTH << 6) | 0, 1);
  histogram_tester_.ExpectTotalCount(
      "Autofill.DataUtilization.HadPrediction.ByPossibleType", 0);

  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples("Autofill.DataUtilization.AllFieldTypes.Garbage")
          .empty());

  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples("Autofill.DataUtilization.AllFieldTypes.HadPrediction")
          .empty());

  // No "SelectedFieldTypes" variant emitted.
  EXPECT_TRUE(histogram_tester_
                  .GetAllSamples(
                      "Autofill.DataUtilization.SelectedFieldTypes.Aggregate")
                  .empty());
  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples(
              "Autofill.DataUtilization.SelectedFieldTypes.NoPrediction")
          .empty());
  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples(
              "Autofill.DataUtilization.SelectedFieldTypes.HadPrediction")
          .empty());
  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples("Autofill.DataUtilization.SelectedFieldTypes.Garbage")
          .empty());
  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples(
              "Autofill.DataUtilization.AllFieldTypes.GarbageHadPrediction")
          .empty());
  EXPECT_TRUE(histogram_tester_
                  .GetAllSamples("Autofill.DataUtilization.SelectedFieldTypes."
                                 "GarbageHadPrediction")
                  .empty());
}

// Tests that "GarbageHadPrediction" variants are recorded for a NAME_FIRST
// field with autocomplete=garbage.
TEST_F(QualityMetricsFillingTest,
       DataUtilizationEmittedWithVariantGarbageHadPrediction) {
  std::unique_ptr<FormStructure> form_structure =
      GetFormStructure({.fields = {{.autocomplete_attribute = "garbage"}}});
  form_structure->field(0)->set_value(u"Jane");
  form_structure->field(0)->set_possible_types({NAME_FIRST});
  form_structure->field(0)->SetTypeTo(AutofillType(NAME_FIRST),
                                      AutofillPredictionSource::kHeuristics);

  LogFillingQualityMetrics(*form_structure);

  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.AllFieldTypes.Aggregate",
      AutofillDataUtilization::kNotAutofilled, 1);
  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.SelectedFieldTypes.Aggregate",
      AutofillDataUtilization::kNotAutofilled, 1);

  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.AllFieldTypes.HadPrediction",
      AutofillDataUtilization::kNotAutofilled, 1);
  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.SelectedFieldTypes.HadPrediction",
      AutofillDataUtilization::kNotAutofilled, 1);

  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.AllFieldTypes.Garbage",
      AutofillDataUtilization::kNotAutofilled, 1);
  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.SelectedFieldTypes.Garbage",
      AutofillDataUtilization::kNotAutofilled, 1);

  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.AllFieldTypes.GarbageHadPrediction",
      AutofillDataUtilization::kNotAutofilled, 1);
  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.SelectedFieldTypes.GarbageHadPrediction",
      AutofillDataUtilization::kNotAutofilled, 1);

  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.ByPossibleType", (NAME_FIRST << 6) | 0, 1);
  histogram_tester_.ExpectUniqueSample(
      "Autofill.DataUtilization.HadPrediction.ByPossibleType",
      (NAME_FIRST << 6) | 0, 1);
  histogram_tester_.ExpectTotalCount(
      "Autofill.DataUtilization.NoPrediction.ByPossibleType", 0);

  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples("Autofill.DataUtilization.AllFieldTypes.NoPrediction")
          .empty());
  EXPECT_TRUE(
      histogram_tester_
          .GetAllSamples(
              "Autofill.DataUtilization.SelectedFieldTypes.NoPrediction")
          .empty());
}

}  // namespace

}  // namespace autofill::autofill_metrics
