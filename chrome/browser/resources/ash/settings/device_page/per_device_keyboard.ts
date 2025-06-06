// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'per-device-keyboard-settings' allow users to configure their keyboard
 * settings for each device in system settings.
 */

import '../settings_shared.css.js';
import 'chrome://resources/ash/common/cr_elements/localized_link/localized_link.js';
import 'chrome://resources/ash/common/cr_elements/cr_radio_button/cr_radio_button.js';
import 'chrome://resources/ash/common/cr_elements/cr_shared_vars.css.js';
import 'chrome://resources/ash/common/cr_elements/cr_slider/cr_slider.js';
import '../controls/settings_radio_group.js';
import '../controls/settings_slider.js';
import '../controls/settings_toggle_button.js';
import './per_device_keyboard_subsection.js';

import {getInstance as getAnnouncerInstance} from 'chrome://resources/ash/common/cr_elements/cr_a11y_announcer/cr_a11y_announcer.js';
import {I18nMixin} from 'chrome://resources/ash/common/cr_elements/i18n_mixin.js';
import type {PolymerElementProperties} from 'chrome://resources/polymer/v3_0/polymer/interfaces.js';
import {PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {DeepLinkingMixin} from '../common/deep_linking_mixin.js';
import {RouteObserverMixin} from '../common/route_observer_mixin.js';
import type {PrefsState} from '../common/types.js';
import type {KeyboardPolicies} from '../mojom-webui/input_device_settings.mojom-webui.js';
import {Setting} from '../mojom-webui/setting.mojom-webui.js';
import type {Route} from '../router.js';
import {Router, routes} from '../router.js';

import type {DevicePageBrowserProxy} from './device_page_browser_proxy.js';
import {DevicePageBrowserProxyImpl} from './device_page_browser_proxy.js';
import type {Keyboard} from './input_device_settings_types.js';
import {getDeviceStateChangesToAnnounce} from './input_device_settings_utils.js';
import {getTemplate} from './per_device_keyboard.html.js';

const SettingsPerDeviceKeyboardElementBase =
    DeepLinkingMixin(RouteObserverMixin(I18nMixin(PolymerElement)));

export class SettingsPerDeviceKeyboardElement extends
    SettingsPerDeviceKeyboardElementBase {
  static get is() {
    return 'settings-per-device-keyboard';
  }

  static get template(): HTMLTemplateElement {
    return getTemplate();
  }

  static get properties(): PolymerElementProperties {
    return {
      /** Preferences state. Used for auto repeat settings. */
      prefs: {
        type: Object,
        notify: true,
      },

      keyboards: {
        type: Array,
        observer: 'onKeyboardListUpdated',
      },

      keyboardPolicies: {
        type: Object,
      },

      /**
       * Auto-repeat delays (in ms) for the corresponding slider values, from
       * long to short. The values were chosen to provide a large range while
       * giving several options near the defaults.
       */
      autoRepeatDelays: {
        type: Array,
        value() {
          return [150, 200, 300, 500, 1000, 1500, 2000];
        },
        readOnly: true,
      },

      /**
       * Auto-repeat intervals (in ms) for the corresponding slider values, from
       * long to short. The slider itself is labeled "rate", the inverse of
       * interval, and goes from slow (long interval) to fast (short interval).
       */
      autoRepeatIntervals: {
        type: Array,
        value: [2000, 1000, 500, 300, 200, 100, 50, 30, 20],
        readOnly: true,
      },
    };
  }

  prefs: PrefsState;

  // DeepLinkingMixin override
  override supportedSettingIds = new Set<Setting>([
    Setting.kKeyboardAutoRepeat,
    Setting.kKeyboardShortcuts,
  ]);

  protected keyboards: Keyboard[];
  protected keyboardPolicies: KeyboardPolicies;
  private autoRepeatDelays: number[];
  private autoRepeatIntervals: number[];
  private browserProxy: DevicePageBrowserProxy =
      DevicePageBrowserProxyImpl.getInstance();

  override connectedCallback(): void {
    super.connectedCallback();

    this.browserProxy.initializeKeyboard();
  }

  override currentRouteChanged(route: Route): void {
    // Does not apply to this page.
    if (route !== routes.PER_DEVICE_KEYBOARD) {
      return;
    }

    this.attemptDeepLink();
  }

  private onKeyboardListUpdated(
      newKeyboardList: Keyboard[],
      oldKeyboardList: Keyboard[]|undefined): void {
    if (!oldKeyboardList) {
      return;
    }
    const {msgId, deviceNames} =
        getDeviceStateChangesToAnnounce(newKeyboardList, oldKeyboardList);
    for (const deviceName of deviceNames) {
      getAnnouncerInstance().announce(this.i18n(msgId, deviceName));
    }
  }

  private onShowShortcutCustomizationAppClick(): void {
    this.browserProxy.showShortcutCustomizationApp();
  }

  private onShowInputSettingsClick(): void {
    Router.getInstance().navigateTo(
        routes.OS_LANGUAGES_INPUT,
        /*dynamicParams=*/ undefined, /*removeSearch=*/ true);
  }

  private onShowA11yKeyboardSettingsClick(): void {
    Router.getInstance().navigateTo(
        routes.A11Y_KEYBOARD_AND_TEXT_INPUT,
        /*dynamicParams=*/ undefined, /*removeSearch=*/ true);
  }

  protected hasKeyboards(): boolean {
    return this.keyboards.length > 0;
  }

  private computeIsLastDevice(index: number): boolean {
    return index === this.keyboards.length - 1;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-per-device-keyboard': SettingsPerDeviceKeyboardElement;
  }
}

customElements.define(
    SettingsPerDeviceKeyboardElement.is, SettingsPerDeviceKeyboardElement);
