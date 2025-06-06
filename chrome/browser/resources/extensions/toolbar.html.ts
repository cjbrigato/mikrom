// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {ToolbarElement} from './toolbar.js';

export function getHtml(this: ToolbarElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
<cr-toolbar id="toolbar" page-name="$i18n{toolbarTitle}"
    search-prompt="$i18n{search}" clear-label="$i18n{clearSearch}" autofocus
    menu-label="$i18n{mainMenu}" ?narrow="${this.narrow}"
    @narrow-changed="${this.onNarrowChanged_}" narrow-threshold="1000"
    ?show-menu="${this.narrow}">
  <div class="more-actions">
    <span id="devModeLabel">$i18n{toolbarDevMode}</span>
    <cr-tooltip-icon ?hidden="${!this.shouldDisableDevMode_()}"
        tooltip-text="${this.getTooltipText_()}" icon-class="${this.getIcon_()}"
        icon-aria-label="${this.getTooltipText_()}">
    </cr-tooltip-icon>
    <cr-toggle id="devMode" @change="${this.onDevModeToggleChange_}"
        ?disabled="${this.shouldDisableDevMode_()}" ?checked="${this.inDevMode}"
        aria-labelledby="devModeLabel">
    </cr-toggle>
  </div>
  <if expr="is_android">
    <picture slot="product-logo">
      <source media="(prefers-color-scheme: dark)"
          srcset="//resources/images/chrome_logo_dark.svg">
      <img srcset="images/product_logo.png" role="presentation">
    </picture>
  </if>
</cr-toolbar>
${this.showPackDialog_ ? html`
  <extensions-pack-dialog .delegate="${this.delegate}"
      @close="${this.onPackDialogClose_}">
  </extensions-pack-dialog>` : ''}
<div id="devDrawer" ?expanded="${this.expanded_}">
  <div id="buttonStrip">
    <cr-button ?hidden="${!this.canLoadUnpacked_()}" id="loadUnpacked"
        @click="${this.onLoadUnpackedClick_}">
      $i18n{toolbarLoadUnpacked}
    </cr-button>
    <cr-button id="packExtensions" @click="${this.onPackClick_}">
      $i18n{toolbarPack}
    </cr-button>
    <cr-button id="updateNow" @click="${this.onUpdateNowClick_}"
        title="$i18n{toolbarUpdateNowTooltip}">
      $i18n{toolbarUpdateNow}
    </cr-button>
  </div>
</div>
<!--_html_template_end_-->`;
  // clang-format on
}
