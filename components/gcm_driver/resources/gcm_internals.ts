// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// <if expr="is_ios">
import 'chrome://resources/js/ios/web_ui.js';
// </if>

import '/strings.m.js';

import {addWebUiListener} from 'chrome://resources/js/cr.js';
import {$, getRequiredElement} from 'chrome://resources/js/util.js';

let isRecording: boolean = false;
let keyPressState = 0;

/**
 * If the info dictionary has property prop, then set the text content of
 * element to the value of this property. Otherwise clear the content.
 * @param info A dictionary of device infos to be displayed.
 * @param prop Name of the property.
 */
function setIfExists(
    info: Record<string, string>, prop: string, elementId: string) {
  const element = $(elementId);
  if (!element) {
    return;
  }

  if (info[prop] !== undefined) {
    element.textContent = info[prop];
  } else {
    element.textContent = '';
  }
}

/**
 * Sets the registeredAppIds from |info| to the element identified by
 * |elementId|. The list will have duplicates counted and visually shown.
 * @param info A dictionary of device infos to be displayed.
 * @param prop Name of the property.
 */
function setRegisteredAppIdsIfExists(
    info: Record<string, string>, prop: string, elementId: string) {
  const element = $(elementId);
  if (!element) {
    return;
  }

  if (info[prop] === undefined || !Array.isArray(info[prop])) {
    return;
  }

  const registeredAppIds = new Map();
  info[prop].forEach(registeredAppId => {
    registeredAppIds.set(
        registeredAppId, (registeredAppIds.get(registeredAppId) || 0) + 1);
  });

  const list = [];
  for (const [registeredAppId, count] of registeredAppIds.entries()) {
    list.push(registeredAppId + (count > 1 ? ` (x${count})` : ``));
  }

  element.textContent = list.join(', ');
}

/**
 * Display device information.
 * @param info A dictionary of device infos to be displayed.
 */
function displayDeviceInfo(info: Record<string, string>) {
  setIfExists(info, 'androidId', 'android-id');
  setIfExists(info, 'androidSecret', 'android-secret');
  setIfExists(info, 'profileServiceCreated', 'profile-service-created');
  setIfExists(info, 'gcmEnabled', 'gcm-enabled');
  setIfExists(info, 'gcmClientCreated', 'gcm-client-created');
  setIfExists(info, 'gcmClientState', 'gcm-client-state');
  setIfExists(info, 'connectionClientCreated', 'connection-client-created');
  setIfExists(info, 'connectionState', 'connection-state');
  setIfExists(info, 'lastCheckin', 'last-checkin');
  setIfExists(info, 'nextCheckin', 'next-checkin');
  setIfExists(info, 'sendQueueSize', 'send-queue-size');
  setIfExists(info, 'resendQueueSize', 'resend-queue-size');

  setRegisteredAppIdsIfExists(info, 'registeredAppIds', 'registered-app-ids');
}

/**
 * Remove all the child nodes of the element.
 */
function removeAllChildNodes(element: HTMLElement) {
  element.textContent = '';
}

/**
 * For each item in line, add a row to the table. Each item is actually a list
 * of sub-items; each of which will have a corresponding cell created in that
 * row, and the sub-item will be displayed in the cell.
 * @param table A HTML tbody element.
 * @param list A list of list of item.
 */
function addRows(table: HTMLElement, list: ItemInfo[]) {
  for (const info of list) {
    const row = document.createElement('tr');

    // The first element is always a timestamp.
    let cell = document.createElement('td');
    const d = new Date(info[0]);
    cell.textContent = d.toString();
    row.appendChild(cell);

    for (let j = 1; j < info.length; ++j) {
      cell = document.createElement('td');
      cell.textContent = info[j] as string;
      row.appendChild(cell);
    }
    table.appendChild(row);
  }
}

/**
 * Refresh all displayed information.
 */
function refreshAll() {
  chrome.send('getGcmInternalsInfo', [false]);
}

/**
 * Toggle the isRecording variable and send it to browser.
 */
function setRecording() {
  isRecording = !isRecording;
  chrome.send('setGcmInternalsRecording', [isRecording]);
}

/**
 * Clear all the activity logs.
 */
function clearLogs() {
  chrome.send('getGcmInternalsInfo', [true]);
}

function initialize() {
  addWebUiListener('set-gcm-internals-info', setGcmInternalsInfo);
  getRequiredElement<HTMLButtonElement>('recording').disabled = true;
  getRequiredElement('refresh').onclick = refreshAll;
  getRequiredElement('recording').onclick = setRecording;
  getRequiredElement('clear-logs').onclick = clearLogs;
  chrome.send('getGcmInternalsInfo', [false]);

  // Recording defaults to on.
  chrome.send('setGcmInternalsRecording', [true]);
}

/**
 * Allows displaying the Android Secret by typing a secret phrase.
 *
 * There are good reasons for displaying the Android Secret associated with
 * the local connection info, but we also need to be careful to make sure that
 * users don't share this value by accident. Therefore we require a secret
 * phrase to be typed into the page for making it visible.
 */
function handleKeyPress(event: KeyboardEvent) {
  const PHRASE = 'secret';
  if (PHRASE.charCodeAt(keyPressState) === event.keyCode) {
    if (++keyPressState < PHRASE.length) {
      return;
    }

    getRequiredElement('android-secret-container')
        .classList.remove('invisible');
  }

  keyPressState = 0;
}

/**
 * Refresh the log html table by clearing it first. If data is not empty, then
 * it will be used to populate the table.
 * @param data A list of list of data items.
 */
function refreshLogTable(tableId: string, data: ItemInfo[]|undefined) {
  const element = $(tableId);
  if (!element) {
    return;
  }

  removeAllChildNodes(element);
  if (data !== undefined) {
    addRows(element, data);
  }
}

type ItemInfo = [number, ...string[]];

interface GcmInternalsInfo {
  deviceInfo: Record<string, string>;
  isRecording: boolean;
  checkinInfo?: ItemInfo[];
  connectionInfo?: ItemInfo[];
  decryptionFailureInfo?: ItemInfo[];
  receiveInfo?: ItemInfo[];
  registrationInfo?: ItemInfo[];
  sendInfo?: ItemInfo[];
}

/**
 * Callback function accepting a dictionary of info items to be displayed.
 * @param infos A dictionary of info items to be displayed.
 */
function setGcmInternalsInfo(infos: GcmInternalsInfo) {
  const recordingEl = getRequiredElement<HTMLButtonElement>('recording');
  recordingEl.textContent =
      infos.isRecording ? 'Stop Recording' : 'Start Recording';
  recordingEl.disabled = false;
  if (infos.deviceInfo !== undefined) {
    displayDeviceInfo(infos.deviceInfo);
  }

  refreshLogTable('checkin-info', infos.checkinInfo);
  refreshLogTable('connection-info', infos.connectionInfo);
  refreshLogTable('registration-info', infos.registrationInfo);
  refreshLogTable('receive-info', infos.receiveInfo);
  refreshLogTable('decryption-failure-info', infos.decryptionFailureInfo);
  refreshLogTable('send-info', infos.sendInfo);
}

document.addEventListener('DOMContentLoaded', initialize);
document.addEventListener('keypress', handleKeyPress);
