// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import type {AutogeneratedThemeColorInfo, ProfileState} from './manage_profiles_browser_proxy.js';

export function createDummyProfileState(): ProfileState {
  return {
    profilePath: '',
    localProfileName: '',
    isSyncing: false,
    needsSignin: false,
    gaiaName: '',
    userName: '',
    avatarBadge: '',
    avatarIcon: '',
    profileCardButtonLabel: '',
    hasEnterpriseLabel: false,
  };
}

export function createDummyAutogeneratedThemeColorInfo():
    AutogeneratedThemeColorInfo {
  return {
    colorId: 0,
    color: 0,
    themeFrameColor: '',
    themeShapeColor: '',
    themeFrameTextColor: '',
    themeGenericAvatar: '',
  };
}
