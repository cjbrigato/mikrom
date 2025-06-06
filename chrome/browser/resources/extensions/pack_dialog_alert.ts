// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import 'chrome://resources/cr_elements/cr_dialog/cr_dialog.js';

import type {CrDialogElement} from 'chrome://resources/cr_elements/cr_dialog/cr_dialog.js';
import {assert, assertNotReached} from 'chrome://resources/js/assert.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {getCss} from './pack_dialog_alert.css.js';
import {getHtml} from './pack_dialog_alert.html.js';

export interface ExtensionsPackDialogAlertElement {
  $: {
    dialog: CrDialogElement,
  };
}

export class ExtensionsPackDialogAlertElement extends CrLitElement {
  static get is() {
    return 'extensions-pack-dialog-alert';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      model: {type: Object},
      cancelLabel_: {type: String},
      confirmLabel_: {type: String},
      title_: {type: String},
    };
  }

  accessor model: chrome.developerPrivate.PackDirectoryResponse = {
    message: '',
    item_path: '',
    pem_path: '',
    override_flags: 0,
    status: chrome.developerPrivate.PackStatus.SUCCESS,
  };
  protected accessor cancelLabel_: string|null = null;
  /** This needs to be initialized to trigger data-binding. */
  protected accessor confirmLabel_: string|null = '';
  protected accessor title_: string = '';

  get returnValue(): string {
    return this.$.dialog.getNative().returnValue;
  }

  override firstUpdated() {
    // Initialize button label values for initial html binding.
    this.cancelLabel_ = null;
    this.confirmLabel_ = null;

    switch (this.model.status) {
      case chrome.developerPrivate.PackStatus.WARNING:
        this.title_ = loadTimeData.getString('packDialogWarningTitle');
        this.cancelLabel_ = loadTimeData.getString('cancel');
        this.confirmLabel_ = loadTimeData.getString('packDialogProceedAnyway');
        break;
      case chrome.developerPrivate.PackStatus.ERROR:
        this.title_ = loadTimeData.getString('packDialogErrorTitle');
        this.cancelLabel_ = loadTimeData.getString('ok');
        break;
      case chrome.developerPrivate.PackStatus.SUCCESS:
        this.title_ = loadTimeData.getString('packDialogTitle');
        this.cancelLabel_ = loadTimeData.getString('ok');
        break;
      default:
        assertNotReached();
    }
  }

  protected getCancelButtonClass_(): string {
    return this.confirmLabel_ ? 'cancel-button' : 'action-button';
  }

  protected onCancelClick_() {
    this.$.dialog.cancel();
  }

  protected onConfirmClick_() {
    // The confirm button should only be available in WARNING state.
    assert(this.model.status === chrome.developerPrivate.PackStatus.WARNING);
    this.$.dialog.close();
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'extensions-pack-dialog-alert': ExtensionsPackDialogAlertElement;
  }
}

customElements.define(
    ExtensionsPackDialogAlertElement.is, ExtensionsPackDialogAlertElement);
