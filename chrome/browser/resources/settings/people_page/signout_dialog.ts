// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview 'settings-signout-dialog' is a dialog that allows the
 * user to turn off sync and sign out of Chromium.
 */
import '//resources/cr_elements/cr_button/cr_button.js';
import '//resources/cr_elements/cr_checkbox/cr_checkbox.js';
import '//resources/cr_elements/cr_collapse/cr_collapse.js';
import '//resources/cr_elements/cr_dialog/cr_dialog.js';
import '//resources/cr_elements/cr_expand_button/cr_expand_button.js';
import '//resources/cr_elements/cr_shared_style.css.js';
import '//resources/cr_elements/cr_shared_vars.css.js';
import '../settings_shared.css.js';

import type {CrDialogElement} from '//resources/cr_elements/cr_dialog/cr_dialog.js';
import {WebUiListenerMixin} from '//resources/cr_elements/web_ui_listener_mixin.js';
import {sanitizeInnerHtml} from '//resources/js/parse_html_subset.js';
import {microTask, PolymerElement} from '//resources/polymer/v3_0/polymer/polymer_bundled.min.js';
import {ProfileInfoBrowserProxyImpl} from '/shared/settings/people_page/profile_info_browser_proxy.js';
import type {SyncStatus} from '/shared/settings/people_page/sync_browser_proxy.js';
import {SignedInState, SyncBrowserProxyImpl} from '/shared/settings/people_page/sync_browser_proxy.js';

import {loadTimeData} from '../i18n_setup.js';

import {getTemplate} from './signout_dialog.html.js';

export interface SettingsSignoutDialogElement {
  $: {
    dialog: CrDialogElement,
    disconnectConfirm: HTMLElement,
  };
}

const SettingsSignoutDialogElementBase = WebUiListenerMixin(PolymerElement);

export class SettingsSignoutDialogElement extends
    SettingsSignoutDialogElementBase {
  static get is() {
    return 'settings-signout-dialog';
  }

  static get template() {
    return getTemplate();
  }

  static get properties() {
    return {
      /**
       * The current sync status, supplied by the parent.
       */
      syncStatus: {
        type: Object,
        observer: 'syncStatusChanged_',
      },

      /**
       * True if the checkbox to delete the profile has been checked.
       */
      deleteProfile_: Boolean,

      /**
       * True if the profile deletion warning is visible.
       */
      deleteProfileWarningVisible_: Boolean,

      /**
       * The profile deletion warning. The message indicates the number of
       * profile stats that will be deleted if a non-zero count for the profile
       * stats is returned from the browser.
       */
      deleteProfileWarning_: String,
    };
  }

  declare syncStatus: SyncStatus|null;
  declare private deleteProfile_: boolean;
  declare private deleteProfileWarningVisible_: boolean;
  declare private deleteProfileWarning_: string;

  override connectedCallback() {
    super.connectedCallback();

    this.addWebUiListener(
        'profile-stats-count-ready', this.handleProfileStatsCount_.bind(this));
    // <if expr="not chromeos_ash">
    ProfileInfoBrowserProxyImpl.getInstance().getProfileStatsCount();
    // </if>
    microTask.run(() => {
      this.$.dialog.showModal();
    });
  }

  /**
   * @return true when the user selected 'Confirm'.
   */
  wasConfirmed(): boolean {
    return this.$.dialog.getNative().returnValue === 'success';
  }

  /**
   * Handler for when the profile stats count is pushed from the browser.
   */
  private handleProfileStatsCount_(count: number) {
    const username = this.syncStatus!.signedInUsername || '';
    if (count === 0) {
      this.deleteProfileWarning_ = loadTimeData.getStringF(
          'deleteProfileWarningWithoutCounts', username);
    } else if (count === 1) {
      this.deleteProfileWarning_ = loadTimeData.getStringF(
          'deleteProfileWarningWithCountsSingular', username);
    } else {
      this.deleteProfileWarning_ = loadTimeData.getStringF(
          'deleteProfileWarningWithCountsPlural', count, username);
    }
  }

  /**
   * Polymer observer for syncStatus.
   */
  private syncStatusChanged_() {
    if (!!this.syncStatus &&
        this.syncStatus.signedInState !== SignedInState.SYNCING &&
        this.$.dialog.open) {
      this.$.dialog.close();
    }
  }

  // <if expr="not chromeos_ash">
  private getDisconnectExplanationHtml_(domain: string): TrustedHTML {
    if (domain) {
      return sanitizeInnerHtml(loadTimeData.getStringF(
          'syncDisconnectManagedProfileExplanation', `<span>${domain}</span>`));
    }
    return sanitizeInnerHtml(
        loadTimeData.getString('syncDisconnectExplanation'));
  }
  // </if>

  // <if expr="chromeos_ash">
  private getDisconnectExplanationHtml_(_domain: string): TrustedHTML {
    return sanitizeInnerHtml(
        loadTimeData.getString('syncDisconnectExplanation'));
  }
  // </if>

  private onDisconnectCancel_() {
    this.$.dialog.cancel();
  }

  private onDisconnectConfirm_() {
    this.$.dialog.close();
    // <if expr="not chromeos_ash">
    SyncBrowserProxyImpl.getInstance().signOut(this.deleteProfile_);
    // </if>
    // <if expr="chromeos_ash">
    // Chrome OS users are always signed-in, so just turn off sync.
    SyncBrowserProxyImpl.getInstance().turnOffSync();
    // </if>
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-signout-dialog': SettingsSignoutDialogElement;
  }
}

customElements.define(
    SettingsSignoutDialogElement.is, SettingsSignoutDialogElement);
