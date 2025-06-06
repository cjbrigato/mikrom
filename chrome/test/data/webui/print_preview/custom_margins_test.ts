// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import type {MarginsSetting, PrintPreviewMarginControlContainerElement, PrintPreviewMarginControlElement, PrintPreviewModelElement, Settings} from 'chrome://print/print_preview.js';
import {CustomMarginsOrientation, Margins, MarginsType, MeasurementSystem, MeasurementSystemUnitType, Size, State} from 'chrome://print/print_preview.js';
import {assertNotReached} from 'chrome://resources/js/assert.js';
import {assertEquals, assertFalse, assertTrue} from 'chrome://webui-test/chai_assert.js';
import {eventToPromise, microtasksFinished} from 'chrome://webui-test/test_util.js';

suite('CustomMarginsTest', function() {
  let container: PrintPreviewMarginControlContainerElement;
  let model: PrintPreviewModelElement;
  let sides: CustomMarginsOrientation[] = [];
  let measurementSystem: MeasurementSystem;
  const pixelsPerInch: number = 100;
  const pointsPerInch: number = 72.0;
  const defaultMarginPts: number = 36;  // 0.5 inch

  // Keys for the custom margins setting, in order.
  const keys: string[] =
      ['marginTop', 'marginRight', 'marginBottom', 'marginLeft'];

  setup(function() {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;
    measurementSystem =
        new MeasurementSystem(',', '.', MeasurementSystemUnitType.IMPERIAL);
    model = document.createElement('print-preview-model');
    document.body.appendChild(model);
    model.setSettingAvailableForTesting('mediaSize', true);

    sides = [
      CustomMarginsOrientation.TOP,
      CustomMarginsOrientation.RIGHT,
      CustomMarginsOrientation.BOTTOM,
      CustomMarginsOrientation.LEFT,
    ];

    container =
        document.createElement('print-preview-margin-control-container');
    container.previewLoaded = false;
    // 8.5 x 11, in points
    container.pageSize = new Size(612, 794);
    container.documentMargins = new Margins(
        defaultMarginPts, defaultMarginPts, defaultMarginPts, defaultMarginPts);
    container.state = State.NOT_READY;
  });

  function getControls(): PrintPreviewMarginControlElement[] {
    return Array.from(
        container.shadowRoot.querySelectorAll('print-preview-margin-control'));
  }

  /*
   * Completes setup of the test by setting the settings and adding the
   * container to the document body.
   * @return {!Promise} Promise that resolves when all controls have been
   *     added and initialization is complete.
   */
  function finishSetup() {
    // Wait for the control elements to be created before updating the state.
    container.measurementSystem = measurementSystem;
    document.body.appendChild(container);
    // 8.5 x 11, in pixels
    const controls = getControls();
    assertEquals(4, controls.length);

    container.state = State.READY;
    container.updateClippingMask(new Size(850, 1100));
    container.updateScaleTransform(pixelsPerInch / pointsPerInch);
    container.previewLoaded = true;
    return microtasksFinished();
  }

  /**
   * @return Promise that resolves when transitionend has fired
   *     for all of the controls.
   */
  function getAllTransitions(controls: PrintPreviewMarginControlElement[]):
      Promise<any[]> {
    return Promise.all(
        controls.map(control => eventToPromise('transitionend', control)));
  }

  /**
   * Simulates dragging the margin control.
   * @param control The control to move.
   * @param start The starting position for the control in pixels.
   * @param end The ending position for the control in pixels.
   */
  function dragControl(
      control: PrintPreviewMarginControlElement, start: number, end: number) {
    if (window.getComputedStyle(control).getPropertyValue('pointer-events') ===
        'none') {
      return;
    }

    let xStart = 0;
    let yStart = 0;
    let xEnd = 0;
    let yEnd = 0;
    switch (control.side) {
      case CustomMarginsOrientation.TOP:
        yStart = start;
        yEnd = end;
        break;
      case CustomMarginsOrientation.RIGHT:
        xStart = control.clipSize!.width - start;
        xEnd = control.clipSize!.width - end;
        break;
      case CustomMarginsOrientation.BOTTOM:
        yStart = control.clipSize!.height - start;
        yEnd = control.clipSize!.height - end;
        break;
      case CustomMarginsOrientation.LEFT:
        xStart = start;
        xEnd = end;
        break;
      default:
        assertNotReached();
    }
    // Simulate events in the same order they are fired by the browser.
    // Need to provide a valid |pointerId| for setPointerCapture() to not
    // throw an error.
    control.dispatchEvent(new PointerEvent(
        'pointerdown', {pointerId: 1, clientX: xStart, clientY: yStart}));
    control.dispatchEvent(new PointerEvent(
        'pointermove', {pointerId: 1, clientX: xEnd, clientY: yEnd}));
    control.dispatchEvent(new PointerEvent(
        'pointerup', {pointerId: 1, clientX: xEnd, clientY: yEnd}));
  }

  /**
   * Tests setting the margin control with its textbox.
   * @param control The control.
   * @param key The control's key in the custom margin setting.
   * @param currentValuePts The current margin value in points.
   * @param input The new textbox input for the margin.
   * @param invalid Whether the new value is invalid.
   * @param newValuePts the new margin value in pts. If not
   *     specified, computes the value assuming it is in bounds and assuming
   *     the default measurement system.
   * @return Promise that resolves when the test is complete.
   */
  function testControlTextbox(
      control: PrintPreviewMarginControlElement, key: string,
      currentValuePts: number, input: string, invalid: boolean,
      newValuePts?: number): Promise<void> {
    if (newValuePts === undefined) {
      newValuePts = invalid ? currentValuePts :
                              Math.round(parseFloat(input) * pointsPerInch);
    }
    assertEquals(
        currentValuePts, container.getSettingValue('customMargins')[key]);
    const controlTextbox = control.$.input;
    controlTextbox.value = input;
    controlTextbox.dispatchEvent(
        new CustomEvent('input', {composed: true, bubbles: true}));

    if (!invalid) {
      return eventToPromise('text-change', control).then(() => {
        assertEquals(
            newValuePts, container.getSettingValue('customMargins')[key]);
        assertFalse(control.invalid);
      });
    } else {
      return eventToPromise('input-change', control).then(() => {
        assertTrue(control.invalid);
      });
    }
  }

  /*
   * Initializes the settings custom margins to some test values, and returns
   * a map with the values.
   */
  function setupCustomMargins(): Map<CustomMarginsOrientation, number> {
    const orientationEnum = CustomMarginsOrientation;
    const marginValues = new Map([
      [orientationEnum.TOP, 72],
      [orientationEnum.RIGHT, 36],
      [orientationEnum.BOTTOM, 108],
      [orientationEnum.LEFT, 18],
    ]);
    model.setSetting('customMargins', {
      marginTop: marginValues.get(orientationEnum.TOP),
      marginRight: marginValues.get(orientationEnum.RIGHT),
      marginBottom: marginValues.get(orientationEnum.BOTTOM),
      marginLeft: marginValues.get(orientationEnum.LEFT),
    });
    return marginValues;
  }

  /*
   * Tests that the custom margins and margin value are cleared when the
   * setting |settingName| is set to have value |newValue|.
   * @param settingName The name of the setting to check.
   * @param newValue The value to set the setting to.
   * @return Promise that resolves when the check is complete.
   */
  function validateMarginsClearedForSetting(
      settingName: keyof Settings, newValue: any) {
    const marginValues = setupCustomMargins();
    return finishSetup().then(() => {
      // Simulate setting custom margins.
      model.setSetting('margins', MarginsType.CUSTOM);

      // Validate control positions are set based on the custom values.
      const controls = getControls();
      controls.forEach((control, index) => {
        const side = sides[index]!;
        assertEquals(side, control.side);
        assertEquals(marginValues.get(side), control.getPositionInPts());
      });

      // Simulate setting the media size.
      container.setSetting(settingName, newValue);
      container.previewLoaded = false;

      // Custom margins values should be cleared.
      assertEquals(
          '{}', JSON.stringify(container.getSettingValue('customMargins')));
      // The margins-settings element will also set the margins type to DEFAULT.
      model.setSetting('margins', MarginsType.DEFAULT);

      // When preview loads, custom margins should still be empty, since
      // custom margins are not selected. We do not want to set the sticky
      // values until the user has selected custom margins.
      container.previewLoaded = true;
      assertEquals(
          '{}', JSON.stringify(container.getSettingValue('customMargins')));
    });
  }

  // Test that controls correctly appear when custom margins are selected and
  // disappear when the preview is loading.
  test('ControlsCheck', function() {
    const getCustomMarginsValue = function(): MarginsSetting {
      return container.getSettingValue('customMargins') as MarginsSetting;
    };
    return finishSetup()
        .then(() => {
          const controls = getControls();
          assertEquals(4, controls.length);

          // Controls are not visible when margin type DEFAULT is selected.
          controls.forEach(control => {
            assertEquals('0', window.getComputedStyle(control).opacity);
          });

          const onTransitionEnd = getAllTransitions(controls);
          // Controls become visible when margin type CUSTOM is selected.
          model.setSetting('margins', MarginsType.CUSTOM);

          // Wait for the opacity transitions to finish.
          return onTransitionEnd;
        })
        .then(function() {
          // Verify margins are correctly set based on previous value.
          assertEquals(defaultMarginPts, getCustomMarginsValue().marginTop);
          assertEquals(defaultMarginPts, getCustomMarginsValue().marginLeft);
          assertEquals(defaultMarginPts, getCustomMarginsValue().marginRight);
          assertEquals(defaultMarginPts, getCustomMarginsValue().marginBottom);

          // Verify there is one control for each side and that controls are
          // visible and positioned correctly.
          const controls = getControls();
          controls.forEach((control, index) => {
            assertFalse(control.invisible);
            assertFalse(control.disabled);
            assertEquals('1', window.getComputedStyle(control).opacity);
            assertEquals(sides[index], control.side);
            assertEquals(defaultMarginPts, control.getPositionInPts());
          });

          const onTransitionEnd = getAllTransitions(controls);

          // Disappears when preview is loading or an error message is shown.
          // Check that all the controls also disappear.
          container.previewLoaded = false;
          // Wait for the opacity transitions to finish.
          return onTransitionEnd;
        })
        .then(function() {
          const controls = getControls();
          controls.forEach(control => {
            assertEquals('0', window.getComputedStyle(control).opacity);
            assertTrue(control.invisible);
            assertTrue(control.disabled);
          });
        });
  });

  // Tests that the margin controls can be correctly set from the sticky
  // settings.
  test('SetFromStickySettings', function() {
    return finishSetup().then(() => {
      const controls = getControls();

      // Simulate setting custom margins from sticky settings.
      model.setSetting('margins', MarginsType.CUSTOM);
      const marginValues = setupCustomMargins();

      // Validate control positions have been updated.
      controls.forEach((control, index) => {
        const side = sides[index]!;
        assertEquals(side, control.side);
        assertEquals(marginValues.get(side), control.getPositionInPts());
      });
    });
  });

  // Test that dragging margin controls updates the custom margins setting.
  test('DragControls', function() {
    /**
     * Tests that the control can be moved from its current position (assumed
     * to be the default margins) to newPositionInPts by dragging it.
     * @param {!PrintPreviewMarginControlElement} control The control to test.
     * @param {number} index The index of this control in the container's list
     *     of controls.
     * @param {number} newPositionInPts The new position in points.
     */
    const testControl = function(
        control: PrintPreviewMarginControlElement, index: number,
        newPositionInPts: number): Promise<void> {
      const oldValue =
          container.getSettingValue('customMargins') as {[k: string]: number};
      assertEquals(defaultMarginPts, oldValue[keys[index]!]);

      // Compute positions in pixels.
      const oldPositionInPixels =
          defaultMarginPts * pixelsPerInch / pointsPerInch;
      const newPositionInPixels =
          newPositionInPts * pixelsPerInch / pointsPerInch;

      const whenDragChanged = eventToPromise('margin-drag-changed', container);
      dragControl(control, oldPositionInPixels, newPositionInPixels);
      return whenDragChanged.then(function() {
        const newValue = container.getSettingValue('customMargins');
        assertEquals(newPositionInPts, newValue[keys[index]!]);
      });
    };

    return finishSetup().then(() => {
      const controls = getControls();
      model.setSetting('margins', MarginsType.CUSTOM);


      // Wait for an animation frame. The position of the controls is set in
      // an animation frame, and this needs to be initialized before dragging
      // the control so that the computation of the new location is performed
      // with the correct initial margin offset.
      // Set all controls to 108 = 1.5" in points.
      window.requestAnimationFrame(function() {
        return testControl(controls[0]!, 0, 108)
            .then(() => testControl(controls[1]!, 1, 108))
            .then(() => testControl(controls[2]!, 2, 108))
            .then(() => testControl(controls[3]!, 3, 108));
      });
    });
  });

  /**
   * @param currentValue Current margin value in pts
   * @param input String to set in margin textboxes
   * @param invalid Whether the string is invalid
   * @param newValuePts the new margin value in pts. If not
   *     specified, computes the value assuming it is in bounds and assuming
   *     the default measurement system.
   * @return Promise that resolves when all controls have been
   *     tested.
   */
  function testAllTextboxes(
      controls: PrintPreviewMarginControlElement[], currentValue: number,
      input: string, invalid: boolean, newValuePts?: number): Promise<void> {
    return testControlTextbox(
               controls[0]!, keys[0]!, currentValue, input, invalid,
               newValuePts)
        .then(
            () => testControlTextbox(
                controls[1]!, keys[1]!, currentValue, input, invalid,
                newValuePts))
        .then(
            () => testControlTextbox(
                controls[2]!, keys[2]!, currentValue, input, invalid,
                newValuePts))
        .then(
            () => testControlTextbox(
                controls[3]!, keys[3]!, currentValue, input, invalid,
                newValuePts));
  }

  // Test that setting the margin controls with their textbox inputs updates
  // the custom margins setting.
  test('SetControlsWithTextbox', function() {
    return finishSetup().then(async () => {
      const controls = getControls();
      // Set a shorter delay for testing so the test doesn't take too
      // long.
      controls.forEach(c => {
        c.getInput().setAttribute('data-timeout-delay', '1');
      });
      model.setSetting('margins', MarginsType.CUSTOM);
      await microtasksFinished();

      // Verify entering a new value updates the settings.
      // Then verify entering an invalid value invalidates the control
      // and does not update the settings.
      const value1 = '1.75';  // 1.75 inches
      const newMargin1 = Math.round(parseFloat(value1) * pointsPerInch);
      const value2 = '.6';
      const newMargin2 = Math.round(parseFloat(value2) * pointsPerInch);
      const value3 = '2';  // 2 inches
      const newMargin3 = Math.round(parseFloat(value3) * pointsPerInch);
      const maxTopMargin = container.pageSize.height - newMargin3 -
          72 /* MINIMUM_DISTANCE, see margin_control.js */;
      return testAllTextboxes(controls, defaultMarginPts, value1, false)
          .then(() => testAllTextboxes(controls, newMargin1, 'abc', true))
          .then(() => testAllTextboxes(controls, newMargin1, '1.2abc', true))
          .then(() => testAllTextboxes(controls, newMargin1, '1.   2', true))
          .then(() => testAllTextboxes(controls, newMargin1, '.', true))
          .then(() => testAllTextboxes(controls, newMargin1, value2, false))
          .then(() => testAllTextboxes(controls, newMargin2, value3, false))
          .then(
              () => testControlTextbox(
                  controls[0]!, keys[0]!, newMargin3, '100', false,
                  maxTopMargin))
          .then(
              () => testControlTextbox(
                  controls[0]!, keys[0]!, maxTopMargin, '1,000', false,
                  maxTopMargin));
    });
  });

  // Test that setting the margin controls with their textbox inputs updates
  // the custom margins setting, using a metric measurement system with a ','
  // as the decimal delimiter and '.' as the thousands delimiter. Regression
  // test for https://crbug.com/1005816.
  test('SetControlsWithTextboxMetric', function() {
    measurementSystem =
        new MeasurementSystem('.', ',', MeasurementSystemUnitType.METRIC);
    return finishSetup().then(async () => {
      const controls = getControls();
      // Set a shorter delay for testing so the test doesn't take too
      // long.
      controls.forEach(c => {
        c.getInput().setAttribute('data-timeout-delay', '1');
      });
      model.setSetting('margins', MarginsType.CUSTOM);
      await microtasksFinished();

      // Verify entering a new value updates the settings.
      // Then verify entering an invalid value invalidates the control
      // and does not update the settings.
      const pointsPerMM = pointsPerInch / 25.4;
      const newMargin1 = '50,0';
      const newMargin1Pts = Math.round(50 * pointsPerMM);
      const newMargin2 = ',9';
      const newMargin2Pts = Math.round(.9 * pointsPerMM);
      const newMargin3 = '60';
      const newMargin3Pts = Math.round(60 * pointsPerMM);
      const maxTopMargin = container.pageSize.height - newMargin3Pts -
          72 /* MINIMUM_DISTANCE, see margin_control.js */;
      return testAllTextboxes(
                 controls, defaultMarginPts, newMargin1, false, newMargin1Pts)
          .then(
              () => testAllTextboxes(
                  controls, newMargin1Pts, 'abc', true, newMargin1Pts))
          .then(
              () => testAllTextboxes(
                  controls, newMargin1Pts, '50,2abc', true, newMargin1Pts))
          .then(
              () => testAllTextboxes(
                  controls, newMargin1Pts, '10,   2', true, newMargin1Pts))
          .then(
              () => testAllTextboxes(
                  controls, newMargin1Pts, ',', true, newMargin1Pts))
          .then(
              () => testAllTextboxes(
                  controls, newMargin1Pts, newMargin2, false, newMargin2Pts))
          .then(
              () => testAllTextboxes(
                  controls, newMargin2Pts, newMargin3, false, newMargin3Pts))
          .then(
              () => testControlTextbox(
                  controls[0]!, keys[0]!, newMargin3Pts, '1.000.000', false,
                  maxTopMargin))
          .then(
              () => testControlTextbox(
                  controls[0]!, keys[0]!, maxTopMargin, '1.000', false,
                  maxTopMargin));
    });
  });

  // Test that if there is a custom margins sticky setting, it is restored
  // when margin setting changes.
  test('RestoreStickyMarginsAfterDefault', function() {
    const marginValues = setupCustomMargins();
    return finishSetup().then(() => {
      // Simulate setting custom margins.
      const controls = getControls();
      model.setSetting('margins', MarginsType.CUSTOM);

      // Validate control positions are set based on the custom values.
      controls.forEach((control, index) => {
        const side = sides[index]!;
        assertEquals(side, control.side);
        assertEquals(marginValues.get(side), control.getPositionInPts());
      });

      // Simulate setting minimum margins.
      model.setSetting('margins', MarginsType.MINIMUM);

      // Validate control positions still reflect the custom values.
      controls.forEach((control, index) => {
        const side = sides[index]!;
        assertEquals(side, control.side);
        assertEquals(marginValues.get(side), control.getPositionInPts());
      });
    });
  });

  // Test that if the media size changes, the custom margins are cleared.
  test('MediaSizeClearsCustomMargins', async function() {
    await validateMarginsClearedForSetting(
        'mediaSize', {height_microns: 200000, width_microns: 200000});
    // Simulate setting custom margins again.
    model.setSetting('margins', MarginsType.CUSTOM);
    await microtasksFinished();

    // Validate control positions are initialized based on the default
    // values.
    const controls = getControls();
    controls.forEach((control, index) => {
      const side = sides[index];
      assertEquals(side, control.side);
      assertEquals(defaultMarginPts, control.getPositionInPts());
    });
  });

  // Test that if the orientation changes, the custom margins are cleared.
  test('LayoutClearsCustomMargins', async function() {
    await validateMarginsClearedForSetting('layout', true);
    // Simulate setting custom margins again
    model.setSetting('margins', MarginsType.CUSTOM);
    await microtasksFinished();

    // Validate control positions are initialized based on the default
    // values.
    const controls = getControls();
    controls.forEach((control, index) => {
      const side = sides[index];
      assertEquals(side, control.side);
      assertEquals(defaultMarginPts, control.getPositionInPts());
    });
  });

  // Test that if the margins are not available, the custom margins setting is
  // not updated based on the document margins - i.e. PDFs do not change the
  // custom margins state.
  test('IgnoreDocumentMarginsFromPDF', function() {
    model.setSettingAvailableForTesting('margins', false);
    return finishSetup().then(() => {
      assertEquals(
          '{}', JSON.stringify(container.getSettingValue('customMargins')));
    });
  });

  // Test that if margins are not available but the user changes the media
  // size, the custom margins are cleared.
  test('MediaSizeClearsCustomMarginsPDF', function() {
    model.setSettingAvailableForTesting('margins', false);
    return validateMarginsClearedForSetting(
        'mediaSize', {height_microns: 200000, width_microns: 200000});
  });

  function whenAnimationFrameDone() {
    return new Promise(resolve => window.requestAnimationFrame(resolve));
  }

  // Test that if the user focuses a textbox that is not visible, the
  // text-focus event is fired with the correct values to scroll by.
  test('RequestScrollToOutOfBoundsTextbox', function() {
    return finishSetup()
        .then(() => {
          // Wait for the controls to be set up, which occurs in an
          // animation frame.
          return whenAnimationFrameDone();
        })
        .then(() => {
          const onTransitionEnd = getAllTransitions(getControls());

          // Controls become visible when margin type CUSTOM is selected.
          model.setSetting('margins', MarginsType.CUSTOM);
          return onTransitionEnd;
        })
        .then(async () => {
          // Zoom in by 2x, so that some margin controls will not be
          // visible.
          container.updateScaleTransform(pixelsPerInch * 2 / pointsPerInch);
          await microtasksFinished();
          return whenAnimationFrameDone();
        })
        .then(() => {
          const controls = getControls();
          assertEquals(4, controls.length);

          // Focus the bottom control, which is currently not visible since
          // the viewer is showing only the top left quarter of the page.
          const bottomControl = controls[2]!;
          const whenEventFired =
              eventToPromise('text-focus-position', container);
          bottomControl.$.input.focus();
          // Workaround for mac so that this does not need to be an
          // interactive test: manually fire the focus event from the
          // control.
          bottomControl.dispatchEvent(
              new CustomEvent('text-focus', {bubbles: true, composed: true}));
          return whenEventFired;
        })
        .then((args) => {
          // Shifts left by padding of 50px to ensure that the full textbox
          // is visible.
          assertEquals(50, args.detail.x);

          // Offset top will be 2097 = 200 px/in / 72 pts/in * (794pts -
          // 36ptx) - 9px radius of line
          // Height of the clip box is 200 px/in * 11in = 2200px
          // Shifts down by offsetTop = 2097 - height / 2 + padding =
          // 1047px. This will ensure that the textbox is in the visible
          // area.
          assertEquals(1047, args.detail.y);
        });
  });

  // Tests that the margin controls can be correctly set from the sticky
  // settings.
  test('ControlsDisabledOnError', async function() {
    await finishSetup();

    // Simulate setting custom margins.
    model.setSetting('margins', MarginsType.CUSTOM);
    await microtasksFinished();

    const controls = getControls();
    controls.forEach(control => assertFalse(control.disabled));

    container.state = State.ERROR;
    await microtasksFinished();
    // Validate controls are disabled.
    controls.forEach(control => assertTrue(control.disabled));

    container.state = State.READY;
    await microtasksFinished();
    controls.forEach(control => assertFalse(control.disabled));
  });
});
