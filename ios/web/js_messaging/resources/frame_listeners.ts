// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {gCrWeb, gCrWebLegacy} from '//ios/web/public/js_messaging/resources/gcrweb.js';
import {sendWebKitMessage} from '//ios/web/public/js_messaging/resources/utils.js';

window.addEventListener('unload', function() {
  sendWebKitMessage('FrameBecameUnavailable', gCrWeb.getFrameId());
});

/**
 * Listens for messages received by the parent frame to initialize messaging
 * state.
 */
window.addEventListener('message', function(message: MessageEvent) {
  const payload = message.data;
  if (!payload || typeof payload !== 'object') {
    return;
  }
  if (payload.hasOwnProperty('type') &&
      payload.type === 'org.chromium.registerForFrameMessaging') {
    gCrWebLegacy.message.getExistingFrames();
  }
});
