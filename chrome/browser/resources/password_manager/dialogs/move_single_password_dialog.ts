// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import 'chrome://resources/cr_elements/cr_dialog/cr_dialog.js';
import '../shared_style.css.js';
import '../user_utils_mixin.js';
import './password_preview_item.js';

import type {CrButtonElement} from 'chrome://resources/cr_elements/cr_button/cr_button.js';
import type {CrDialogElement} from 'chrome://resources/cr_elements/cr_dialog/cr_dialog.js';
import {I18nMixin} from 'chrome://resources/cr_elements/i18n_mixin.js';
import {assert} from 'chrome://resources/js/assert.js';
import {PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {PasswordManagerImpl} from '../password_manager_proxy.js';
import {UserUtilMixin} from '../user_utils_mixin.js';

import {getTemplate} from './move_single_password_dialog.html.js';

/**
 * This should be kept in sync with the enum in
 * components/password_manager/core/browser/password_manager_metrics_util.h.
 * These values are persisted to logs. Entries should not be renumbered and
 * numeric values should never be reused.
 * @enum {number}
 */
export enum MoveToAccountStoreTrigger {
  // LINT.IfChange
  SUCCESSFUL_LOGIN_WITH_PROFILE_STORE_PASSWORD = 0,
  EXPLICITLY_TRIGGERED_IN_SETTINGS = 1,
  EXPLICITLY_TRIGGERED_FOR_MULTIPLE_PASSWORDS_IN_SETTINGS = 2,
  USER_OPTED_IN_AFTER_SAVING_LOCALLY = 3,
  EXPLICITLY_TRIGGERED_FOR_SINGLE_PASSWORD_IN_DETAILS_IN_SETTINGS = 4,
  COUNT = 5,
  // LINT.ThenChange(//tools/metrics/histograms/metadata/password/enums.xml)
}


export interface MoveSinglePasswordDialogElement {
  $: {
    accountEmail: HTMLElement,
    avatar: HTMLImageElement,
    cancel: CrButtonElement,
    dialog: CrDialogElement,
    move: CrButtonElement,
    title: HTMLElement,
    description: HTMLElement,
  };
}

const MovePasswordsDialogElementBase = UserUtilMixin(I18nMixin(PolymerElement));

export class MoveSinglePasswordDialogElement extends
    MovePasswordsDialogElementBase {
  static get is() {
    return 'move-single-password-dialog';
  }

  static get template() {
    return getTemplate();
  }

  static get properties() {
    return {
      /**
       * Password UI entry.
       */
      password: Object,
    };
  }

  declare password: chrome.passwordsPrivate.PasswordUiEntry;

  override connectedCallback() {
    super.connectedCallback();

    chrome.metricsPrivate.recordEnumerationValue(
        'PasswordManager.AccountStorage.MoveToAccountStoreFlowOffered',
        MoveToAccountStoreTrigger
            .EXPLICITLY_TRIGGERED_FOR_SINGLE_PASSWORD_IN_DETAILS_IN_SETTINGS,
        MoveToAccountStoreTrigger.COUNT);

    this.$.dialog.showModal();
  }

  private onCancel_() {
    this.$.dialog.cancel();
  }

  private onMoveButtonClick_() {
    assert(this.isAccountStorageEnabled);
    PasswordManagerImpl.getInstance().movePasswordsToAccount(
        [this.password.id]);
    this.dispatchEvent(new CustomEvent('passwords-moved', {
      bubbles: true,
      composed: true,
      detail: {accountEmail: this.accountEmail, numberOfPasswords: 1},
    }));

    this.$.dialog.close();
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'move-single-password-dialog': MoveSinglePasswordDialogElement;
  }
}

customElements.define(
    MoveSinglePasswordDialogElement.is, MoveSinglePasswordDialogElement);
