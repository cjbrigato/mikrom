// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import type {PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';
import {Debouncer, dedupingMixin, timeOut} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

/**
 * Helper functions for a select with timeout. Implemented by select settings
 * sections, so that the preview does not immediately begin generating and
 * freeze the dropdown when the value is changed.
 * Assumes that the elements using this mixin have no more than one <select>
 * element. Clients should:
 * (1) Single-bind `selectedValue` to the <select>'s `value` property.
 * (2) Register `onSelectChange` as an event listener for the <select>'s
 *     `change` event.
 * (3) Override `onProcessSelectChange` to receive notifications of new values
 *     set from the UI.
 */

type Constructor<T> = new (...args: any[]) => T;

export const SelectMixin = dedupingMixin(
    <T extends Constructor<PolymerElement>>(superClass: T): T&
    Constructor<SelectMixinInterface> => {
      class SelectMixin extends superClass {
        static get properties() {
          return {
            selectedValue: {type: String},
          };
        }

        selectedValue: string;
        private debouncer_: Debouncer|null = null;

        onSelectChange(e: Event) {
          const newValue = (e.target as HTMLSelectElement).value;
          this.debouncer_ = Debouncer.debounce(
              this.debouncer_, timeOut.after(100),
              () => this.callProcessSelectChange_(newValue));
        }

        private callProcessSelectChange_(newValue: string) {
          if (!this.isConnected || newValue === this.selectedValue) {
            return;
          }
          this.selectedValue = newValue;
          this.onProcessSelectChange(newValue);
          // For testing only
          this.dispatchEvent(new CustomEvent(
              'process-select-change', {bubbles: true, composed: true}));
        }

        onProcessSelectChange(_value: string) {}
      }

      return SelectMixin;
    });

export interface SelectMixinInterface {
  selectedValue: string;

  /**
   * Should be overridden by elements using this mixin to receive select
   * value updates.
   * @param value The new select value to process.
   */
  onProcessSelectChange(value: string): void;
}
