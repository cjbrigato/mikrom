// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '//resources/cr_elements/cr_radio_group/cr_radio_group.js';

import {CrLitElement} from '//resources/lit/v3_0/lit.rollup.js';

import {getCss} from './segmented_button.css.js';
import {getHtml} from './segmented_button.html.js';

export class SegmentedButtonElement extends CrLitElement {
  static get is() {
    return 'segmented-button';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      selected: {
        type: String,
        notify: true,
      },
      selectableElements: {type: String},
      groupAriaLabel: {type: String},
    };
  }

  accessor selected: string|undefined;
  accessor selectableElements: string = 'segmented-button-option';
  accessor groupAriaLabel: string = '';

  protected onSelectedChanged_(e: CustomEvent<{value: string}>) {
    this.selected = e.detail.value;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'segmented-button': SegmentedButtonElement;
  }
}

customElements.define(SegmentedButtonElement.is, SegmentedButtonElement);
