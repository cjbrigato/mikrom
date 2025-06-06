// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SidebarElement} from './sidebar.js';

export function getHtml(this: SidebarElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
<print-preview-header id="header" .destination="${this.destination}"
    error="${this.error}" .state="${this.state}"
    ?managed="${this.controlsManaged}">
</print-preview-header>
<div id="container" show-bottom-shadow>
  <print-preview-destination-settings id="destinationSettings"
      ?dark="${this.inDarkMode}"
      @destination-changed="${this.onDestinationChanged_}"
      @destination-state-changed="${this.onDestinationStateChanged_}"
      error="${this.error}" @error-changed="${this.onErrorChanged_}"
      ?first-load="${this.firstLoad_}"
      .state="${this.state}" ?app-kiosk-mode="${this.isInAppKioskMode_}"
      ?disabled="${this.controlsDisabled_}"
      available class="settings-section"
      @destination-capabilities-changed="${this.onDestinationCapabilitiesChanged_}">
  </print-preview-destination-settings>
  <print-preview-pages-settings
      page-count="${this.pageCount}" ?disabled="${this.controlsDisabled_}"
      ?hidden="${!this.settingsAvailable_.pages}" class="settings-section">
  </print-preview-pages-settings>
  <print-preview-copies-settings
      .capability="${this.destinationCapabilities_?.printer.copies || null}"
      ?disabled="${this.controlsDisabled_}"
      ?hidden="${!this.settingsAvailable_.copies}" class="settings-section">
  </print-preview-copies-settings>
  <print-preview-layout-settings
      ?disabled="${this.controlsDisabled_}"
      ?hidden="${!this.settingsAvailable_.layout}" class="settings-section">
  </print-preview-layout-settings>
  <print-preview-color-settings
      ?disabled="${this.controlsDisabled_}"
      ?hidden="${!this.settingsAvailable_.color}" class="settings-section">
  </print-preview-color-settings>
  <print-preview-more-settings
      ?settings-expanded-by-user="${this.settingsExpandedByUser_}"
      @settings-expanded-by-user-changed="${this.onSettingsExpandedByUserChanged_}"
      ?disabled="${this.controlsDisabled_}"
      ?hidden="${!this.shouldShowMoreSettings_}">
  </print-preview-more-settings>
  <cr-collapse id="moreSettings"
      ?opened="${this.shouldExpandSettings_()}">
    <print-preview-media-size-settings
        .capability="${this.destinationCapabilities_?.printer.media_size || null}"
        ?disabled="${this.controlsDisabled_}"
        ?hidden="${!this.settingsAvailable_.mediaSize}"
        class="settings-section">
    </print-preview-media-size-settings>
    <print-preview-pages-per-sheet-settings
        ?disabled="${this.controlsDisabled_}"
        ?hidden="${!this.settingsAvailable_.pagesPerSheet}"
        class="settings-section">
    </print-preview-pages-per-sheet-settings>
    <print-preview-margins-settings .state="${this.state}"
        ?disabled="${this.controlsDisabled_}"
        ?hidden="${!this.settingsAvailable_.margins}"
        class="settings-section">
    </print-preview-margins-settings>
    <print-preview-dpi-settings
        .capability="${this.destinationCapabilities_?.printer.dpi || null}"
        ?disabled="${this.controlsDisabled_}"
        ?hidden="${!this.settingsAvailable_.dpi}" class="settings-section">
    </print-preview-dpi-settings>
    <print-preview-scaling-settings
        ?disabled="${this.controlsDisabled_}" ?is-pdf="${this.isPdf}"
        ?hidden="${!this.settingsAvailable_.scaling}"
        class="settings-section">
    </print-preview-scaling-settings>
    <print-preview-duplex-settings
        ?disabled="${this.controlsDisabled_}" ?dark="${this.inDarkMode}"
        ?hidden="${!this.settingsAvailable_.duplex}"
        class="settings-section">
    </print-preview-duplex-settings>
    <print-preview-other-options-settings
        ?disabled="${this.controlsDisabled_}"
        ?hidden="${!this.settingsAvailable_.otherOptions}"
        class="settings-section">
    </print-preview-other-options-settings>
    <print-preview-advanced-options-settings
        .destination="${this.destination}"
        ?disabled="${this.controlsDisabled_}"
        ?hidden="${!this.settingsAvailable_.vendorItems}"
        class="settings-section">
    </print-preview-advanced-options-settings>
    <print-preview-link-container .destination="${this.destination}"
        ?app-kiosk-mode="${this.isInAppKioskMode_}"
        ?disabled="${this.controlsDisabled_}">
    </print-preview-link-container>
  </cr-collapse>
</div>
<print-preview-button-strip .destination="${this.destination}"
    .state="${this.state}" ?first-load="${this.firstLoad_}"
    @print-button-focused="${this.onPrintButtonFocused_}">
</print-preview-button-strip>
<!--_html_template_end_-->`;
  // clang-format on
}
