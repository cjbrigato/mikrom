// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://resources/cr_elements/cr_dialog/cr_dialog.js';

import type {CrDialogElement} from 'chrome://resources/cr_elements/cr_dialog/cr_dialog.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {DocumentMetadata} from '../constants.js';

import {getCss} from './viewer_properties_dialog.css.js';
import {getHtml} from './viewer_properties_dialog.html.js';

export interface ViewerPropertiesDialogElement {
  $: {
    dialog: CrDialogElement,
    close: HTMLElement,
  };
}

export class ViewerPropertiesDialogElement extends CrLitElement {
  static get is() {
    return 'viewer-properties-dialog';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      documentMetadata: {type: Object},
      fileName: {type: String},
      pageCount: {type: Number},
      strings: {type: Object},
    };
  }

  accessor documentMetadata: DocumentMetadata = {
    author: '',
    canSerializeDocument: false,
    creationDate: '',
    creator: '',
    fileSize: '',
    keywords: '',
    linearized: false,
    modDate: '',
    pageSize: '',
    producer: '',
    subject: '',
    title: '',
    version: '',
  };
  accessor fileName: string = '';
  accessor pageCount: number = 0;
  accessor strings: {[key: string]: string}|undefined;

  protected getFastWebViewValue_(): string {
    if (!this.strings) {
      return '';
    }
    return loadTimeData.getString(
        this.documentMetadata.linearized ? 'propertiesFastWebViewYes' :
                                           'propertiesFastWebViewNo');
  }

  protected getOrPlaceholder_(value: string): string {
    return value || '-';
  }

  protected onClickClose_() {
    this.$.dialog.close();
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'viewer-properties-dialog': ViewerPropertiesDialogElement;
  }
}

customElements.define(
    ViewerPropertiesDialogElement.is, ViewerPropertiesDialogElement);
