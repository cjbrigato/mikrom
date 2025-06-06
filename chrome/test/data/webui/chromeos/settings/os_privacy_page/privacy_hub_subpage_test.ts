// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://os-settings/lazy_load.js';

import type {SettingsPrivacyHubSubpage} from 'chrome://os-settings/lazy_load.js';
import {MediaDevicesProxy, PrivacyHubBrowserProxyImpl} from 'chrome://os-settings/lazy_load.js';
import type {CrLinkRowElement, CrToggleElement, OsSettingsPrivacyPageElement, PaperTooltipElement, SettingsToggleButtonElement} from 'chrome://os-settings/os_settings.js';
import {GeolocationAccessLevel, MetricsConsentBrowserProxyImpl, PrivacyHubSensorSubpageUserAction, Router, routes, SecureDnsMode, settingMojom} from 'chrome://os-settings/os_settings.js';
import {assert} from 'chrome://resources/js/assert.js';
import {webUIListenerCallback} from 'chrome://resources/js/cr.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {getDeepActiveElement} from 'chrome://resources/js/util.js';
import {flush} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';
import {assertEquals, assertFalse, assertNotReached, assertTrue} from 'chrome://webui-test/chai_assert.js';
import {flushTasks, waitAfterNextRender} from 'chrome://webui-test/polymer_test_util.js';

import {FakeMediaDevices} from '../fake_media_devices.js';
import type {FakeMetricsPrivate} from '../fake_metrics_private.js';
import {clearBody} from '../utils.js';

import {createFakeMetricsPrivate} from './privacy_hub_app_permission_test_util.js';
import {DEVICE_METRICS_CONSENT_PREF_NAME, TestMetricsConsentBrowserProxy} from './test_metrics_consent_browser_proxy.js';
import {TestPrivacyHubBrowserProxy} from './test_privacy_hub_browser_proxy.js';

const USER_METRICS_CONSENT_PREF_NAME = 'metrics.user_consent';

const PRIVACY_HUB_PREFS = {
  'ash': {
    'user': {
      'camera_allowed': {
        value: true,
      },
      'microphone_allowed': {
        value: true,
      },
      'geolocation_access_level': {
        key: 'ash.user.geolocation_access_level',
        type: chrome.settingsPrivate.PrefType.NUMBER,
        value: GeolocationAccessLevel.ALLOWED,
      },
    },
  },
};

const PrivacyHubVersion = {
  V0: 'Only contains camera and microphone access control.',
  V0AndLocation:
      'Privacy Hub location access control along with the V0 features.',
};

function overriddenValues(privacyHubVersion: string) {
  switch (privacyHubVersion) {
    case PrivacyHubVersion.V0: {
      return {
        showPrivacyHubLocationControl: false,
        showSpeakOnMuteDetectionPage: true,
      };
    }
    case PrivacyHubVersion.V0AndLocation: {
      return {
        showPrivacyHubLocationControl: true,
        showSpeakOnMuteDetectionPage: true,
      };
    }
    default: {
      assertNotReached(`Unsupported Privacy Hub version: {privacyHubVersion}`);
    }
  }
}

function parametrizedPrivacyHubSubpageTestsuite(
    privacyHubVersion: string, enforceCameraLedFallback: boolean) {
  let privacyHubSubpage: SettingsPrivacyHubSubpage;
  let privacyHubBrowserProxy: TestPrivacyHubBrowserProxy;
  let mediaDevices: FakeMediaDevices;

  setup(async () => {
    loadTimeData.overrideValues(overriddenValues(privacyHubVersion));

    privacyHubBrowserProxy = new TestPrivacyHubBrowserProxy();
    if (enforceCameraLedFallback) {
      privacyHubBrowserProxy.cameraLEDFallbackState = true;
    }
    PrivacyHubBrowserProxyImpl.setInstanceForTesting(privacyHubBrowserProxy);

    mediaDevices = new FakeMediaDevices();
    MediaDevicesProxy.setMediaDevicesForTesting(mediaDevices);

    await createSubpage();
  });

  teardown(() => {
    Router.getInstance().resetRouteForTesting();
  });

  function createSubpage(): Promise<void> {
    clearBody();
    privacyHubSubpage = document.createElement('settings-privacy-hub-subpage');
    privacyHubSubpage.prefs = {...PRIVACY_HUB_PREFS};
    document.body.appendChild(privacyHubSubpage);
    return flushTasks();
  }

  test('Suggested content, pref disabled', () => {
    // The default state of the pref is disabled.
    const suggestedContent = privacyHubSubpage.shadowRoot!
                                 .querySelector<SettingsToggleButtonElement>(
                                     '#contentRecommendationsToggle');
    assert(suggestedContent);
    assertFalse(suggestedContent.checked);
  });

  test('Suggested content, pref enabled', () => {
    privacyHubSubpage.prefs = {
      'settings': {
        'suggested_content_enabled': {
          value: true,
        },
      },
      ...PRIVACY_HUB_PREFS,
    };

    flush();

    // The checkbox reflects the updated pref state.
    const suggestedContent = privacyHubSubpage.shadowRoot!
                                 .querySelector<SettingsToggleButtonElement>(
                                     '#contentRecommendationsToggle');
    assert(suggestedContent);
    assertTrue(suggestedContent.checked);
  });

  test('Deep link to Geolocation area on privacy hub', async () => {
    const params = new URLSearchParams();
    const settingId = settingMojom.Setting.kGeolocationOnOff;
    params.append('settingId', settingId.toString());
    Router.getInstance().navigateTo(routes.PRIVACY_HUB, params);

    flush();

    const linkRowElement =
        privacyHubSubpage.shadowRoot!.querySelector('#geolocationAreaLinkRow');
    if (privacyHubVersion === PrivacyHubVersion.V0) {
      assertEquals(null, linkRowElement);
    } else if (privacyHubVersion === PrivacyHubVersion.V0AndLocation) {
      assert(linkRowElement);
      const deepLinkElement =
          linkRowElement.shadowRoot!.querySelector('cr-icon-button');
      assert(deepLinkElement);
      await waitAfterNextRender(deepLinkElement);
      assertEquals(
          deepLinkElement, getDeepActiveElement(),
          `Geolocation link row should be focused for settingId=${settingId}`);
    }
  });

  test('Deep link to speak-on-mute toggle on privacy hub', async () => {
    const params = new URLSearchParams();
    const settingId = settingMojom.Setting.kSpeakOnMuteDetectionOnOff;
    params.append('settingId', `${settingId}`);
    Router.getInstance().navigateTo(routes.PRIVACY_HUB, params);

    flush();

    const deepLinkElement = privacyHubSubpage.shadowRoot!
                                .querySelector('#speakonmuteDetectionToggle')!
                                .shadowRoot!.querySelector('cr-toggle');
    assert(deepLinkElement);
    await waitAfterNextRender(deepLinkElement);
    assertEquals(
        deepLinkElement, getDeepActiveElement(),
        `Speak-on-mute detection toggle should be focused for settingId=${
            settingId}.`);
  });
}

suite('<settings-privacy-hub-subpage> AllBuilds', () => {
  suite(
      'Privacy Hub V0',
      () =>
          parametrizedPrivacyHubSubpageTestsuite(PrivacyHubVersion.V0, false));
  suite(
      'V0 using camera LED Fallback Mechanism',
      () => parametrizedPrivacyHubSubpageTestsuite(PrivacyHubVersion.V0, true));
  suite(
      'Location access control with V0 features.',
      () => parametrizedPrivacyHubSubpageTestsuite(
          PrivacyHubVersion.V0AndLocation, false));
});

suite('<settings-privacy-hub-subpage> AllBuilds app permissions', () => {
  let metrics: FakeMetricsPrivate;
  let privacyHubSubpage: SettingsPrivacyHubSubpage;
  let privacyHubBrowserProxy: TestPrivacyHubBrowserProxy;
  let mediaDevices: FakeMediaDevices;

  setup(async () => {
    metrics = createFakeMetricsPrivate();

    privacyHubBrowserProxy = new TestPrivacyHubBrowserProxy();
    PrivacyHubBrowserProxyImpl.setInstanceForTesting(privacyHubBrowserProxy);

    mediaDevices = new FakeMediaDevices();
    MediaDevicesProxy.setMediaDevicesForTesting(mediaDevices);

    await createSubpage();
  });

  teardown(() => {
    Router.getInstance().resetRouteForTesting();
  });

  async function createSubpage(): Promise<void> {
    clearBody();
    privacyHubSubpage = document.createElement('settings-privacy-hub-subpage');
    privacyHubSubpage.prefs = {...PRIVACY_HUB_PREFS};
    document.body.appendChild(privacyHubSubpage);
    await flushTasks();
  }

  function getCameraCrToggle(): CrToggleElement {
    const crToggle =
        privacyHubSubpage.shadowRoot!.querySelector<CrToggleElement>(
            '#cameraToggle');
    assertTrue(!!crToggle);
    return crToggle;
  }

  test('Navigate to the camera subpage', () => {
    assertEquals(
        0,
        metrics.countMetricValue(
            'ChromeOS.PrivacyHub.CameraSubpage.UserAction',
            PrivacyHubSensorSubpageUserAction.SUBPAGE_OPENED));

    const cameraSubpageLink =
        privacyHubSubpage.shadowRoot!.querySelector<CrLinkRowElement>(
            '#cameraSubpageLink');
    assertTrue(!!cameraSubpageLink);
    cameraSubpageLink.click();

    assertEquals(
        1,
        metrics.countMetricValue(
            'ChromeOS.PrivacyHub.CameraSubpage.UserAction',
            PrivacyHubSensorSubpageUserAction.SUBPAGE_OPENED));
    assertEquals(routes.PRIVACY_HUB_CAMERA, Router.getInstance().currentRoute);
  });

  test('Toggle camera access', async () => {
    mediaDevices.addDevice('videoinput', 'Fake Camera');
    await waitAfterNextRender(privacyHubSubpage);

    const cameraToggle = getCameraCrToggle();
    const cameraPref = privacyHubSubpage.prefs.ash.user.camera_allowed;

    // Pref and toggle should be in sync and not disabled.
    assertTrue(cameraToggle.checked);
    assertTrue(cameraPref.value);

    cameraToggle.click();
    assertFalse(cameraToggle.checked);
    assertFalse(cameraPref.value);

    cameraToggle.click();
    assertTrue(cameraToggle.checked);
    assertTrue(cameraPref.value);
  });

  function getMicrophoneCrToggle(): CrToggleElement {
    const crToggle =
        privacyHubSubpage.shadowRoot!.querySelector<CrToggleElement>(
            '#microphoneToggle');
    assertTrue(!!crToggle);
    return crToggle;
  }

  function getMicrophoneTooltip(): PaperTooltipElement {
    const tooltip =
        privacyHubSubpage.shadowRoot!.querySelector<PaperTooltipElement>(
            '#microphoneToggleTooltip');
    assertTrue(!!tooltip);
    return tooltip;
  }

  test('Microphone toggle disabled scenarios', async () => {
    privacyHubBrowserProxy.microphoneToggleIsEnabled = false;
    await privacyHubBrowserProxy.whenCalled(
        'getInitialMicrophoneHardwareToggleState');
    await waitAfterNextRender(privacyHubSubpage);

    // There is no MediaDevice connected initially. Microphone toggle should be
    // disabled as no microphone is connected.
    assertTrue(getMicrophoneCrToggle().disabled);
    assertFalse(getMicrophoneTooltip().hidden);
    assertEquals(
        privacyHubSubpage.i18n('privacyHubNoMicrophoneConnectedTooltipText'),
        getMicrophoneTooltip().textContent!.trim());

    // Add a microphone.
    mediaDevices.addDevice('audioinput', 'Fake Microphone');
    await waitAfterNextRender(privacyHubSubpage);

    // Microphone toggle should be enabled to click now as there is a microphone
    // connected and the hw toggle is inactive.
    assertFalse(getMicrophoneCrToggle().disabled);
    assertTrue(getMicrophoneTooltip().hidden);

    // Activate the hw toggle.
    webUIListenerCallback('microphone-hardware-toggle-changed', true);
    await waitAfterNextRender(privacyHubSubpage);

    // Microphone toggle should be disabled again due to the hw switch being
    // active.
    assertTrue(getMicrophoneCrToggle().disabled);
    // With the HW switch being active the tooltip should be visible.
    assertFalse(getMicrophoneTooltip().hidden);
    // Ensure that the tooltip has the intended content.
    assertEquals(
        privacyHubSubpage.i18n('microphoneHwToggleTooltip'),
        getMicrophoneTooltip().textContent!.trim());

    mediaDevices.popDevice();
  });

  function getCameraTooltip(): PaperTooltipElement {
    const tooltip =
        privacyHubSubpage.shadowRoot!.querySelector<PaperTooltipElement>(
            '#cameraToggleTooltip');
    assertTrue(!!tooltip);
    return tooltip;
  }

  test('Camera toggle tooltip displayed when no camera connected', async () => {
    assertTrue(getCameraCrToggle().disabled);
    assertFalse(getCameraTooltip().hidden);
    assertEquals(
        privacyHubSubpage.i18n('privacyHubNoCameraConnectedTooltipText'),
        getCameraTooltip().textContent!.trim());

    // Add a camera.
    mediaDevices.addDevice('videoinput', 'Fake Camera');
    await flushTasks();

    assertFalse(getCameraCrToggle().disabled);
    assertTrue(getCameraTooltip().hidden);

    mediaDevices.popDevice();
  });

  test('Toggle microphone access', async () => {
    mediaDevices.addDevice('audioinput', 'Fake Mic');
    await waitAfterNextRender(privacyHubSubpage);

    const microphoneToggle = getMicrophoneCrToggle();
    const microphonePref = privacyHubSubpage.prefs.ash.user.microphone_allowed;

    // Pref and toggle should be in sync and not disabled.
    assertTrue(microphoneToggle.checked);
    assertTrue(microphonePref.value);

    microphoneToggle.click();
    assertFalse(microphoneToggle.checked);
    assertFalse(microphonePref.value);


    microphoneToggle.click();
    assertTrue(microphoneToggle.checked);
    assertTrue(microphonePref.value);
  });

  test('Navigate to the microphone subpage', () => {
    assertEquals(
        0,
        metrics.countMetricValue(
            'ChromeOS.PrivacyHub.MicrophoneSubpage.UserAction',
            PrivacyHubSensorSubpageUserAction.SUBPAGE_OPENED));

    const microphoneSubpageLink =
        privacyHubSubpage.shadowRoot!.querySelector<CrLinkRowElement>(
            '#microphoneSubpageLink');
    assertTrue(!!microphoneSubpageLink);
    microphoneSubpageLink.click();

    assertEquals(
        1,
        metrics.countMetricValue(
            'ChromeOS.PrivacyHub.MicrophoneSubpage.UserAction',
            PrivacyHubSensorSubpageUserAction.SUBPAGE_OPENED));
    assertEquals(
        routes.PRIVACY_HUB_MICROPHONE, Router.getInstance().currentRoute);
  });

  function getMicrophoneRowSubtext(): string {
    return privacyHubSubpage.shadowRoot!
        .querySelector<CrLinkRowElement>('#microphoneSubpageLink')!.shadowRoot!
        .querySelector<HTMLElement>('#subLabel')!.innerText.trim();
  }

  function getCameraRowSubtext(): string {
    return privacyHubSubpage.shadowRoot!
        .querySelector<CrLinkRowElement>('#cameraSubpageLink')!.shadowRoot!
        .querySelector<HTMLElement>('#subLabel')!.innerText.trim();
  }

  test('Microphone row subtext', async () => {
    mediaDevices.addDevice('audioinput', 'Fake Mic');
    await flushTasks();

    assertEquals(
        privacyHubSubpage.i18n('privacyHubPageMicrophoneRowSubtext'),
        getMicrophoneRowSubtext());

    getMicrophoneCrToggle().click();
    flush();

    assertEquals(
        privacyHubSubpage.i18n('privacyHubMicrophoneAccessBlockedText'),
        getMicrophoneRowSubtext());

    getMicrophoneCrToggle().click();
    flush();

    assertEquals(
        privacyHubSubpage.i18n('privacyHubPageMicrophoneRowSubtext'),
        getMicrophoneRowSubtext());
  });

  function getMicrophoneToggleAriaLabel(): string {
    return getMicrophoneCrToggle().getAttribute('aria-label')!.trim();
  }

  function getMicrophoneToggleAriaDescription(): string {
    return getMicrophoneCrToggle().getAttribute('aria-description')!.trim();
  }

  test('Microphone toggle aria label and description', async () => {
    mediaDevices.addDevice('audioinput', 'Fake Mic');
    await flushTasks();

    assertEquals(
        privacyHubSubpage.i18n('microphoneToggleTitle'),
        getMicrophoneToggleAriaLabel());
    assertEquals(
        getMicrophoneRowSubtext(), getMicrophoneToggleAriaDescription());

    getMicrophoneCrToggle().click();
    flush();

    assertEquals(
        privacyHubSubpage.i18n('microphoneToggleTitle'),
        getMicrophoneToggleAriaLabel());
    assertEquals(
        getMicrophoneRowSubtext(), getMicrophoneToggleAriaDescription());
  });

  test('Camera row subtext', async () => {
    mediaDevices.addDevice('videoinput', 'Fake Camera');
    await flushTasks();

    assertEquals(
        privacyHubSubpage.i18n('privacyHubPageCameraRowSubtext'),
        getCameraRowSubtext());

    getCameraCrToggle().click();
    flush();

    assertEquals(
        privacyHubSubpage.i18n('privacyHubCameraAccessBlockedText'),
        getCameraRowSubtext());

    getCameraCrToggle().click();
    flush();

    assertEquals(
        privacyHubSubpage.i18n('privacyHubPageCameraRowSubtext'),
        getCameraRowSubtext());
  });

  test('Camera row fallback subtext', async () => {
    privacyHubBrowserProxy.cameraLEDFallbackState = true;
    PrivacyHubBrowserProxyImpl.setInstanceForTesting(privacyHubBrowserProxy);

    await createSubpage();

    assertEquals(
        privacyHubSubpage.i18n('privacyHubPageCameraRowFallbackSubtext'),
        getCameraRowSubtext());
  });

  function getCameraToggleAriaLabel(): string {
    return getCameraCrToggle().getAttribute('aria-label')!.trim();
  }

  function getCameraToggleAriaDescription(): string {
    return getCameraCrToggle().getAttribute('aria-description')!.trim();
  }

  test('Camera toggle aria label and description', async () => {
    mediaDevices.addDevice('videoinput', 'Fake Camera');
    await flushTasks();

    assertEquals(
        privacyHubSubpage.i18n('cameraToggleTitle'),
        getCameraToggleAriaLabel());
    assertEquals(getCameraRowSubtext(), getCameraToggleAriaDescription());

    getCameraCrToggle().click();
    flush();

    assertEquals(
        privacyHubSubpage.i18n('cameraToggleTitle'),
        getCameraToggleAriaLabel());
    assertEquals(getCameraRowSubtext(), getCameraToggleAriaDescription());
  });

  function setGeolocationAccessLevel(accessLevel: GeolocationAccessLevel) {
    privacyHubSubpage.set(
        'prefs.ash.user.geolocation_access_level.value', accessLevel);
  }

  test('Camera toggle initially force disabled', async () => {
    privacyHubBrowserProxy.cameraSwitchIsForceDisabled = true;

    await createSubpage();

    // There is no MediaDevice connected initially. Camera toggle should be
    // disabled as no camera is connected.
    assertTrue(getCameraCrToggle().disabled);

    // Add a camera.
    mediaDevices.addDevice('videoinput', 'Fake Camera');
    await waitAfterNextRender(privacyHubSubpage);

    // Camera toggle should remain disabled.
    assertTrue(getCameraCrToggle().disabled);

    mediaDevices.popDevice();
  });

  test('Change force-disable-camera-switch', async () => {
    // Add a camera so the camera toggle is enabled.
    mediaDevices.addDevice('videoinput', 'Fake Camera');
    await waitAfterNextRender(privacyHubSubpage);
    assertFalse(getCameraCrToggle().disabled);

    // Force disable camera toggle.
    webUIListenerCallback('force-disable-camera-switch', true);
    await waitAfterNextRender(privacyHubSubpage);
    assertTrue(getCameraCrToggle().disabled);

    // Stop Force disabling camera toggle.
    webUIListenerCallback('force-disable-camera-switch', false);
    await waitAfterNextRender(privacyHubSubpage);
    assertFalse(getCameraCrToggle().disabled);

    // Remove the last camera should again disable the camera toggle.
    mediaDevices.popDevice();
    await waitAfterNextRender(privacyHubSubpage);
    assertTrue(getCameraCrToggle().disabled);
  });

  function getGeolocationSubtext(): string {
    return privacyHubSubpage.shadowRoot!
        .querySelector<CrLinkRowElement>('#geolocationAreaLinkRow')!.shadowRoot!
        .querySelector<HTMLElement>('#subLabel')!.innerText;
  }

  test('Geolocation row subtext', async () => {
    // Location should be allowed by default
    assertEquals(
        privacyHubSubpage.prefs.ash.user.geolocation_access_level.value,
        GeolocationAccessLevel.ALLOWED);
    assertEquals(
        privacyHubSubpage.i18n('geolocationAreaAllowedSubtext'),
        getGeolocationSubtext());

    // Set Location setting to system only
    setGeolocationAccessLevel(GeolocationAccessLevel.ONLY_ALLOWED_FOR_SYSTEM);
    await waitAfterNextRender(privacyHubSubpage);
    assertEquals(
        privacyHubSubpage.prefs.ash.user.geolocation_access_level.value,
        GeolocationAccessLevel.ONLY_ALLOWED_FOR_SYSTEM);
    assertEquals(
        privacyHubSubpage.i18n('geolocationAreaOnlyAllowedForSystemSubtext'),
        getGeolocationSubtext());

    // Disable location
    setGeolocationAccessLevel(GeolocationAccessLevel.DISALLOWED);
    await waitAfterNextRender(privacyHubSubpage);
    assertEquals(
        privacyHubSubpage.prefs.ash.user.geolocation_access_level.value,
        GeolocationAccessLevel.DISALLOWED);
    assertEquals(
        privacyHubSubpage.i18n('geolocationAreaDisallowedSubtext'),
        getGeolocationSubtext());
  });
});


function testsuiteForMetricsConsentToggle() {
  let settingsPage: SettingsPrivacyHubSubpage|OsSettingsPrivacyPageElement;

  // Which settings page to run the tests on.
  const pageId = 'settings-privacy-hub-subpage';

  const prefs_ = {
    'cros': {
      'device': {
        'peripheral_data_access_enabled': {
          value: true,
        },
      },
      'metrics': {
        'reportingEnabled': {
          value: true,
        },
      },
    },
    'metrics': {
      'user_consent': {
        value: false,
      },
    },
    'dns_over_https':
        {'mode': {value: SecureDnsMode.AUTOMATIC}, 'templates': {value: ''}},
    ...PRIVACY_HUB_PREFS,
  };

  let metricsConsentBrowserProxy: TestMetricsConsentBrowserProxy;

  setup(() => {
    metricsConsentBrowserProxy = new TestMetricsConsentBrowserProxy();
    MetricsConsentBrowserProxyImpl.setInstanceForTesting(
        metricsConsentBrowserProxy);

    settingsPage = document.createElement(pageId);
  });

  async function setUpPage(prefName: string, isConfigurable: boolean) {
    metricsConsentBrowserProxy.setMetricsConsentState(prefName, isConfigurable);

    settingsPage = document.createElement(pageId);
    settingsPage.prefs = Object.assign({}, prefs_);
    document.body.appendChild(settingsPage);
    flush();

    await metricsConsentBrowserProxy.whenCalled('getMetricsConsentState');
    await waitAfterNextRender(settingsPage);
    flush();
  }

  teardown(() => {
    settingsPage.remove();
    Router.getInstance().resetRouteForTesting();
  });

  test(
      'Send usage stats toggle visibility in os-settings-privacy-page', () => {
        settingsPage = document.createElement('os-settings-privacy-page');
        document.body.appendChild(settingsPage);
        flush();

        const element =
            settingsPage.shadowRoot!.querySelector('#metricsConsentToggle');

        assertEquals(
            element, null,
            'Send usage toggle should only be visible here when privacy hub' +
                ' is hidden.');
      });

  test(
      'Send usage stats toggle visibility in settings-privacy-hub-subpage',
      () => {
        settingsPage = document.createElement('settings-privacy-hub-subpage');
        settingsPage.prefs = {...PRIVACY_HUB_PREFS};
        document.body.appendChild(settingsPage);
        flush();

        const element =
            settingsPage.shadowRoot!.querySelector('#metricsConsentToggle');

        assertFalse(
            element === null,
            'Send usage toggle should be visible in the privacy hub' +
                ' subpage.');
      });

  test('Deep link to metrics consent toggle', async () => {
    await setUpPage(DEVICE_METRICS_CONSENT_PREF_NAME, /*isConfigurable=*/ true);

    const setting = settingMojom.Setting.kUsageStatsAndCrashReports;
    const params = new URLSearchParams();
    params.append('settingId', setting.toString());
    Router.getInstance().navigateTo(routes.PRIVACY_HUB, params);
    flush();

    const deepLinkElement = settingsPage.shadowRoot!.querySelector<HTMLElement>(
        '#metricsConsentToggle');
    assertTrue(!!deepLinkElement);
    await waitAfterNextRender(deepLinkElement);
    assertEquals(
        deepLinkElement, settingsPage.shadowRoot!.activeElement,
        `Metrics consent toggle should be focused for settingId=${setting}.`);
  });

  test('Toggle disabled if metrics consent is not configurable', async () => {
    await setUpPage(
        DEVICE_METRICS_CONSENT_PREF_NAME, /*isConfigurable=*/ false);

    const toggle =
        settingsPage.shadowRoot!.querySelector(
                                    '#metricsConsentToggle')!.shadowRoot!
            .querySelector('#settingsToggle')!.shadowRoot!.querySelector(
                'cr-toggle');
    assert(toggle);
    await waitAfterNextRender(toggle);

    // The pref is true, so the toggle should be on.
    assertTrue(toggle.checked);

    // Not configurable, so toggle should be disabled.
    assertTrue(toggle.disabled);
  });

  test('Toggle enabled if metrics consent is configurable', async () => {
    await setUpPage(DEVICE_METRICS_CONSENT_PREF_NAME, /*isConfigurable=*/ true);

    const toggle =
        settingsPage.shadowRoot!.querySelector(
                                    '#metricsConsentToggle')!.shadowRoot!
            .querySelector('#settingsToggle')!.shadowRoot!.querySelector(
                'cr-toggle');
    assert(toggle);
    await waitAfterNextRender(toggle);

    // The pref is true, so the toggle should be on.
    assertTrue(toggle.checked);

    // Configurable, so toggle should be enabled.
    assertFalse(toggle.disabled);

    // Toggle.
    toggle.click();
    await metricsConsentBrowserProxy.whenCalled('updateMetricsConsent');

    // Pref should be off now.
    assertFalse(toggle.checked);
  });

  test('Correct pref displayed', async () => {
    await setUpPage(USER_METRICS_CONSENT_PREF_NAME, /*isConfigurable=*/ true);

    const toggle =
        settingsPage.shadowRoot!.querySelector(
                                    '#metricsConsentToggle')!.shadowRoot!
            .querySelector('#settingsToggle')!.shadowRoot!.querySelector(
                'cr-toggle');
    assert(toggle);
    await waitAfterNextRender(toggle);

    // The user consent pref is false, so the toggle should not be checked.
    assertFalse(toggle.checked);

    // Configurable, so toggle should be enabled.
    assertFalse(toggle.disabled);

    // Toggle.
    toggle.click();
    await metricsConsentBrowserProxy.whenCalled('updateMetricsConsent');

    // Pref should be on now.
    assertTrue(toggle.checked);
  });
}

suite(
    '<os-settings-privacy-page> OfficialBuild',
    () => testsuiteForMetricsConsentToggle());
