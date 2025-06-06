// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {DestinationDialogElement} from './destination_dialog.js';

export function getHtml(this: DestinationDialogElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
<cr-dialog id="dialog" @close="${this.onCloseOrCancel_}">
  <div slot="title" id="header">$i18n{destinationSearchTitle}</div>
  <div slot="body">
    <print-preview-search-box id="searchBox"
        label="$i18n{searchBoxPlaceholder}" .searchQuery="${this.searchQuery_}"
        @search-query-changed="${this.onSearchQueryChanged_}"
        autofocus>
    </print-preview-search-box>
    <print-preview-destination-list id="printList"
        ?loading-destinations="${this.loadingDestinations_}"
        .searchQuery="${this.searchQuery_}"
        @destination-selected="${this.onDestinationSelected_}">
    </print-preview-destination-list>
  </div>
  <div slot="button-container">
    <cr-button @click="${this.onManageButtonClick_}">
      $i18n{manage}
      <cr-icon icon="cr:open-in-new" id="manageIcon"></cr-icon>
    </cr-button>
    <cr-button class="cancel-button" @click="${this.onCancelButtonClick_}">
      $i18n{cancel}
    </cr-button>
  </div>
</cr-dialog>
<!--_html_template_end_-->`;
  // clang-format on
}
