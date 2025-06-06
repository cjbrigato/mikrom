// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import type {CrButtonElement, PrintPreviewButtonStripElement} from 'chrome://print/print_preview.js';
import {Destination, DestinationOrigin, State} from 'chrome://print/print_preview.js';
import {assertEquals, assertFalse, assertTrue} from 'chrome://webui-test/chai_assert.js';
import {eventToPromise, microtasksFinished} from 'chrome://webui-test/test_util.js';

suite('ButtonStripTest', function() {
  let buttonStrip: PrintPreviewButtonStripElement;

  setup(function() {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;
    buttonStrip = document.createElement('print-preview-button-strip');

    buttonStrip.destination = new Destination(
        'FooDevice', DestinationOrigin.EXTENSION, 'FooName',
        {extensionId: 'aaa111', extensionName: 'myPrinterExtension'});
    buttonStrip.state = State.READY;
    document.body.appendChild(buttonStrip);
  });

  // Tests that the correct message is shown for non-READY states, and that
  // the print button is disabled appropriately.
  test('ButtonStripChangesForState', async function() {
    const printButton = buttonStrip.shadowRoot.querySelector<CrButtonElement>(
        '.action-button')!;
    assertFalse(printButton.disabled);

    buttonStrip.state = State.NOT_READY;
    await microtasksFinished();
    assertTrue(printButton.disabled);

    buttonStrip.state = State.PRINTING;
    await microtasksFinished();
    assertTrue(printButton.disabled);

    buttonStrip.state = State.ERROR;
    await microtasksFinished();
    assertTrue(printButton.disabled);

    buttonStrip.state = State.FATAL_ERROR;
    await microtasksFinished();
    assertTrue(printButton.disabled);
  });

  // Tests that the buttons are in the correct order for different platforms.
  // See https://crbug.com/880562.
  test('ButtonOrder', function() {
    // Verify that there are only 2 buttons.
    assertEquals(
        2, buttonStrip.shadowRoot.querySelectorAll('cr-button').length);

    const firstButton =
        buttonStrip.shadowRoot.querySelector('cr-button:not(:last-child)');
    const lastButton =
        buttonStrip.shadowRoot.querySelector('cr-button:last-child');
    const printButton =
        buttonStrip.shadowRoot.querySelector('cr-button.action-button');
    const cancelButton =
        buttonStrip.shadowRoot.querySelector('cr-button.cancel-button');

    // <if expr="is_win">
    // On Windows, the print button is on the left.
    assertEquals(firstButton, printButton);
    assertEquals(lastButton, cancelButton);
    // </if>
    // <if expr="not is_win">
    assertEquals(firstButton, cancelButton);
    assertEquals(lastButton, printButton);
    // </if>
  });

  // Tests that the button strip fires print-requested and cancel-requested
  // events.
  test('ButtonStripFiresEvents', async function() {
    const printButton = buttonStrip.shadowRoot.querySelector<HTMLElement>(
        'cr-button.action-button')!;
    const cancelButton = buttonStrip.shadowRoot.querySelector<HTMLElement>(
        'cr-button.cancel-button')!;

    const whenPrintRequested = eventToPromise('print-requested', buttonStrip);
    printButton.click();
    await whenPrintRequested;
    const whenCancelRequested = eventToPromise('cancel-requested', buttonStrip);
    cancelButton.click();
    return whenCancelRequested;
  });
});
