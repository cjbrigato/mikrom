// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {ExtensionsSidebarElement} from './sidebar.js';

export function getHtml(this: ExtensionsSidebarElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
<cr-menu-selector id="sectionMenu" selected-attribute="selected"
    attr-for-selected="data-path" .selected="${this.selectedPath_}">
  <!-- Values for "data-path" attribute must match the "Page" enum. -->
  <a role="menuitem" class="cr-nav-menu-item" id="sectionsExtensions" href="/"
      @click="${this.onLinkClick_}" data-path="items-list">
    <cr-icon icon="extensions-icons:my_extensions"></cr-icon>
    $i18n{sidebarExtensions}
    <cr-ripple></cr-ripple>
  </a>
  <a role="menuitem" class="cr-nav-menu-item" id="sectionsSitePermissions"
      ?hidden="${!this.enableEnhancedSiteControls}" href="/sitePermissions"
      @click="${this.onLinkClick_}" data-path="site-permissions">
    <cr-icon icon="extensions-icons:site_permissions"></cr-icon>
    $i18n{sitePermissions}
    <cr-ripple></cr-ripple>
  </a>
  <a role="menuitem" class="cr-nav-menu-item" id="sectionsShortcuts"
      href="/shortcuts" @click="${this.onLinkClick_}"
      data-path="keyboard-shortcuts">
    <cr-icon icon="extensions-icons:keyboard_shortcuts"></cr-icon>
      $i18n{keyboardShortcuts}
    <cr-ripple></cr-ripple>
  </a>
</cr-menu-selector>
<div class="separator" ?hidden="${!this.inDevMode}"></div>
      ${this.inDevMode ? html`
        <div class="cr-nav-menu-item" id="moreExtensions">
          <span id="promo-message-text" class="cr-secondary-text"
            .innerHTML="${this.computeDocsPromoText_()}">
          </span>
        </div>
        `: ''}
<div class="separator"></div>
<div class="cr-nav-menu-item" id="moreExtensions">
  <cr-icon id="web-store-icon" icon="extensions-icons:web_store">
  </cr-icon>
  <span id="discover-more-text" class="cr-secondary-text"
      @click="${this.onMoreExtensionsClick_}"
      .innerHTML="${this.computeDiscoverMoreText_()}">
  </span>
  <cr-ripple></cr-ripple>
</div>
<!--_html_template_end_-->`;
  // clang-format on
}
