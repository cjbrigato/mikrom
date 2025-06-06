// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Suites of tests for flags-app loaded with an experiment
 * reference tag in the URL, i.e., chrome://flags/#test-feature. All tests are
 * loaded with the same tag, i.e., test-feature.
 * 1) UrlWithSupportedFeatureTest suite expects test-feature is
 * highlighted under supported feature tab.
 * 2) UrlWithUnsupportedFeatureTest suite expects test-feature is highlighted
 * under the unsupported feature tab.
 */

import 'chrome://flags/app.js';

import type {FlagsAppElement} from 'chrome://flags/app.js';
import type {ExperimentElement} from 'chrome://flags/experiment.js';
import type {ExperimentalFeaturesData, Feature} from 'chrome://flags/flags_browser_proxy.js';
import {FlagsBrowserProxyImpl} from 'chrome://flags/flags_browser_proxy.js';
import {assertEquals, assertTrue} from 'chrome://webui-test/chai_assert.js';

import {TestFlagsBrowserProxy} from './test_flags_browser_proxy.js';

const referencedFlagName = 'test-feature';
const experimentalFeaturesData: ExperimentalFeaturesData = {
  'supportedFeatures': [],
  'unsupportedFeatures': [],
  'needsRestart': false,
  'showBetaChannelPromotion': false,
  'showDevChannelPromotion': false,
  // <if expr="is_chromeos">
  'showOwnerWarning': false,
  // </if>
};
const mockFeatures: Feature[] = [
  {
    'description': 'test feature 1',
    'internal_name': 'test-feature-1',
    'is_default': true,
    'name': 'feature for testing 1',
    'enabled': true,
    'supported_platforms': ['Windows'],
  },
  {
    'description': 'test feature',
    'internal_name': 'test-feature',
    'is_default': true,
    'name': 'feature for testing',
    'enabled': true,
    'supported_platforms': ['Windows'],
  },
  {
    'description': 'test feature 2',
    'internal_name': 'test-feature-2',
    'is_default': true,
    'name': 'feature for testing 2',
    'enabled': true,
    'supported_platforms': ['Windows'],
  },
];

let app: FlagsAppElement;
let browserProxy: TestFlagsBrowserProxy;

function createFlagsAppElement(
    featureType: 'supportedFeatures'|'unsupportedFeatures') {
  document.body.innerHTML = window.trustedTypes!.emptyHTML;
  browserProxy = new TestFlagsBrowserProxy();
  browserProxy.setFeatureData(Object.assign(
      {}, experimentalFeaturesData, {[featureType]: mockFeatures}));
  FlagsBrowserProxyImpl.setInstance(browserProxy);
  app = document.createElement('flags-app');
  document.body.appendChild(app);
  app.setAnnounceStatusDelayMsForTesting(0);
  app.setSearchDebounceDelayMsForTesting(0);
  return app.experimentalFeaturesReadyForTesting();
}

suite('UrlTest', function() {
  function assertHighlightedExperimentInTab(selectedTab: number) {
    // check tab is selected
    const crTabs = app.getRequiredElement('cr-tabs');
    assertEquals(selectedTab, crTabs.selected);

    const referencedExperiment =
        app.getRequiredElement<ExperimentElement>(window.location.hash);
    assertTrue(!!referencedExperiment);
    assertEquals(referencedFlagName, referencedExperiment.id);

    // check experiment is highlighted
    assertTrue(referencedExperiment.classList.contains('referenced'));
  }

  test(
      'check referenced experiment is highlighted for supported features',
      async function() {
        assertEquals('#test-feature', window.location.hash);
        await createFlagsAppElement('supportedFeatures');
        assertHighlightedExperimentInTab(0);
        assertEquals('#test-feature', window.location.hash);
      });

  test(
      'check referenced experiment is highlighted for unsupported features',
      async function() {
        assertEquals('#test-feature', window.location.hash);
        await createFlagsAppElement('unsupportedFeatures');
        assertHighlightedExperimentInTab(1);
        assertEquals('#test-feature', window.location.hash);
      });
});

suite('UrlWithInvalidReferencedFlagHashTest', function() {
  test('check invalid referenced flag hash is removed', async function() {
    window.location.hash = '#invalid-hash.';

    // Reload page.
    await createFlagsAppElement('supportedFeatures');

    assertEquals('', window.location.hash);
  });
});
