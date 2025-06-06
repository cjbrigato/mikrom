// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
/**
 * @fileoverview A lightweight toast.
 */
import {CrLitElement} from '//resources/lit/v3_0/lit.rollup.js';
import type {PropertyValues} from '//resources/lit/v3_0/lit.rollup.js';

import {getCss} from './cr_toast.css.js';
import {getHtml} from './cr_toast.html.js';

export class CrToastElement extends CrLitElement {
  static get is() {
    return 'cr-toast';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      duration: {
        type: Number,
      },

      open: {
        type: Boolean,
        reflect: true,
      },
    };
  }

  accessor duration: number = 0;
  accessor open: boolean = false;
  private hideTimeoutId_: number|null = null;

  constructor() {
    super();
    this.addEventListener('focusin', this.clearTimeout_);
    this.addEventListener('focusout', this.resetAutoHide_);
  }

  override willUpdate(changedProperties: PropertyValues<this>) {
    super.willUpdate(changedProperties);

    if (changedProperties.has('duration') || changedProperties.has('open')) {
      this.resetAutoHide_();
    }
  }

  private clearTimeout_() {
    if (this.hideTimeoutId_ !== null) {
      window.clearTimeout(this.hideTimeoutId_);
      this.hideTimeoutId_ = null;
    }
  }

  /**
   * Cancels existing auto-hide, and sets up new auto-hide.
   */
  private resetAutoHide_() {
    this.clearTimeout_();

    if (this.open && this.duration !== 0) {
      this.hideTimeoutId_ = window.setTimeout(() => {
        this.hide();
      }, this.duration);
    }
  }

  /**
   * Shows the toast and auto-hides after |this.duration| milliseconds has
   * passed. If the toast is currently being shown, any preexisting auto-hide
   * is cancelled and replaced with a new auto-hide.
   */
  async show() {
    // Force autohide to reset if calling show on an already shown toast.
    const shouldResetAutohide = this.open;

    // The role attribute is removed first so that screen readers to better
    // ensure that screen readers will read out the content inside the toast.
    // If the role is not removed and re-added back in, certain screen readers
    // do not read out the contents, especially if the text remains exactly
    // the same as a previous toast.
    this.removeAttribute('role');

    this.open = true;
    await this.updateComplete;
    this.setAttribute('role', 'alert');

    if (shouldResetAutohide) {
      this.resetAutoHide_();
    }
  }

  /**
   * Hides the toast and ensures that its contents can not be focused while
   * hidden.
   */
  async hide() {
    this.open = false;
    await this.updateComplete;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'cr-toast': CrToastElement;
  }
}

customElements.define(CrToastElement.is, CrToastElement);
