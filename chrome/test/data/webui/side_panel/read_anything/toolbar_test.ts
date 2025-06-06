// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome-untrusted://read-anything-side-panel.top-chrome/read_anything.js';

import type {CrIconButtonElement} from '//resources/cr_elements/cr_icon_button/cr_icon_button.js';
import type {ReadAnythingToolbarElement} from 'chrome-untrusted://read-anything-side-panel.top-chrome/read_anything.js';
import {assertTrue} from 'chrome-untrusted://webui-test/chai_assert.js';
import {microtasksFinished} from 'chrome-untrusted://webui-test/test_util.js';

import {stubAnimationFrame} from './common.js';
import {FakeReadingMode} from './fake_reading_mode.js';

suite('Toolbar', () => {
  let toolbar: ReadAnythingToolbarElement;
  let shadowRoot: ShadowRoot;

  setup(() => {
    // Clearing the DOM should always be done first.
    document.body.innerHTML = window.trustedTypes!.emptyHTML;
    const readingMode = new FakeReadingMode();
    chrome.readingMode = readingMode as unknown as typeof chrome.readingMode;
  });

  async function createToolbar(): Promise<void> {
    toolbar = document.createElement('read-anything-toolbar');
    document.body.appendChild(toolbar);
    await microtasksFinished();
    assertTrue(!!toolbar.shadowRoot);
    shadowRoot = toolbar.shadowRoot;
  }

  suite('with read aloud', () => {
    setup(() => {
      chrome.readingMode.isReadAloudEnabled = true;
      return createToolbar();
    });

    test('has text settings menus', () => {
      stubAnimationFrame();

      const colorButton =
          shadowRoot.querySelector<CrIconButtonElement>('#color');
      assertTrue(!!colorButton);
      colorButton.click();
      assertTrue(toolbar.$.colorMenu.$.menu.$.lazyMenu.get().open);

      const lineSpacingButton =
          shadowRoot.querySelector<CrIconButtonElement>('#line-spacing');
      assertTrue(!!lineSpacingButton);
      lineSpacingButton.click();
      assertTrue(toolbar.$.lineSpacingMenu.$.menu.$.lazyMenu.get().open);

      const letterSpacingButton =
          shadowRoot.querySelector<CrIconButtonElement>('#letter-spacing');
      assertTrue(!!letterSpacingButton);
      letterSpacingButton.click();
      assertTrue(toolbar.$.letterSpacingMenu.$.menu.$.lazyMenu.get().open);
    });
  });

  suite('without read aloud', () => {
    setup(() => {
      chrome.readingMode.isReadAloudEnabled = false;
      return createToolbar();
    });

    test('has text settings menus', () => {
      stubAnimationFrame();

      const colorButton =
          shadowRoot.querySelector<CrIconButtonElement>('#color');
      assertTrue(!!colorButton);
      colorButton.click();
      assertTrue(toolbar.$.colorMenu.$.menu.$.lazyMenu.get().open);

      const lineSpacingButton =
          shadowRoot.querySelector<CrIconButtonElement>('#line-spacing');
      assertTrue(!!lineSpacingButton);
      lineSpacingButton.click();
      assertTrue(toolbar.$.lineSpacingMenu.$.menu.$.lazyMenu.get().open);

      const letterSpacingButton =
          shadowRoot.querySelector<CrIconButtonElement>('#letter-spacing');
      assertTrue(!!letterSpacingButton);
      letterSpacingButton.click();
      assertTrue(toolbar.$.letterSpacingMenu.$.menu.$.lazyMenu.get().open);
    });
  });
});
