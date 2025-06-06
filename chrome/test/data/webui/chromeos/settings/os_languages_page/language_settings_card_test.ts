// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://os-settings/lazy_load.js';

import type {LanguageSettingsCardElement} from 'chrome://os-settings/lazy_load.js';
import type {Route} from 'chrome://os-settings/os_settings.js';
import {Router, routes} from 'chrome://os-settings/os_settings.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {flush} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';
import {assertEquals, assertTrue} from 'chrome://webui-test/chai_assert.js';
import {waitAfterNextRender} from 'chrome://webui-test/polymer_test_util.js';
import {eventToPromise, isVisible} from 'chrome://webui-test/test_util.js';

suite('<language-settings-card>', () => {
  const defaultRoute = routes.SYSTEM_PREFERENCES;
  let languageSettingsCard: LanguageSettingsCardElement;

  function createLanguagesCard(): void {
    languageSettingsCard = document.createElement('language-settings-card');
    document.body.appendChild(languageSettingsCard);
    flush();
  }

  async function assertSubpageTriggerFocused(
      triggerSelector: string, route: Route): Promise<void> {
    const subpageTrigger =
        languageSettingsCard.shadowRoot!.querySelector<HTMLElement>(
            triggerSelector);
    assertTrue(!!subpageTrigger);

    // Subpage trigger navigates to subpage for route
    subpageTrigger.click();
    assertEquals(route, Router.getInstance().currentRoute);

    // Navigate back
    const popStateEventPromise = eventToPromise('popstate', window);
    Router.getInstance().navigateToPreviousRoute();
    await popStateEventPromise;
    await waitAfterNextRender(languageSettingsCard);

    assertEquals(
        subpageTrigger, languageSettingsCard.shadowRoot!.activeElement,
        `${triggerSelector} should be focused.`);
  }

  setup(() => {
    loadTimeData.overrideValues({allowEmojiSuggestion: true});
  });

  teardown(() => {
    languageSettingsCard.remove();
    Router.getInstance().resetRouteForTesting();
  });

  test('Languages row is visible', () => {
    createLanguagesCard();
    const languagesRow =
        languageSettingsCard.shadowRoot!.querySelector<HTMLElement>(
            '#languagesRow');
    assertTrue(isVisible(languagesRow));
  });

  test('Languages row is focused when returning from subpage', async () => {
    Router.getInstance().navigateTo(defaultRoute);
    createLanguagesCard();
    await assertSubpageTriggerFocused(
        '#languagesRow', routes.OS_LANGUAGES_LANGUAGES);
  });
});
