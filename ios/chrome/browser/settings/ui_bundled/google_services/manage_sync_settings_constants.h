// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_SETTINGS_UI_BUNDLED_GOOGLE_SERVICES_MANAGE_SYNC_SETTINGS_CONSTANTS_H_
#define IOS_CHROME_BROWSER_SETTINGS_UI_BUNDLED_GOOGLE_SERVICES_MANAGE_SYNC_SETTINGS_CONSTANTS_H_

#import <Foundation/Foundation.h>

#import "ios/chrome/browser/shared/ui/list_model/list_model.h"

// Accessibility identifier for the Data from Chrome Sync cell.
extern NSString* const kDataFromChromeSyncAccessibilityIdentifier;

// Accessibility identifier for Manage Sync table view.
extern NSString* const kManageSyncTableViewAccessibilityIdentifier;

// Accessibility identifiers for sync switch items.
extern NSString* const kSyncBookmarksIdentifier;
extern NSString* const kSyncHistoryAndTabsIdentifier;
extern NSString* const kSyncPasswordsIdentifier;
extern NSString* const kSyncPaymentsIdentifier;
extern NSString* const kSyncOpenTabsIdentifier;
extern NSString* const kSyncAutofillIdentifier;
extern NSString* const kSyncPreferencesIdentifier;
extern NSString* const kSyncReadingListIdentifier;
extern NSString* const kSyncErrorButtonIdentifier;

// Accessibility identifier for Encryption item.
extern NSString* const kEncryptionAccessibilityIdentifier;

// Accessibility identifier for batch upload recommendation item.
extern NSString* const kBatchUploadRecommendationItemAccessibilityIdentifier;
// Accessibility identifier for batch upload item.
extern NSString* const kBatchUploadAccessibilityIdentifier;

// Accessibility identifier for the Personalize Google Services item.
extern NSString* const kPersonalizeGoogleServicesIdentifier;
// Accessibility identifier for the Personalize Google Services view.
extern NSString* const kPersonalizeGoogleServicesViewIdentifier;

// Sections used in Sync Settings page.
typedef NS_ENUM(NSInteger, SyncSettingsSectionIdentifier) {
  // Section for all the sync settings.
  SyncDataTypeSectionIdentifier = kSectionIdentifierEnumZero,
  // Manage and sign out options.
  ManageAndSignOutSectionIdentifier,
  // Switch account and sign out options.
  SwitchAccountAndSignOutSectionIdentifier,
  // Advanced settings.
  AdvancedSettingsSectionIdentifier,
  // Sync errors.
  SyncErrorsSectionIdentifier,
  // Section to show the batch upload option.
  BatchUploadSectionIdentifier,
};

// Item types used per Sync Setting section.
// Types are used to uniquely identify SyncSwitchItem items.
typedef NS_ENUM(NSInteger, SyncSettingsItemType) {
  // SyncDataTypeSectionIdentifier section.
  // kSyncAutofill.
  AutofillDataTypeItemType = kItemTypeEnumZero,
  // kSyncBookmarks.
  BookmarksDataTypeItemType,
  // kSyncOmniboxHistory.
  HistoryDataTypeItemType,
  // kSyncOpenTabs.
  OpenTabsDataTypeItemType,
  // kSyncPasswords
  PasswordsDataTypeItemType,
  // kSyncReadingList.
  ReadingListDataTypeItemType,
  // kSyncPreferences.
  SettingsDataTypeItemType,
  // kPayments.
  PaymentsDataTypeItemType,
  // Item for the header and the footer of the types list.
  TypesListHeaderOrFooterType,
  // ManageAndSignOutSectionIdentifier section.
  // Sign out item.
  SignOutItemType,
  // Sign out item footer.
  SignOutItemFooterType,
  // Manage Google Account item.
  ManageGoogleAccountItemType,
  // Manage accounts on this device item.
  ManageAccountsItemType,
  // SwitchAccountAndSignOutSectionIdentifier section.
  // Switch account item.
  SwitchAccountItemType,
  // AdvancedSettingsSectionIdentifier section.
  // Encryption item.
  EncryptionItemType,
  // Google activity controls item.
  GoogleActivityControlsItemType,
  // Data from Chrome sync.
  DataFromChromeSync,
  // Personalize Google services item.
  PersonalizeGoogleServicesItemType,
  // SyncErrorsSectionIdentifier section.
  // Sync errors.
  PrimaryAccountReauthErrorItemType,
  ShowPassphraseDialogErrorItemType,
  SyncNeedsTrustedVaultKeyErrorItemType,
  SyncTrustedVaultRecoverabilityDegradedErrorItemType,
  SyncDisabledByAdministratorErrorItemType,
  // Indicates the errors related to the signed in account.
  AccountErrorMessageItemType,
  // BatchUploadSectionIdentifier section.
  // Item for the batch upload button.
  BatchUploadButtonItemType,
  // Indicates the items to be uploaded to the account.
  BatchUploadRecommendationItemType,
  // Open account storage management.
  ManageAccountStorageType,
};

#endif  // IOS_CHROME_BROWSER_SETTINGS_UI_BUNDLED_GOOGLE_SERVICES_MANAGE_SYNC_SETTINGS_CONSTANTS_H_
