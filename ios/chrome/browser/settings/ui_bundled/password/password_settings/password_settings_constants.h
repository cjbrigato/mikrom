// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_SETTINGS_UI_BUNDLED_PASSWORD_PASSWORD_SETTINGS_PASSWORD_SETTINGS_CONSTANTS_H_
#define IOS_CHROME_BROWSER_SETTINGS_UI_BUNDLED_PASSWORD_PASSWORD_SETTINGS_PASSWORD_SETTINGS_CONSTANTS_H_

#import <Foundation/Foundation.h>

#import "ios/chrome/browser/shared/ui/list_model/list_model.h"

// Sections of the password settings UI.
typedef NS_ENUM(NSInteger, PasswordSettingsSectionIdentifier) {
  SectionIdentifierSavePasswordsSwitch = kSectionIdentifierEnumZero,
  SectionIdentifierBulkMovePasswordsToAccount,
  SectionIdentifierPasswordsInOtherApps,
  SectionIdentifierAutomaticPasskeyUpgradesSwitch,
  SectionIdentifierGooglePasswordManagerPin,
  SectionIdentifierOnDeviceEncryption,
  SectionIdentifierExportPasswordsButton,
  SectionIdentifierImportPasswordsButton,
  SectionIdentifierDeleteCredentialsButton,
};

// Accessibility ID for the dialog asking users to confirm the export of their
// passwords.
extern NSString* const kPasswordSettingsExportConfirmViewId;

// Accessibility ID for the switch controlling the "Offer to Save Passwords"
// setting.
extern NSString* const kPasswordSettingsSavePasswordSwitchTableViewId;
extern NSString* const kPasswordSettingsManagedSavePasswordSwitchTableViewId;

// Accessibility IDs for the sections/items pertaining to bulk move passwords to
// account.
extern NSString* const
    kPasswordSettingsBulkMovePasswordsToAccountDescriptionTableViewId;
extern NSString* const
    kPasswordSettingsBulkMovePasswordsToAccountButtonTableViewId;
extern NSString* const kPasswordSettingsBulkMovePasswordsToAccountAlertViewId;

// Accessibility ID for the UITableView in Password Settings.
extern NSString* const kPasswordsSettingsTableViewId;

// Accessibility ID for the row showing status of Passwords in Other Apps.
extern NSString* const kPasswordSettingsPasswordsInOtherAppsRowId;

// Accessibility IDs for on-device encryption elements.
extern NSString* const kPasswordSettingsOnDeviceEncryptionOptInId;
extern NSString* const kPasswordSettingsOnDeviceEncryptionLearnMoreId;
extern NSString* const kPasswordSettingsOnDeviceEncryptionOptedInTextId;
extern NSString* const kPasswordSettingsOnDeviceEncryptionSetUpId;

// Accessibility IDs of change PIN elements.
extern NSString* const kPasswordSettingsChangePinButtonId;
extern NSString* const kPasswordSettingsChangePinDescriptionId;

#endif  // IOS_CHROME_BROWSER_SETTINGS_UI_BUNDLED_PASSWORD_PASSWORD_SETTINGS_PASSWORD_SETTINGS_CONSTANTS_H_
