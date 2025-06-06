// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import 'chrome-untrusted://read-anything-side-panel.top-chrome/read_anything.js';

import {BrowserProxy, SpeechBrowserProxyImpl, ToolbarEvent, VoiceLanguageController} from 'chrome-untrusted://read-anything-side-panel.top-chrome/read_anything.js';
import type {AppElement} from 'chrome-untrusted://read-anything-side-panel.top-chrome/read_anything.js';
import {assertEquals, assertFalse, assertTrue} from 'chrome-untrusted://webui-test/chai_assert.js';
import {microtasksFinished} from 'chrome-untrusted://webui-test/test_util.js';

import {emitEvent, mockMetrics, setupBasicSpeech} from './common.js';
import {FakeReadingMode} from './fake_reading_mode.js';
import {FakeTreeBuilder} from './fake_tree_builder.js';
import {TestColorUpdaterBrowserProxy} from './test_color_updater_browser_proxy.js';
import type {TestMetricsBrowserProxy} from './test_metrics_browser_proxy.js';
import {TestSpeechBrowserProxy} from './test_speech_browser_proxy.js';

suite('UpdateContent', () => {
  let app: AppElement;
  let readingMode: FakeReadingMode;
  let metrics: TestMetricsBrowserProxy;

  const textNodeIds = [3, 5, 7, 9];
  const texts = [
    'If there\'s a prize for rotten judgment',
    'I guess I\'ve already won that',
    'No one is worth the aggravation',
    'That\'s ancient history, been there, done that!',
  ];

  setup(() => {
    // Clearing the DOM should always be done first.
    document.body.innerHTML = window.trustedTypes!.emptyHTML;
    BrowserProxy.setInstance(new TestColorUpdaterBrowserProxy());
    readingMode = new FakeReadingMode();
    chrome.readingMode = readingMode as unknown as typeof chrome.readingMode;
    const speech = new TestSpeechBrowserProxy();
    SpeechBrowserProxyImpl.setInstance(speech);
    VoiceLanguageController.setInstance(new VoiceLanguageController());
    metrics = mockMetrics();

    // Don't use await createApp() when using a FakeTree, as it seems to cause
    // flakiness.
    app = document.createElement('read-anything-app');
    document.body.appendChild(app);
    new FakeTreeBuilder()
        .root(1)
        .addTag(2, /* parentId= */ 1, 'p')
        .addText(textNodeIds[0]!, /* parentId= */ 2, texts[0]!)
        .addTag(4, /* parentId= */ 1, 'p')
        .addText(textNodeIds[1]!, /* parentId= */ 4, texts[1]!)
        .addTag(6, /* parentId= */ 1, 'p')
        .addText(textNodeIds[2]!, /* parentId= */ 6, texts[2]!)
        .addTag(8, /* parentId= */ 1, 'p')
        .addText(textNodeIds[3]!, /* parentId= */ 8, texts[3]!)
        .build(readingMode);

    setupBasicSpeech(speech);
  });

  test('playable if done with distillation', async () => {
    chrome.readingMode.requiresDistillation = false;
    app.updateContent();
    await microtasksFinished();

    assertTrue(app.$.toolbar.isReadAloudPlayable);
  });

  test('not playable if still requires distillation', async () => {
    chrome.readingMode.requiresDistillation = true;
    app.updateContent();
    await microtasksFinished();

    assertFalse(app.$.toolbar.isReadAloudPlayable);
  });

  test('logs speech stop if called while speech active', async () => {
    emitEvent(app, ToolbarEvent.PLAY_PAUSE);
    app.updateContent();
    await microtasksFinished();

    assertEquals(
        chrome.readingMode.unexpectedUpdateContentStopSource,
        await metrics.whenCalled('recordSpeechStopSource'));
  });

  test('hides loading page', async () => {
    app.updateContent();
    await microtasksFinished();

    assertTrue(!!app.shadowRoot);
    const emptyState =
        app.shadowRoot.querySelector<HTMLElement>('#empty-state-container');
    assertTrue(!!emptyState);
    assertTrue(emptyState.hidden);
  });

  test(
      'container clears old content when it receives new content', async () => {
        const expected1 = 'Gotta keep one jump ahead of the breadline.';
        new FakeTreeBuilder()
            .root(1)
            .addText(2, /* parentId= */ 1, expected1)
            .build(readingMode);
        app.updateContent();
        await microtasksFinished();

        assertEquals(expected1, app.$.container.textContent);

        const expected2 = 'One swing ahead of the sword.';
        new FakeTreeBuilder()
            .root(1)
            .addText(2, /* parentId= */ 1, expected2)
            .build(readingMode);
        app.updateContent();
        await microtasksFinished();

        // There should be nothing from the first content here, we should only
        // have the new content.
        assertEquals(expected2, app.$.container.textContent);
      });
});
