// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import type {LifetimeBrowserProxy} from 'chrome://settings/settings.js';
import {TestBrowserProxy} from 'chrome://webui-test/test_browser_proxy.js';

/**
 * A test version of LifetimeBrowserProxy.
 */
export class TestLifetimeBrowserProxy extends TestBrowserProxy implements
    LifetimeBrowserProxy {
  // <if expr="not is_chromeos">
  private shouldShowRelaunchDialog_: boolean = false;
  private relaunchConfirmationDialogDescription_: string|null = null;
  // </if>

  constructor() {
    super([
      'restart', 'relaunch',

      // <if expr="not is_chromeos">
      'shouldShowRelaunchDialog', 'getRelaunchConfirmationDialogDescription',
      // </if>

      // <if expr="is_chromeos">
      'signOutAndRestart', 'factoryReset',
      // </if>
    ]);
  }

  restart() {
    this.methodCalled('restart');
  }

  relaunch() {
    this.methodCalled('relaunch');
  }

  // <if expr="not is_chromeos">
  shouldShowRelaunchConfirmationDialog(alwaysShowDialog: boolean) {
    this.methodCalled('shouldShowRelaunchDialog', alwaysShowDialog);
    return Promise.resolve(this.shouldShowRelaunchDialog_);
  }

  setShouldShowRelaunchConfirmationDialog(value: boolean) {
    this.shouldShowRelaunchDialog_ = value;
  }

  setRelaunchConfirmationDialogDescription(value: string) {
    this.relaunchConfirmationDialogDescription_ = value;
  }

  getRelaunchConfirmationDialogDescription(isVersionUpdate: boolean) {
    this.methodCalled(
        'getRelaunchConfirmationDialogDescription', isVersionUpdate);
    return Promise.resolve(this.relaunchConfirmationDialogDescription_);
  }
  // </if>

  // <if expr="is_chromeos">
  signOutAndRestart() {
    this.methodCalled('signOutAndRestart');
  }

  factoryReset(requestTpmFirmwareUpdate: boolean) {
    this.methodCalled('signOutAndRestart', requestTpmFirmwareUpdate);
  }
  //  </if>
}
