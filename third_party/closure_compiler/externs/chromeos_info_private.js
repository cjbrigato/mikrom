// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file was generated by:
//   tools/json_schema_compiler/compiler.py.
// NOTE: The format of types has changed. 'FooType' is now
//   'chrome.chromeosInfoPrivate.FooType'.
// Please run the closure compiler before committing changes.
// See https://chromium.googlesource.com/chromium/src/+/main/docs/closure_compilation.md

/**
 * @fileoverview Externs generated from namespace: chromeosInfoPrivate
 * @externs
 */

/** @const */
chrome.chromeosInfoPrivate = {};

/**
 * @enum {string}
 */
chrome.chromeosInfoPrivate.PropertyName = {
  TIMEZONE: 'timezone',
  A11Y_LARGE_CURSOR_ENABLED: 'a11yLargeCursorEnabled',
  A11Y_STICKY_KEYS_ENABLED: 'a11yStickyKeysEnabled',
  A11Y_SPOKEN_FEEDBACK_ENABLED: 'a11ySpokenFeedbackEnabled',
  A11Y_HIGH_CONTRAST_ENABLED: 'a11yHighContrastEnabled',
  A11Y_SCREEN_MAGNIFIER_ENABLED: 'a11yScreenMagnifierEnabled',
  A11Y_AUTO_CLICK_ENABLED: 'a11yAutoClickEnabled',
  A11Y_VIRTUAL_KEYBOARD_ENABLED: 'a11yVirtualKeyboardEnabled',
  A11Y_CARET_HIGHLIGHT_ENABLED: 'a11yCaretHighlightEnabled',
  A11Y_CURSOR_HIGHLIGHT_ENABLED: 'a11yCursorHighlightEnabled',
  A11Y_FOCUS_HIGHLIGHT_ENABLED: 'a11yFocusHighlightEnabled',
  A11Y_SELECT_TO_SPEAK_ENABLED: 'a11ySelectToSpeakEnabled',
  A11Y_SWITCH_ACCESS_ENABLED: 'a11ySwitchAccessEnabled',
  A11Y_CURSOR_COLOR_ENABLED: 'a11yCursorColorEnabled',
  A11Y_DOCKED_MAGNIFIER_ENABLED: 'a11yDockedMagnifierEnabled',
  SEND_FUNCTION_KEYS: 'sendFunctionKeys',
};

/**
 * @enum {string}
 */
chrome.chromeosInfoPrivate.SessionType = {
  NORMAL: 'normal',
  KIOSK: 'kiosk',
  PUBLIC_SESSION: 'public session',
};

/**
 * @enum {string}
 */
chrome.chromeosInfoPrivate.PlayStoreStatus = {
  NOT_AVAILABLE: 'not available',
  AVAILABLE: 'available',
  ENABLED: 'enabled',
};

/**
 * @enum {string}
 */
chrome.chromeosInfoPrivate.ManagedDeviceStatus = {
  MANAGED: 'managed',
  NOT_MANAGED: 'not managed',
};

/**
 * @enum {string}
 */
chrome.chromeosInfoPrivate.DeviceType = {
  CHROMEBASE: 'chromebase',
  CHROMEBIT: 'chromebit',
  CHROMEBOOK: 'chromebook',
  CHROMEBOX: 'chromebox',
  CHROMEDEVICE: 'chromedevice',
};

/**
 * @enum {string}
 */
chrome.chromeosInfoPrivate.StylusStatus = {
  UNSUPPORTED: 'unsupported',
  SUPPORTED: 'supported',
  SEEN: 'seen',
};

/**
 * @enum {string}
 */
chrome.chromeosInfoPrivate.AssistantStatus = {
  UNSUPPORTED: 'unsupported',
  SUPPORTED: 'supported',
};

/**
 * Fetches customization values for the given property names. See property names
 * in the declaration of the returned dictionary.
 * @param {!Array<string>} propertyNames Chrome OS Property names
 * @param {function({
 *   board: (string|undefined),
 *   customizationId: (string|undefined),
 *   homeProvider: (string|undefined),
 *   hwid: (string|undefined),
 *   deviceRequisition: (string|undefined),
 *   isMeetDevice: (boolean|undefined),
 *   initialLocale: (string|undefined),
 *   isOwner: (boolean|undefined),
 *   sessionType: (!chrome.chromeosInfoPrivate.SessionType|undefined),
 *   playStoreStatus: (!chrome.chromeosInfoPrivate.PlayStoreStatus|undefined),
 *   managedDeviceStatus: (!chrome.chromeosInfoPrivate.ManagedDeviceStatus|undefined),
 *   deviceType: (!chrome.chromeosInfoPrivate.DeviceType|undefined),
 *   stylusStatus: (!chrome.chromeosInfoPrivate.StylusStatus|undefined),
 *   assistantStatus: (!chrome.chromeosInfoPrivate.AssistantStatus|undefined),
 *   clientId: (string|undefined),
 *   timezone: (string|undefined),
 *   a11yLargeCursorEnabled: (boolean|undefined),
 *   a11yStickyKeysEnabled: (boolean|undefined),
 *   a11ySpokenFeedbackEnabled: (boolean|undefined),
 *   a11yHighContrastEnabled: (boolean|undefined),
 *   a11yScreenMagnifierEnabled: (boolean|undefined),
 *   a11yAutoClickEnabled: (boolean|undefined),
 *   a11yVirtualKeyboardEnabled: (boolean|undefined),
 *   a11yCaretHighlightEnabled: (boolean|undefined),
 *   a11yCursorHighlightEnabled: (boolean|undefined),
 *   a11yFocusHighlightEnabled: (boolean|undefined),
 *   a11ySelectToSpeakEnabled: (boolean|undefined),
 *   a11ySwitchAccessEnabled: (boolean|undefined),
 *   a11yCursorColorEnabled: (boolean|undefined),
 *   a11yDockedMagnifierEnabled: (boolean|undefined),
 *   sendFunctionKeys: (boolean|undefined),
 *   supportedTimezones: (!Array<!Array<string>>|undefined)
 * }): void} callback
 */
chrome.chromeosInfoPrivate.get = function(propertyNames, callback) {};

/**
 * Sets values for the given system property.
 * @param {!chrome.chromeosInfoPrivate.PropertyName} propertyName Chrome OS
 *     system property name
 * @param {*} propertyValue Chrome OS system property value
 */
chrome.chromeosInfoPrivate.set = function(propertyName, propertyValue) {};

/**
 * Called to request tablet mode enabled status from the Chrome OS system.
 * @param {function(boolean): void} callback Returns tablet mode enabled status
 *     as a boolean.
 */
chrome.chromeosInfoPrivate.isTabletModeEnabled = function(callback) {};
