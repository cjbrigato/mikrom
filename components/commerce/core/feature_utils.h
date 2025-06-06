// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_COMMERCE_CORE_FEATURE_UTILS_H_
#define COMPONENTS_COMMERCE_CORE_FEATURE_UTILS_H_

class PrefService;

namespace commerce {

class AccountChecker;
class ProductSpecificationsService;

// This is a feature check for the "shopping list". This will only return true
// if the user has the feature flag enabled, is signed-in, has MSBB enabled,
// has webapp activity enabled, is allowed by enterprise policy, and (if
// applicable) in an eligible country and locale. The value returned by this
// method can change at runtime, so it should not be used when deciding
// whether to create critical, feature-related infrastructure.
bool IsShoppingListEligible(AccountChecker* account_checker);

// Returns whether the API for getting price insights is available for use. This
// considers the user's region and locale.
bool IsPriceInsightsApiEnabled(AccountChecker* account_checker);

// This is a feature check for the "price insights", which will return true
// if the user has the feature flag enabled, has MSBB enabled, and (if
// applicable) is in an eligible country and locale. The value returned by
// this method can change at runtime, so it should not be used when deciding
// whether to create critical, feature-related infrastructure.
bool IsPriceInsightsEligible(AccountChecker* account_checker);

// Returns whether the subscriptions API is available for use. This considers
// the user's region and locale and is not necessarily bound to any specific
// user-facing feature.
bool IsSubscriptionsApiEnabled(AccountChecker* account_checker);

// Returns whether the price annotations feature is enabled. This check will
// check allowed country and locale.
bool IsPriceAnnotationsEnabled(AccountChecker* account_checker);

// Check if the product specifications feature is allowed for enterprise.
bool IsProductSpecificationsAllowedForEnterprise(PrefService* prefs);

// Returns whether quality logging is allowed for the product specifications
// feature. This is directly tied to the enterprise setting.
bool IsProductSpecificationsQualityLoggingAllowed(PrefService* prefs);

// Returns whether the sync type for product specifications is enabled and
// syncing.
bool IsSyncingProductSpecifications(AccountChecker* account_checker);

// Returns whether the full-page UI for product specifications is allowed to
// load.
bool CanLoadProductSpecificationsFullPageUi(AccountChecker* account_checker);

// Returns whether a user is allowed to manage their product specifications
// sets. This check is not 1:1 with the feature being enabled. There are some
// cases where we'd like the user to be able to view or remove their sets
// without necessarily being able to use the full feature.
bool CanManageProductSpecificationsSets(
    AccountChecker* account_checker,
    ProductSpecificationsService* product_spec_service);

// Returns whether the data for product specifications can be fetched. This
// should be used to test if we can call the product specs backend. The user
// may still be able to manage their sets.
bool CanFetchProductSpecificationsData(AccountChecker* account_checker);

// Returns whether we should show the settings UI for product specifications.
bool IsProductSpecificationsSettingVisible(AccountChecker* account_checker);

// Whether APIs like |GetDiscountInfoForUrls| are enabled and allowed to be
// used.
bool IsDiscountInfoApiEnabled(AccountChecker* account_checker);

// This is a feature check for "show discounts on navigation", which will
// return true if the user has the feature flag enabled, is signed-in and
// synced, has MSBB enabled, and (if applicable) is in an eligible country and
// locale. The value returned by this method can change at runtime, so it
// should not be used when deciding whether to create critical,
// feature-related infrastructure.
bool IsDiscountEligibleToShowOnNavigation(AccountChecker* account_checker);

// This is a feature check for the "merchant viewer", which will return true
// if the user has the feature flag enabled or (if applicable) is in an
// enabled country and locale.
bool IsMerchantViewerEnabled(AccountChecker* account_checker);

// Returns whether the |IsShoppingPage| API is available for use. This considers
// the user's region and locale and is not necessarily bound to any specific
// user-facing feature.
bool IsShoppingPageTypesApiEnabled(AccountChecker* account_checker);

// This is a feature check for showing discounts at the checkout page, which
// will return true if the user has the feature flag enabled, is signed-in and
// synced, has MSBB enabled, and (if applicable) is in an eligible country and
// locale. The value returned by this method can change at runtime, so it
// should not be used when deciding whether to create critical,
// feature-related infrastructure.
bool IsDiscountAutofillEnabled(AccountChecker* account_checker);
}  // namespace commerce

#endif  // COMPONENTS_COMMERCE_CORE_FEATURE_UTILS_H_
