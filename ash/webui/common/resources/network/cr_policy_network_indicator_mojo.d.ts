// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import type {LegacyElementMixin} from '//resources/polymer/v3_0/polymer/lib/legacy/legacy-element-mixin.js';

export interface CrPolicyNetworkIndicatorMojoElement extends
    HTMLElement, LegacyElementMixin {}

declare global {
  interface HTMLElementTagNameMap {
    'cr-policy-network-indicator-mojo': CrPolicyNetworkIndicatorMojoElement;
  }
}
