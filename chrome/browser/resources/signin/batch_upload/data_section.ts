// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '/strings.m.js';
import '//resources/cr_elements/cr_toggle/cr_toggle.js';
import '//resources/cr_elements/cr_checkbox/cr_checkbox.js';
import '//resources/cr_elements/cr_collapse/cr_collapse.js';
import '//resources/cr_elements/cr_expand_button/cr_expand_button.js';

import type {CrCollapseElement} from '//resources/cr_elements/cr_collapse/cr_collapse.js';
import type {CrExpandButtonElement} from '//resources/cr_elements/cr_expand_button/cr_expand_button.js';
import type {CrToggleElement} from '//resources/cr_elements/cr_toggle/cr_toggle.js';
import {assert} from '//resources/js/assert.js';
import {PluralStringProxyImpl} from '//resources/js/plural_string_proxy.js';
import {CrLitElement} from '//resources/lit/v3_0/lit.rollup.js';
import type {PropertyValues} from '//resources/lit/v3_0/lit.rollup.js';
import {getInstance as getAnnouncerInstance} from 'chrome://resources/cr_elements/cr_a11y_announcer/cr_a11y_announcer.js';
import {I18nMixinLit} from 'chrome://resources/cr_elements/i18n_mixin_lit.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';

import type {DataContainer} from './batch_upload.js';
import {getCss} from './data_section.css.js';
import {getHtml} from './data_section.html.js';

// Used for initialization only.
function createEmptyContainer(): DataContainer {
  return {
    sectionTitle: '',
    dataItems: [],
    isTheme: false,
  };
}

// Update request count, to be used along the transition duration to compute the
// interval time requests.
const UPDATE_REQUEST_COUNT: number = 10;

export interface DataSectionElement {
  $: {
    sectionTitle: HTMLElement,
    expandButton: CrExpandButtonElement,
    separator: HTMLElement,
    toggle: CrToggleElement,
    collapse: CrCollapseElement,
  };
}

const DataSectionElementBase = I18nMixinLit(CrLitElement);

export class DataSectionElement extends DataSectionElementBase {
  static get is() {
    return 'data-section';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      dataContainer: {type: Object},
      title_: {type: String},
      titleWithoutCount_: {type: String},
      expanded_: {type: Boolean},
      disabled_: {type: Boolean},
      dataSelectedCount_: {type: Number},
    };
  }

  // Data to be displayed.
  accessor dataContainer: DataContainer = createEmptyContainer();

  // Title of the section, updated on each item checkbox selection based on the
  // number of selected items.
  protected accessor title_: string = '';
  // Computed once on page load as it does not contain the selected item count.
  protected accessor titleWithoutCount_: string = '';

  // If the collapse section is exapnded.
  protected accessor expanded_: boolean = false;
  // If the section toggle is off.
  protected accessor disabled_: boolean = false;

  // Map containing the ids of the selected items in the section. Initialized
  // with all the ids of the section.
  // To be used as the output of the section as well for the parent element.
  dataSelected: Set<number> = new Set<number>();
  protected accessor dataSelectedCount_: number = 0;

  // Animation variables used to update the main view height based on the
  // collapse animation duration. Initialized to 0 and gets their values in
  // `firstUpdated()` which are not expected to be modified later.
  private intervalDurationOfUpdateHeightRequests_: number|null = null;
  private collapseAnimationDuration_: number = 0;

  override async connectedCallback() {
    super.connectedCallback();

    this.initializeSectionOutput_();

    // In tests this id may be empty.
    if (this.dataContainer.sectionTitle &&
        this.dataContainer.sectionTitle.length > 0) {
      this.titleWithoutCount_ =
          await PluralStringProxyImpl.getInstance().getPluralString(
              this.dataContainer.sectionTitle, 0);
    }
  }

  override firstUpdated() {
    // Compute the animation duration/intervals once on startup.
    this.collapseAnimationDuration_ =
        parseInt(getComputedStyle(this).getPropertyValue(
            '--collapse-transition-duration'));
    this.intervalDurationOfUpdateHeightRequests_ =
        this.collapseAnimationDuration_ / UPDATE_REQUEST_COUNT;
  }

  override async willUpdate(changedProperties: PropertyValues<this>) {
    super.willUpdate(changedProperties);

    // Cast necessary since `expanded_` is protected.
    const changedPrivateProperties =
        changedProperties as Map<PropertyKey, unknown>;

    // Make sure not to trigger updates before
    // `intervalDurationOfUpdateHeightRequests_` is initialized. `willUpdate`
    // may be called before `firstUpdated`.
    if (changedPrivateProperties.has('expanded_') &&
        this.intervalDurationOfUpdateHeightRequests_) {
      setTimeout(() => {
        this.updateViewHeightInterval_(
            this.intervalDurationOfUpdateHeightRequests_!);
      }, this.intervalDurationOfUpdateHeightRequests_);
    }

    if (changedPrivateProperties.has('dataSelectedCount_')) {
      // Make sure the id is valid before requesting the string. In tests this
      // id may be empty.
      if (this.dataContainer.sectionTitle &&
          this.dataContainer.sectionTitle.length > 0) {
        if (this.isThemeSection()) {
          // Themes construct its title with the content of its one and only
          // item title.
          this.title_ = this.i18n(
              this.dataContainer.sectionTitle,
              this.dataContainer.dataItems[0]!.title);
        } else {
          this.title_ =
              await PluralStringProxyImpl.getInstance().getPluralString(
                  this.dataContainer.sectionTitle, this.dataSelectedCount_);
        }
      }
    }
  }

  // Initializes the output variable based on the input.
  private initializeSectionOutput_() {
    this.dataSelected.clear();

    // And any section should not be empty.
    assert(
        this.dataContainer.dataItems !== undefined &&
            this.dataContainer.dataItems.length !== 0,
        'Sections should have at least one item to show.');

    this.dataContainer.dataItems.forEach((item) => {
      // Ids within a section should not be repeated.
      assert(
          !this.dataSelected.has(item.id),
          item.id + ' already exists in this section.' +
              ' An Id should be unique per section');
      this.dataSelected.add(item.id);
    });

    this.dataSelectedCount_ = this.dataSelected.size;
  }

  // Resets the element to the default based on the disabled state.
  private resetWithState_(disabled: boolean) {
    if (disabled) {
      this.dataSelected.clear();
      this.dataSelectedCount_ = 0;
    } else {
      this.initializeSectionOutput_();
    }

    this.expanded_ = false;
    this.disabled_ = disabled;
  }

  // Fire repetitive updates to the parent view height separated by the computed
  // interval, until the animation duration elapsed.
  private updateViewHeightInterval_(timeElapsed: number) {
    this.fire('update-view-height');
    // Animation time elapsed, animation should match the collapse animation. No
    // more view updates needed.
    if (timeElapsed >= this.collapseAnimationDuration_) {
      return;
    }

    // Trigger next update interval with the updated elapsed time.
    setTimeout(() => {
      this.updateViewHeightInterval_(
          timeElapsed + this.intervalDurationOfUpdateHeightRequests_!);
    }, this.intervalDurationOfUpdateHeightRequests_!);
  }

  // Needs to react to both property change (through a reset) and user action.
  protected onExpandChanged_(e: CustomEvent<{value: boolean}>) {
    this.expanded_ = e.detail.value;
  }

  // Needs to react to both property change (through a reset caused from all
  // checkboxes being unselected) and user action.
  protected onToggleChanged_(e: CustomEvent<{value: boolean}>) {
    this.resetWithState_(/*disabled=*/ !e.detail.value);

    // Notify the parent with the new toggle value.
    this.fire('toggle-changed', {toggle: e.detail.value});
  }

  protected getToggleAriaLabel_(): string {
    const selectedStr = this.disabled_ ? this.i18n('selectAllScreenReader') :
                                         this.i18n('selectNoneScreenReader');

    return [
      this.titleWithoutCount_,
      selectedStr,
    ].join('. ');
  }

  protected isCheckboxChecked_(itemId: number): boolean {
    return this.dataSelected.has(itemId);
  }

  protected onCheckedChanged_(e: CustomEvent<boolean>) {
    const currentTarget = e.currentTarget as HTMLElement;
    const itemId = Number(currentTarget.dataset['id']);

    // Check the checkbox value.
    if (e.detail) {
      this.dataSelected.add(itemId);
    } else {
      this.dataSelected.delete(itemId);
    }

    // Triggers update of the section title.
    this.dataSelectedCount_ = this.dataSelected.size;

    // If this is the last item unchecked then disable and reset the section
    // and focus the toggle since its value changed indirectly.
    if (this.dataSelectedCount_ === 0) {
      this.resetWithState_(/*disabled=*/ true);
      this.$.toggle.focus();
    }

    getAnnouncerInstance().announce(loadTimeData.getStringF(
        'itemCountSelectedScreenReader', this.dataSelectedCount_));
  }

  protected onCheckboxFocused_(e: Event) {
    const currentTarget = e.currentTarget as HTMLElement;
    const itemId = Number(currentTarget.dataset['id']);

    if (this.dataSelectedCount_ === 1 && this.dataSelected.has(itemId)) {
      getAnnouncerInstance().announce([
        this.titleWithoutCount_,
        this.i18n('lastItemSelectedScreenReader'),
      ].join('. '));
    }
  }

  // Theme section differs slightly from the regular section since it has always
  // a single item. Therefore the Ui is simplified not to show the expand button
  // and not giving access to items details.
  protected isThemeSection(): boolean {
    return this.dataContainer.isTheme;
  }

  protected isStrEmpty_(str: string) {
    return (!str || str.length === 0);
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'data-section': DataSectionElement;
  }
}

customElements.define(DataSectionElement.is, DataSectionElement);
