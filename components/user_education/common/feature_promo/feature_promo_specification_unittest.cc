// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/user_education/common/feature_promo/feature_promo_specification.h"

#include "base/feature_list.h"
#include "components/strings/grit/components_strings.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/interaction/element_test_util.h"

namespace user_education {

namespace {
BASE_FEATURE(kTestRotatingPromo,
             "TEST_RotatingPromo",
             base::FEATURE_DISABLED_BY_DEFAULT);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kTestAnchorElement);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kTestAnchorElement2);
const ui::ElementContext kTestContext(1);
}  // namespace

TEST(FeaturePromoSpecificationTest, RotatingPromoOverrideFocusOnShow) {
  FeaturePromoSpecification::RotatingPromos promos(
      FeaturePromoSpecification::CreateForToastPromo(
          kTestRotatingPromo, kTestAnchorElement, IDS_CLOSE_PROMO, 0,
          FeaturePromoSpecification::AcceleratorInfo()),
      std::nullopt,
      FeaturePromoSpecification::CreateForToastPromo(
          kTestRotatingPromo, kTestAnchorElement, IDS_CLOSE_PROMO, 0,
          FeaturePromoSpecification::AcceleratorInfo()));

  promos.at(2)->OverrideFocusOnShow(false);

  auto spec = FeaturePromoSpecification::CreateRotatingPromoForTesting(
      kTestRotatingPromo, std::move(promos));

  spec.OverrideFocusOnShow(true);

  EXPECT_EQ(true, spec.rotating_promos().at(0)->focus_on_show_override());
  EXPECT_EQ(std::nullopt, spec.rotating_promos().at(1));
  EXPECT_EQ(false, spec.rotating_promos().at(2)->focus_on_show_override());
}

TEST(FeaturePromoSpecificationTest, GetAnchorElementFromRotatingPromo) {
  ui::test::TestElement el1(kTestAnchorElement, kTestContext);
  ui::test::TestElement el2(kTestAnchorElement2, kTestContext);
  el1.Show();
  el2.Show();

  FeaturePromoSpecification::RotatingPromos promos(
      FeaturePromoSpecification::CreateForToastPromo(
          kTestRotatingPromo, kTestAnchorElement, IDS_CLOSE_PROMO, 0,
          FeaturePromoSpecification::AcceleratorInfo()),
      std::nullopt,
      FeaturePromoSpecification::CreateForToastPromo(
          kTestRotatingPromo, kTestAnchorElement2, IDS_CLOSE_PROMO, 0,
          FeaturePromoSpecification::AcceleratorInfo()));

  auto spec = FeaturePromoSpecification::CreateRotatingPromoForTesting(
      kTestRotatingPromo, std::move(promos));

  EXPECT_EQ(&el1, spec.GetAnchorElement(kTestContext, 0));
  EXPECT_EQ(&el2, spec.GetAnchorElement(kTestContext, 2));
}

}  // namespace user_education
