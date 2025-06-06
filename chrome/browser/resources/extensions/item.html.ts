// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {ItemElement} from './item.js';

export function getHtml(this: ItemElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
<!-- Invisible instead of hidden because VoiceOver refuses to read text of
  element that's hidden when referenced by an aria label.  Unfortunately,
  this text can be found by Ctrl + F because it isn't hidden. -->
<div id="a11yAssociation" aria-hidden="true">
  ${this.a11yAssociation(this.data.name)}
</div>
<div id="card" class="${this.computeClasses_()}">
  <div id="main">
    <div id="icon-wrapper">
      <img id="icon" src="${this.data.iconUrl}"
          aria-describedby="a11yAssociation" alt="">
      ${this.computeSourceIndicatorIcon_() ? html`
        <div id="source-indicator">
          <div class="source-icon-wrapper" role="img"
              aria-describedby="a11yAssociation"
              aria-label="${this.computeSourceIndicatorText_()}">
            <cr-icon .icon="${this.computeSourceIndicatorIcon_()}">
            </cr-icon>
          </div>
        </div>` : ''}
    </div>

    <!-- This needs to be separate from the source-indicator since it can't
     be contained inside of a position:relative parent element. -->
    ${this.computeSourceIndicatorIcon_() ? html`
      <cr-tooltip id="source-indicator-text" for="source-indicator"
          position="top" fit-to-visible-bounds aria-hidden="true">
        ${this.computeSourceIndicatorText_()}
      </cr-tooltip>` : ''}
    <div id="content">
      <!--Note: We wrap inspect-views in a div so that the outer div
          doesn't shrink (because it's not display: flex).-->
      <div>
        <div id="name-and-version" class="layout-horizontal-center">
          <div id="name" role="heading" aria-level="3"
              class="clippable-flex-text">
            ${this.data.name}
          </div>
          <span id="version" class="cr-secondary-text"
              ?hidden="${!this.inDevMode}">
            ${this.data.version}
          </span>
        </div>
      </div>
      <div id="description" class="cr-secondary-text multiline-clippable-text"
          ?hidden="${!this.showDescription_()}">
        ${this.data.description}
      </div>
      ${this.showSevereWarnings() ? html`
        <div id="warnings">
          <cr-icon class="message-icon" icon="cr:error-outline"></cr-icon>
          <span id="runtime-warnings" class="cr-secondary-text"
              aria-describedby="a11yAssociation"
              ?hidden="${!this.data.runtimeWarnings.length}">
            ${this.data.runtimeWarnings.map(item => html`${item}`)}
          </span>
          <span id="suspicious-warning" class="cr-secondary-text"
              aria-describedby="a11yAssociation"
              ?hidden="${!this.data.disableReasons.suspiciousInstall}">
            $i18n{itemSuspiciousInstall}
            <a target="_blank" href="$i18n{suspiciousInstallHelpUrl}"
                aria-label="$i18n{itemSuspiciousInstallLearnMore}">
              $i18n{learnMore}
            </a>
          </span>
          <span id="corrupted-warning" class="cr-secondary-text"
              aria-describedby="a11yAssociation"
              ?hidden="${!this.data.disableReasons.corruptInstall}">
            $i18n{itemCorruptInstall}
          </span>
          <span id="blocklisted-warning" class="cr-secondary-text"><!--
            -->${this.data.blocklistText}<!-- No whitespace; use :empty in css.
       --></span>
          <span id="unsupported-developer-extension-warning"
              class="cr-secondary-text"
              ?hidden="${!this.data.disableReasons.
            unsupportedDeveloperExtension}">
            $i18n{itemUnsupportedDeveloperMode}
          </span>
        </div>` : ''}
      ${this.showMv2DeprecationWarning_() ? html`
        <div id="warnings">
          <cr-icon class="message-icon" icon="cr:error-outline"></cr-icon>
          <span id="mv2-deprecation-warning" class="cr-secondary-text"
              aria-describedby="a11yAssociation">
            $i18n{mv2DeprecationUnsupportedExtensionOffText}
          </span>
        </div>` : ''}
      ${this.showAllowlistWarning_() ? html`
        <div id="allowlist-warning">
          <cr-icon class="message-icon"
              icon="extensions-icons:safebrowsing_warning">
          </cr-icon>
          <span class="cr-secondary-text" aria-describedby="a11yAssociation">
            $i18n{itemAllowlistWarning}
            <a href="$i18n{enhancedSafeBrowsingWarningHelpUrl}" target="_blank"
                aria-label="$i18n{itemAllowlistWarningLearnMoreLabel}">
              $i18n{learnMore}
            </a>
          </span>
        </div>` : ''}
      ${this.inDevMode ? html`
        <div id="extension-id" class="bounded-text cr-secondary-text">
          ${this.getIdElementText_()}
        </div>
        ${!this.computeInspectViewsHidden_() ? html`
          <!--Note: We wrap inspect-views in a div so that the outer div
              doesn't shrink (because it's not display: flex).-->
          <div>
            <div id="inspect-views" class="cr-secondary-text">
              <span aria-describedby="a11yAssociation">
                $i18n{itemInspectViews}
              </span>
              <a class="clippable-flex-text" is="action-link"
                  title="${this.computeFirstInspectTitle_()}"
                  @click="${this.onInspectClick_}">
                ${this.computeFirstInspectLabel_()}
              </a>
              <a is="action-link" ?hidden="${this.computeExtraViewsHidden_()}"
                  @click="${this.onExtraInspectClick_}">
                &nbsp;${this.computeExtraInspectLabel_()}
              </a>
            </div>
          </div>` : ''}` : ''}
    </div>
  </div>
  <div id="button-strip" class="layout-horizontal-center cr-secondary-text">
    <div class="layout-horizontal-center flex">
      <cr-button id="detailsButton" @click="${this.onDetailsClick_}"
          aria-describedby="a11yAssociation">
        $i18n{itemDetails}
      </cr-button>
      <cr-button id="removeButton" @click="${this.onRemoveClick_}"
          aria-describedby="a11yAssociation"
          ?hidden="${this.data.mustRemainInstalled}">
        $i18n{remove}
      </cr-button>
      ${this.shouldShowErrorsButton_() ? html`
        <cr-button id="errors-button" @click="${this.onErrorsClick_}"
            aria-describedby="a11yAssociation">
          $i18n{itemErrors}
        </cr-button>` : ''}
    </div>
    ${this.showAccountUploadButton_() ? html`
      <cr-icon-button id="account-upload-button" class="no-overlap"
          title="$i18n{itemUpload}" aria-label="$i18n{itemUpload}"
          iron-icon="extensions-icons:extension_cloud_upload"
          aria-describedby="a11yAssociation" @click="${this.onUploadClick_}">
      </cr-icon-button>` : ''}
    ${this.showDevReloadButton_() ? html`
      <cr-icon-button id="dev-reload-button" class="icon-refresh no-overlap"
          title="$i18n{itemReload}" aria-label="$i18n{itemReload}"
          aria-describedby="a11yAssociation" @click="${this.onReloadClick_}">
      </cr-icon-button>` : ''}
    ${this.showRepairButton_() ? html`
      <cr-button id="repair-button" class="action-button"
          aria-describedby="a11yAssociation" @click="${this.onRepairClick_}">
        $i18n{itemRepair}
      </cr-button>` : ''}
    ${this.showReloadButton_() ? html`
      <cr-button id="terminated-reload-button" @click="${this.onReloadClick_}"
          aria-describedby="a11yAssociation" class="action-button">
        $i18n{itemReload}
      </cr-button>` : ''}
    <cr-tooltip-icon id="parentDisabledPermissionsToolTip"
        ?hidden="${!this.data.disableReasons.parentDisabledPermissions}"
        tooltip-text="$i18n{parentDisabledPermissions}"
        icon-class="cr20:kite"
        icon-aria-label="$i18n{parentDisabledPermissions}">
    </cr-tooltip-icon>
    <cr-tooltip id="enable-toggle-tooltip" for="enableToggle" position="left"
        aria-hidden="true" animation-delay="0" fit-to-visible-bounds>
      ${this.getEnableToggleTooltipText_()}
    </cr-tooltip>
    <cr-toggle id="enableToggle"
        aria-label="${this.getEnableToggleAriaLabel_()}"
        aria-describedby="a11yAssociation enable-toggle-tooltip"
        ?checked="${this.isEnabled_()}" @change="${this.onEnableToggleChange_}"
        ?disabled="${!this.isEnableToggleEnabled_()}"
        ?hidden="${!this.showEnableToggle_()}">
    </cr-toggle>
  </div>
</div>
<!--_html_template_end_-->`;
  // clang-format on
}
