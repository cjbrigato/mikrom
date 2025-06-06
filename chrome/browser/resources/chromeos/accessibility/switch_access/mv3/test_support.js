// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Test support for tests driven by C++.
 */
(async function() {
  const testImports = TestImportManager.getImports();
  globalThis.BackButtonNode = testImports.BackButtonNode;
  globalThis.FocusRingManager = testImports.FocusRingManager;
  globalThis.Navigator = testImports.Navigator;
  globalThis.Mode = testImports.Mode;
  globalThis.SwitchAccess = testImports.SwitchAccess;
  await SwitchAccess.ready();

  const focusRingState = {
    'primary': {'role': '', 'name': ''},
    'preview': {'role': '', 'name': ''},
  };
  let expectedType = '';
  let expectedRole = '';
  let expectedName = '';
  let successCallback = null;
  const transcript = [];

  function checkFocusRingState() {
    if (expectedType !== '' &&
        focusRingState[expectedType].role === expectedRole &&
        focusRingState[expectedType].name === expectedName) {
      if (successCallback) {
        transcript.push(
            `Success type=${expectedType} ` +
            `role=${expectedRole} name=${expectedName}`);
        successCallback();
        successCallback = null;
      }
    }
  }

  function findAutomationNodeByName(name) {
    return Navigator.byItem.desktopNode.find({attributes: {name}});
  }

  globalThis.doDefault = function(name, callback) {
    transcript.push(`Performing default action on node with name=${name}`);
    const node = findAutomationNodeByName(name);
    node.doDefault();
    callback();
  };

  globalThis.pointScanClick = function(x, y, callback) {
    transcript.push(`Clicking with point scanning at location x=${x} y=${y}`);
    SwitchAccess.mode = Mode.POINT_SCAN;
    Navigator.byPoint.point_ = {x, y};
    Navigator.byPoint.performMouseAction(
        chrome.accessibilityPrivate.SwitchAccessMenuAction.LEFT_CLICK);
    callback();
  };

  globalThis.waitForFocusRing = function(type, role, name, callback) {
    transcript.push(`Waiting for type=${type} role=${role} name=${name}`);
    expectedType = type;
    expectedRole = role;
    expectedName = name;
    successCallback = callback;
    checkFocusRingState();
  };

  globalThis.waitForEventOnAutomationNode = function(
      eventType, name, callback) {
    transcript.push(`Waiting for eventType=${eventType} name=${name}`);
    const node = findAutomationNodeByName(name);
    const listener = () => {
      node.removeEventListener(eventType, listener);
      callback();
    };
    node.addEventListener(eventType, listener);
  };

  globalThis.waitForBackButtonInitialized = async function() {
    const check = () => {
      const node = BackButtonNode.automationNode_;
      return Boolean(node);
    };

    if (check()) {
      chrome.test.sendScriptResult('ok');
      return;
    }

    const id = setInterval(() => {
      if (check()) {
        clearInterval(id);
        chrome.test.sendScriptResult('ok');
      }
    }, 500);
  };

  FocusRingManager.setObserver((primary, preview) => {
    if (primary && primary instanceof BackButtonNode) {
      focusRingState['primary']['role'] = 'back';
      focusRingState['primary']['name'] = '';
    } else if (primary && primary.automationNode) {
      const node = primary.automationNode;
      focusRingState['primary']['role'] = node.role;
      focusRingState['primary']['name'] = node.name;
    } else {
      focusRingState['primary']['role'] = '';
      focusRingState['primary']['name'] = '';
    }
    if (preview && preview.automationNode) {
      const node = preview.automationNode;
      focusRingState['preview']['role'] = node.role;
      focusRingState['preview']['name'] = node.name;
    } else {
      focusRingState['preview']['role'] = '';
      focusRingState['preview']['name'] = '';
    }
    transcript.push(`Focus ring state: ${JSON.stringify(focusRingState)}`);
    checkFocusRingState();
  });

  chrome.test.sendScriptResult('ready');

  setInterval(() => {
    console.error(
        'Test still running. Transcript so far:\n' + transcript.join('\n'));
  }, 5000);
})();
