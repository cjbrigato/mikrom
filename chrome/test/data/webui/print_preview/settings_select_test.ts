// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://print/print_preview.js';

import type {PrintPreviewModelElement, PrintPreviewSettingsSelectElement, SelectOption} from 'chrome://print/print_preview.js';
import {assertEquals, assertFalse, assertTrue} from 'chrome://webui-test/chai_assert.js';
import {microtasksFinished} from 'chrome://webui-test/test_util.js';

import {getMediaSizeCapabilityWithCustomNames, selectOption} from './print_preview_test_utils.js';

suite('SettingsSelectTest', function() {
  let settingsSelect: PrintPreviewSettingsSelectElement;

  let model: PrintPreviewModelElement;

  setup(function() {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;
    model = document.createElement('print-preview-model');
    document.body.appendChild(model);

    settingsSelect = document.createElement('print-preview-settings-select');
    settingsSelect.disabled = false;
    document.body.appendChild(settingsSelect);
  });

  // Test that destinations are correctly displayed in the lists.
  test('custom media names', async function() {
    model.setSettingAvailableForTesting('mediaSize', true);

    // Set a capability with custom paper sizes.
    settingsSelect.settingName = 'mediaSize';
    settingsSelect.capability = getMediaSizeCapabilityWithCustomNames();
    await microtasksFinished();

    const customLocalizedMediaName =
        settingsSelect.capability.option[0]!.custom_display_name_localized![0]!
            .value;
    const customMediaName =
        settingsSelect.capability.option[1]!.custom_display_name;
    const select = settingsSelect.shadowRoot.querySelector('select')!;
    // Verify that the selected option and names are as expected.
    assertEquals(0, select.selectedIndex);
    assertEquals(2, select.options.length);
    assertEquals(
        customLocalizedMediaName, select.options[0]!.textContent!.trim());
    assertEquals(customMediaName, select.options[1]!.textContent!.trim());
  });

  test('set setting', async () => {
    settingsSelect.settingName = 'dpi';
    settingsSelect.capability = {
      option: [
        {name: 'lime', color: 'green', size: 3},
        {name: 'orange', color: 'orange', size: 5, is_default: true},
      ],
    } as {option: Array<SelectOption & {color: string, size: number}>};
    const option0 = JSON.stringify(settingsSelect.capability.option[0]!);
    const option1 = JSON.stringify(settingsSelect.capability.option[1]!);
    const select = settingsSelect.shadowRoot.querySelector('select')!;

    // Normally done for initialization by the model and parent section.
    model.setSetting(
        'dpi', settingsSelect.capability.option[1]!,
        /*noSticky=*/ true);
    settingsSelect.selectValue(option1);
    await microtasksFinished();

    // Verify that the selected option and names are as expected.
    assertEquals(2, select.options.length);
    assertEquals(1, select.selectedIndex);
    assertFalse(settingsSelect.getSetting('dpi').setFromUi);
    assertEquals('lime', select.options[0]!.textContent!.trim());
    assertEquals('orange', select.options[1]!.textContent!.trim());
    assertEquals(option0, select.options[0]!.value);
    assertEquals(option1, select.options[1]!.value);

    // Verify that selecting an new option in the dropdown sets the setting.
    await selectOption(settingsSelect, option0);
    assertEquals(
        option0, JSON.stringify(settingsSelect.getSettingValue('dpi')));
    assertTrue(settingsSelect.getSetting('dpi').setFromUi);
    assertEquals(option0, settingsSelect.selectedValue);
    assertEquals(0, select.selectedIndex);
    assertEquals(option0, select.value);

    // Verify that selecting from outside works. This should update the UI,
    // but does not update the setting. This is because selectValue() is
    // intended for use by client code to update the UI programmatically (e.g.
    // in response to setting changes), so we don't want to call setSetting()
    // and set setFromUi = true for this case.
    settingsSelect.selectValue(option1);
    await microtasksFinished();
    assertEquals(option1, settingsSelect.selectedValue);
    assertEquals(option1, select.value);
    assertEquals(1, select.selectedIndex);
    assertEquals(
        option0, JSON.stringify(settingsSelect.getSettingValue('dpi')));
  });
});
