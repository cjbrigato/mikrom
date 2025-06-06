// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {AppElement} from './app.js';

export function getHtml(this: AppElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
<print-preview-state id="state"
    @state-changed="${this.onStateChanged_}" error="${this.error_}"
    @error-changed="${this.onErrorChanged_}">
</print-preview-state>
<print-preview-model id="model"
    @settings-managed-changed="${this.onSettingsManagedChanged_}"
    .destination="${this.destination_}"
    .documentSettings="${this.documentSettings_}"
    .margins="${this.margins_}" .pageSize="${this.pageSize_}"
    @preview-setting-changed="${this.onPreviewSettingChanged_}"
    @sticky-setting-changed="${this.onStickySettingChanged_}"
    @setting-valid-changed="${this.onSettingValidChanged_}">
</print-preview-model>
<print-preview-document-info id="documentInfo"
    @document-settings-changed="${this.onDocumentSettingsChanged_}"
    @margins-changed="${this.onMarginsChanged_}"
    @page-size-changed="${this.onPageSizeChanged_}">
</print-preview-document-info>
<div id="preview-area-container">
  <print-preview-preview-area id="previewArea"
      .destination="${this.destination_}" error="${this.error_}"
      @error-changed="${this.onErrorChanged_}"
      ?document-modifiable="${this.documentSettings_.isModifiable}"
      .margins="${this.margins_}" .pageSize="${this.pageSize_}"
      state="${this.state}" .measurementSystem="${this.measurementSystem_}"
      @preview-state-changed="${this.onPreviewStateChanged_}"
      @preview-start="${this.onPreviewStart_}">
  </print-preview-preview-area>
</div>
<print-preview-sidebar id="sidebar"
    @destination-state-changed="${this.onDestinationStateChanged_}"
    ?controls-managed="${this.controlsManaged_}"
    error="${this.error_}" @error-changed="${this.onErrorChanged_}"
    ?is-pdf="${!this.documentSettings_.isModifiable}"
    page-count="${this.documentSettings_.pageCount}" state="${this.state}"
    @focus="${this.onSidebarFocus_}"
    @destination-changed="${this.onDestinationChanged_}"
    @destination-capabilities-changed="${this.onDestinationCapabilitiesChanged_}"
    @print-with-system-dialog="${this.onPrintWithSystemDialog_}"
    @print-requested="${this.onPrintRequested_}"
    @cancel-requested="${this.onCancelRequested_}">
</print-preview-sidebar>
<!--_html_template_end_-->`;
  // clang-format on
}
