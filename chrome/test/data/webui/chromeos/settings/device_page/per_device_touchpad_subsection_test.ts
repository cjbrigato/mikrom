// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://os-settings/lazy_load.js';

import type {SettingsPerDeviceTouchpadSubsectionElement} from 'chrome://os-settings/lazy_load.js';
import {DisableTouchpadMode} from 'chrome://os-settings/lazy_load.js';
import type {CrToggleElement, SettingsDropdownMenuElement, SettingsPrefsElement, SettingsSliderElement, SettingsToggleButtonElement} from 'chrome://os-settings/os_settings.js';
import {CrSettingsPrefs, FakeInputDeviceSettingsProvider, fakeMice, fakeTouchpads, Router, routes, setInputDeviceSettingsProviderForTesting, SimulateRightClickModifier} from 'chrome://os-settings/os_settings.js';
import {assert} from 'chrome://resources/js/assert.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {pressAndReleaseKeyOn} from 'chrome://resources/polymer/v3_0/iron-test-helpers/mock-interactions.js';
import {assertEquals, assertFalse, assertTrue} from 'chrome://webui-test/chai_assert.js';
import {flushTasks, waitAfterNextRender} from 'chrome://webui-test/polymer_test_util.js';
import {isVisible} from 'chrome://webui-test/test_util.js';

const TOUCHPAD_SPEED_SETTING_ID = 405;
const TOUCHPAD_SIMULATE_RIGHT_CLICK_ID = 446;

suite('<settings-per-device-touchpad-subsection>', () => {
  let subsection: SettingsPerDeviceTouchpadSubsectionElement;
  let provider: FakeInputDeviceSettingsProvider;
  let prefElement: SettingsPrefsElement;

  setup(async () => {
    loadTimeData.overrideValues({
      enableAltClickAndSixPackCustomization: true,
    });
    provider = new FakeInputDeviceSettingsProvider();
    provider.setFakeTouchpads(fakeTouchpads);
    setInputDeviceSettingsProviderForTesting(provider);
    prefElement = document.createElement('settings-prefs');
    document.body.appendChild(prefElement);
    await CrSettingsPrefs.initialized;
    subsection =
        document.createElement('settings-per-device-touchpad-subsection');
    assert(subsection);
    subsection.set('touchpad', {...fakeTouchpads[0]});
    subsection.prefs = prefElement.prefs;
    document.body.appendChild(subsection);
    await flushTasks();
  });

  teardown(() => {
    subsection.remove();
    prefElement.remove();
  });

  function simulateDropdownChange(modifier: SimulateRightClickModifier) {
    subsection.set('simulateRightClickPref.value', modifier);
    return flushTasks();
  }

  /**
   * Test that API are updated when touchpad settings change.
   */
  test('Update API when touchpad settings change', async () => {
    const enableTapToClickButton =
        subsection.shadowRoot!.querySelector<SettingsToggleButtonElement>(
            '#enableTapToClick');
    assert(enableTapToClickButton);
    enableTapToClickButton.click();
    await flushTasks();
    let updatedTouchpads = await provider.getConnectedTouchpadSettings();
    assertEquals(
        updatedTouchpads[0]!.settings.tapToClickEnabled,
        enableTapToClickButton.pref!.value);

    const enableTapDraggingButton =
        subsection.shadowRoot!.querySelector<SettingsToggleButtonElement>(
            '#enableTapDragging');
    assert(enableTapDraggingButton);
    enableTapDraggingButton.click();
    await flushTasks();
    updatedTouchpads = await provider.getConnectedTouchpadSettings();
    assertEquals(
        updatedTouchpads[0]!.settings.tapDraggingEnabled,
        enableTapDraggingButton.pref!.value);

    const touchpadAccelerationButton =
        subsection.shadowRoot!.querySelector<SettingsToggleButtonElement>(
            '#touchpadAcceleration');
    assert(touchpadAccelerationButton);
    touchpadAccelerationButton.click();
    await flushTasks();
    updatedTouchpads = await provider.getConnectedTouchpadSettings();
    assertEquals(
        updatedTouchpads[0]!.settings.accelerationEnabled,
        touchpadAccelerationButton.pref!.value);

    const touchpadSensitivitySlider =
        subsection.shadowRoot!.querySelector<SettingsSliderElement>(
            '#touchpadSensitivity');
    assert(touchpadSensitivitySlider);
    pressAndReleaseKeyOn(
        touchpadSensitivitySlider.shadowRoot!.querySelector('cr-slider')!, 39,
        [], 'ArrowRight');
    await flushTasks();
    updatedTouchpads = await provider.getConnectedTouchpadSettings();
    assertEquals(
        updatedTouchpads[0]!.settings.sensitivity,
        touchpadSensitivitySlider.pref.value);

    const touchpadHapticClickSensitivitySlider =
        subsection.shadowRoot!.querySelector<SettingsSliderElement>(
            '#touchpadHapticClickSensitivity');
    assert(touchpadHapticClickSensitivitySlider);
    pressAndReleaseKeyOn(
        touchpadHapticClickSensitivitySlider.shadowRoot!.querySelector(
            'cr-slider')!,
        39, [], 'ArrowRight');
    await flushTasks();
    updatedTouchpads = await provider.getConnectedTouchpadSettings();
    assertEquals(
        updatedTouchpads[0]!.settings.hapticSensitivity,
        touchpadHapticClickSensitivitySlider.pref.value);

    const touchpadHapticFeedbackToggleButton =
        subsection.shadowRoot!.querySelector<CrToggleElement>(
            '#touchpadHapticFeedbackToggle');
    touchpadHapticFeedbackToggleButton!.click();
    await flushTasks();
    updatedTouchpads = await provider.getConnectedTouchpadSettings();
    assertEquals(
        updatedTouchpads[0]!.settings.hapticEnabled,
        touchpadHapticFeedbackToggleButton!.checked);

    const touchpadReverseScrollToggleButton =
        subsection.shadowRoot!.querySelector<CrToggleElement>(
            '#enableReverseScrollingToggle');
    touchpadReverseScrollToggleButton!.click();
    await flushTasks();
    updatedTouchpads = await provider.getConnectedTouchpadSettings();
    assertEquals(
        updatedTouchpads[0]!.settings.reverseScrolling,
        touchpadReverseScrollToggleButton!.checked);
  });

  /**
   * Test that touchpad settings data are from the touchpad provider.
   */
  test('Verify touchpad settings data', async () => {
    let enableTapToClickButton =
        subsection.shadowRoot!.querySelector<SettingsToggleButtonElement>(
            '#enableTapToClick');
    assertEquals(
        fakeTouchpads[0]!.settings.tapToClickEnabled,
        enableTapToClickButton!.pref!.value);
    let enableTapDraggingButton =
        subsection.shadowRoot!.querySelector<SettingsToggleButtonElement>(
            '#enableTapDragging');
    assertEquals(
        fakeTouchpads[0]!.settings.tapDraggingEnabled,
        enableTapDraggingButton!.pref!.value);
    let touchpadAcceleration =
        subsection.shadowRoot!.querySelector<SettingsToggleButtonElement>(
            '#touchpadAcceleration');
    assertEquals(
        fakeTouchpads[0]!.settings.accelerationEnabled,
        touchpadAcceleration!.pref!.value);
    let touchpadSensitivitySlider =
        subsection.shadowRoot!.querySelector<SettingsSliderElement>(
            '#touchpadSensitivity');
    assertEquals(
        fakeTouchpads[0]!.settings.sensitivity,
        touchpadSensitivitySlider!.pref.value);
    let touchpadHapticClickSensitivitySlider =
        subsection.shadowRoot!.querySelector<SettingsSliderElement>(
            '#touchpadHapticClickSensitivity');
    assertTrue(isVisible(touchpadHapticClickSensitivitySlider));
    assertEquals(
        fakeTouchpads[0]!.settings.hapticSensitivity,
        touchpadHapticClickSensitivitySlider!.pref.value);
    let touchpadHapticFeedbackToggleButton =
        subsection.shadowRoot!.querySelector<CrToggleElement>(
            '#touchpadHapticFeedbackToggle');
    assertTrue(isVisible(touchpadHapticFeedbackToggleButton));
    assertEquals(
        fakeTouchpads[0]!.settings.hapticEnabled,
        touchpadHapticFeedbackToggleButton!.checked);
    assertEquals(
        fakeTouchpads[0]!.settings.reverseScrolling,
        subsection.get('reverseScrollValue'));

    subsection.set('touchpad', fakeTouchpads[1]);

    await flushTasks();
    enableTapToClickButton =
        subsection.shadowRoot!.querySelector('#enableTapToClick');
    assertEquals(
        fakeTouchpads[1]!.settings.tapToClickEnabled,
        enableTapToClickButton!.pref!.value);
    enableTapDraggingButton =
        subsection.shadowRoot!.querySelector('#enableTapDragging');
    assertEquals(
        fakeTouchpads[1]!.settings.tapDraggingEnabled,
        enableTapDraggingButton!.pref!.value);
    touchpadAcceleration =
        subsection.shadowRoot!.querySelector('#touchpadAcceleration');
    assertEquals(
        fakeTouchpads[1]!.settings.accelerationEnabled,
        touchpadAcceleration!.pref!.value);
    touchpadSensitivitySlider =
        subsection.shadowRoot!.querySelector('#touchpadSensitivity');
    assertEquals(
        fakeTouchpads[1]!.settings.sensitivity,
        touchpadSensitivitySlider!.pref.value);
    touchpadHapticClickSensitivitySlider =
        subsection.shadowRoot!.querySelector('#touchpadHapticClickSensitivity');
    assertFalse(isVisible(touchpadHapticClickSensitivitySlider));
    touchpadHapticFeedbackToggleButton =
        subsection.shadowRoot!.querySelector('#touchpadHapticFeedbackToggle');
    assertFalse(isVisible(touchpadHapticFeedbackToggleButton));
    assertEquals(
        fakeTouchpads[1]!.settings.reverseScrolling,
        subsection.get('reverseScrollValue'));
  });

  /**
   * Test haptic settings are correctly show or hidden based on the touchpad is
   * haptic or not.
   */
  test('Verify haptic settings visibility', async () => {
    // Change the isHaptic state to true.
    subsection.set('touchpad.isHaptic', true);
    await flushTasks();
    // Verify haptic click sensitivity slider is visible in the page.
    let hapticClickSensitivitySlider =
        subsection.shadowRoot!.querySelector<SettingsSliderElement>(
            '#touchpadHapticClickSensitivity');
    assertTrue(isVisible(hapticClickSensitivitySlider));

    // Verify haptic feedback toggle button is visible in the page.
    let hapticFeedbackToggleButton =
        subsection.shadowRoot!.querySelector('#touchpadHapticFeedbackToggle');
    assertTrue(isVisible(hapticFeedbackToggleButton));

    // Change the isHaptic state to false.
    subsection.set('touchpad.isHaptic', false);
    await flushTasks();
    // Verify haptic click sensitivity slider is not visible in the page.
    hapticClickSensitivitySlider =
        subsection.shadowRoot!.querySelector('#touchpadHapticClickSensitivity');
    assertFalse(isVisible(hapticClickSensitivitySlider));

    // Verify haptic feedback toggle button is not visible in the page.
    hapticFeedbackToggleButton =
        subsection.shadowRoot!.querySelector('#touchpadHapticFeedbackToggle');
    assertFalse(isVisible(hapticFeedbackToggleButton));
  });

  /**
   * Verify entering the page with search tags matched will auto focus the
   * searched element.
   */
  test('Deep linking mixin focus on the first searched element', async () => {
    const touchpadSensitivitySlider =
        subsection.shadowRoot!.querySelector<SettingsSliderElement>(
            '#touchpadSensitivity');
    subsection.set('touchpadIndex', 0);
    // Enter the page from auto repeat search tag.
    const url = new URLSearchParams(
        'search=touchpad+speed&settingId=' +
        encodeURIComponent(TOUCHPAD_SPEED_SETTING_ID));

    await Router.getInstance().navigateTo(
        routes.PER_DEVICE_TOUCHPAD,
        /* dynamicParams= */ url, /* removeSearch= */ true);

    assert(touchpadSensitivitySlider);
    await waitAfterNextRender(touchpadSensitivitySlider);
    assertEquals(
        touchpadSensitivitySlider, subsection.shadowRoot!.activeElement);
  });

  test('Simulate right click dropdown is deep-linkable', async () => {
    subsection.set('touchpadIndex', 0);

    const params = new URLSearchParams();
    params.append('settingId', TOUCHPAD_SIMULATE_RIGHT_CLICK_ID.toString());
    await Router.getInstance().navigateTo(routes.PER_DEVICE_TOUCHPAD, params);

    const simulateRightClickDropdown =
        subsection.shadowRoot!.querySelector<SettingsDropdownMenuElement>(
            '#simulateRightClickDropdown');
    assertTrue(!!simulateRightClickDropdown);

    await waitAfterNextRender(simulateRightClickDropdown);
    assertEquals(
        simulateRightClickDropdown, subsection.shadowRoot!.activeElement);
  });

  /**
   * Verify entering the page with search tags matched wll not auto focus the
   * searched element if it's not the first keyboard displayed.
   */
  test('Deep linkng mixin does not focus on second element', async () => {
    const touchpadSensitivitySlider =
        subsection.shadowRoot!.querySelector<SettingsSliderElement>(
            '#touchpadSensitivity');
    subsection.set('touchpadIndex', 1);
    // Enter the page from auto repeat search tag.
    const url = new URLSearchParams(
        'search=touchpad+speed&settingId=' +
        encodeURIComponent(TOUCHPAD_SPEED_SETTING_ID));

    await Router.getInstance().navigateTo(
        routes.PER_DEVICE_TOUCHPAD,
        /* dynamicParams= */ url, /* removeSearch= */ true);
    await flushTasks();

    assert(touchpadSensitivitySlider);
    assertEquals(null, subsection.shadowRoot!.activeElement);
  });

  test('Simulate right click dropdown', async () => {
    assertTrue(isVisible(
        subsection.shadowRoot!.querySelector('#simulateRightClickContainer')));
    const simulateRightClickDropdown =
        subsection.shadowRoot!.querySelector<SettingsDropdownMenuElement>(
            '#simulateRightClickDropdown');
    assertTrue(!!simulateRightClickDropdown);
    // Dropdown has the correct default value.
    assertEquals(
        Number(simulateRightClickDropdown.$.dropdownMenu.value),
        SimulateRightClickModifier.kNone);
    await simulateDropdownChange(SimulateRightClickModifier.kAlt);
    assertEquals(
        Number(simulateRightClickDropdown.$.dropdownMenu.value),
        SimulateRightClickModifier.kAlt);
  });

  test(
      'Show disabled touchpad header when disable touchpad mode' +
          ' is set to "Always"',
      async () => {
        // In disabled touchpad never mode, disable touchpad header should not
        // be visible, but the touchpad settings should be visible.
        assertEquals(
            subsection.getPref('settings.a11y.disable_trackpad_mode').value,
            DisableTouchpadMode.NEVER);
        assertFalse(isVisible(
            subsection.shadowRoot!.querySelector('#disabledTouchpadHeader')));
        assertTrue(isVisible(
            subsection.shadowRoot!.querySelector('#touchpadSettings')));

        subsection.setPrefValue(
            'settings.a11y.disable_trackpad_mode', DisableTouchpadMode.ALWAYS);
        await flushTasks();

        // In disabled touchpad always mode, disable touchpad header should be
        // visible, but the touchpad settings should not be visible.
        assertFalse(isVisible(
            subsection.shadowRoot!.querySelector('#touchpadSettings')));
        assertTrue(isVisible(
            subsection.shadowRoot!.querySelector('#disabledTouchpadHeader')));
      });

  test(
      'Show disabled touchpad header when disable touchpad mode' +
          ' is set to "on Mouse Connected", and mouse connected',
      async () => {
        // In disabled touchpad never mode, disable touchpad header should not
        // be visible, but the touchpad settings should be visible.
        assertEquals(
            subsection.getPref('settings.a11y.disable_trackpad_mode').value,
            DisableTouchpadMode.NEVER);
        assertFalse(isVisible(
            subsection.shadowRoot!.querySelector('#disabledTouchpadHeader')));
        assertTrue(isVisible(
            subsection.shadowRoot!.querySelector('#touchpadSettings')));

        // In disable touchpad on mouse connected mode, the visibility of
        // elements depends on whether or not a mouse is connected. If there is
        // no mouse connected, the disable touchpad header should not be visible
        // and the touchpad settings should be visible.
        subsection.setPrefValue(
            'settings.a11y.disable_trackpad_mode',
            DisableTouchpadMode.ON_MOUSE_CONNECTED);
        provider.setFakeMice([]);
        await flushTasks();

        assertFalse(isVisible(
            subsection.shadowRoot!.querySelector('#disabledTouchpadHeader')));
        assertTrue(isVisible(
            subsection.shadowRoot!.querySelector('#touchpadSettings')));

        // In disable touchpad on mouse connected mode, the visibility of
        // elements depends on whether or not a mouse is connected. If there is
        // a mouse connected, the disable touchpad header should be visible and
        // the touchpad settings should not be visible.
        provider.setFakeMice(fakeMice);

        await flushTasks();
        assertTrue(isVisible(
            subsection.shadowRoot!.querySelector('#disabledTouchpadHeader')));
        assertFalse(isVisible(
            subsection.shadowRoot!.querySelector('#touchpadSettings')));
      });
});
