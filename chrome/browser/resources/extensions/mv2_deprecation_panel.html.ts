// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {ExtensionsMv2DeprecationPanelElement} from './mv2_deprecation_panel.js';

export function getHtml(this: ExtensionsMv2DeprecationPanelElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
<h2 class="panel-title" ?hidden="${!this.showTitle}">
  $i18n{mv2DeprecationPanelTitle}
</h2>

<div class="panel-background" id="panelContainer">
  <div class="panel-header">
    <cr-icon aria-hidden="true" icon="extensions-icons:my_extensions"
        class="panel-header-icon">
    </cr-icon>
    <div class="panel-header-text">
      <h3 id="headingText">${this.headerString_}</h3>
      <div class="cr-secondary-text" .innerHTML="${this.getSubtitleString_()}">
      </div>
    </div>
    <cr-button class="header-button" @click="${this.onDismissButtonClick_}">
      $i18n{mv2DeprecationPanelDismissButton}
    </cr-button>
  </div>

  <div class="panel-extensions">
    ${this.extensions.map((item, index) => html`
      <div class="panel-extension-row cr-row">
        <img class="panel-extension-icon" src="${item.iconUrl}"
            role="presentation">
        <div class="panel-extension-info text-elide">${item.name}</div>
        <div class="extension-buttons">
          <cr-button class="find-alternative-button"
              data-recommendations-url="${item.recommendationsUrl}"
              @click="${this.onFindAlternativeButtonClick_}"
              ?hidden="${!this.showExtensionFindAlternativeButton_(item)}"
              aria-label="${this.getFindAlternativeButtonLabelFor_(item.name)}">
            $i18n{mv2DeprecationPanelFindAlternativeButton}
            <cr-icon icon="cr:open-in-new" slot="suffix-icon"></cr-icon>
          </cr-button>
          <cr-icon-button id="removeButton" class="icon-delete-gray"
              data-id="${item.id}" @click="${this.onRemoveButtonClick_}"
              title="$i18n{remove}"
              aria-label="${this.getRemoveButtonLabelFor_(item.name)}"
              ?hidden="${!this.showExtensionRemoveButton_(item)}">
          </cr-icon-button>
          <cr-icon-button id="actionMenuButton" data-index="${index}"
              class="icon-more-vert header-aligned-button"
              @click="${this.onExtensionActionMenuClick_}"
              title="$i18n{moreOptions}"
              aria-label="${this.getActionMenuButtonLabelFor_(item.name)}"
              ?hidden="${!this.showActionMenu_(item)}">
          </cr-icon-button>
        </div>
      </div>`)}

    <cr-action-menu id="actionMenu">
      <button class="dropdown-item" id="findAlternativeAction"
          @click="${this.onFindAlternativeExtensionActionClick_}"
          ?hidden="${!this.showExtensionFindAlternativeAction_()}">
        Find alternatives
      </button>
      <button class="dropdown-item" id="keepAction"
          @click="${this.onKeepExtensionActionClick_}"
          ?hidden="${!this.showExtensionKeepAction_()}">
        $i18n{mv2DeprecationPanelKeepForNowButton}
      </button>
      <button class="dropdown-item" id="removeAction"
          @click="${this.onRemoveExtensionActionClicked_}"
          ?hidden="${!this.showExtensionRemoveAction_()}">
        $i18n{mv2DeprecationPanelRemoveExtensionButton}
      </button>
    </cr-action-menu>
  </div>
</div>
<!--_html_template_end_-->`;
  // clang-format on
}
