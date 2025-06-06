// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://certificate-manager/certificate_subpage.js';

import {CertificateSource} from 'chrome://certificate-manager/certificate_manager.mojom-webui.js';
import type {CertificateSubpageElement, SubpageCertificateList} from 'chrome://certificate-manager/certificate_subpage.js';
import {CertificatesBrowserProxy} from 'chrome://certificate-manager/certificates_browser_proxy.js';
import {assertEquals, assertFalse, assertTrue} from 'chrome://webui-test/chai_assert.js';
import {microtasksFinished} from 'chrome://webui-test/test_util.js';

import {TestCertificateManagerProxy} from './certificate_manager_test_support.js';

suite('CertificateSubpageV2Test', () => {
  let certSubpage: CertificateSubpageElement;
  let testProxy: TestCertificateManagerProxy;

  setup(() => {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;
    testProxy = new TestCertificateManagerProxy();
    CertificatesBrowserProxy.setInstance(testProxy);
  });

  function initializeElement() {
    certSubpage = document.createElement('certificate-subpage');

    const subpageCertLists: SubpageCertificateList[] = [
      {
        headerText: 'CRS',
        hideExport: false,
        hideIfEmpty: false,
        hideHeader: true,
        showImport: false,
        showImportAndBind: false,
        certSource: CertificateSource.kChromeRootStore,
      },
      {
        headerText: 'EnterpriseTrusted',
        hideExport: true,
        hideIfEmpty: true,
        hideHeader: false,
        showImport: true,
        showImportAndBind: true,
        certSource: CertificateSource.kEnterpriseTrustedCerts,
      },
    ];

    certSubpage.subpageCertLists = subpageCertLists;
    certSubpage.subpageTitle = 'Subpage Title';
    document.body.appendChild(certSubpage);
  }

  test('element check', async () => {
    testProxy.handler.setCertificatesCallback(() => {
      return {
        certs: [
          {
            sha256hashHex: 'deadbeef1',
            displayName: 'cert1',
            isDeletable: false,
          },
          {
            sha256hashHex: 'deadbeef2',
            displayName: 'cert2',
            isDeletable: false,
          },
        ],
      };
    });

    initializeElement();

    await microtasksFinished();
    assertEquals(
        2, testProxy.handler.getCallCount('getCertificates'),
        'getCertificates called incorrect amount of times');

    const lists = certSubpage.shadowRoot!.querySelectorAll('certificate-list');
    assertEquals(2, lists.length, 'no lists displayed');
    assertEquals('CRS', lists[0]!.headerText, 'crs list header wrong');
    assertEquals(
        CertificateSource.kChromeRootStore, lists[0]!.certSource,
        'crs list certsource wrong');
    assertFalse(lists[0]!.hideExport, 'crs hideexport value incorrect');
    assertFalse(lists[0]!.showImport, 'crs showimport value incorrect');
    assertFalse(
        lists[0]!.showImportAndBind, 'crs showimportAndBind value incorrect');
    assertFalse(lists[0]!.hideIfEmpty, 'crs hideIfEmpty value incorrect');
    assertTrue(lists[0]!.hideHeader, 'crs hideHeader value incorrect');

    assertEquals(
        'EnterpriseTrusted', lists[1]!.headerText,
        'enterprisetrusted list header wrong');
    assertEquals(
        CertificateSource.kEnterpriseTrustedCerts, lists[1]!.certSource,
        'enterprisetrusted list certsource wrong');
    assertTrue(
        lists[1]!.hideExport, 'enterprisetrusted hideexport value incorrect');
    assertTrue(
        lists[1]!.showImport, 'enterprisetrusted showimport value incorrect');
    assertTrue(
        lists[1]!.showImportAndBind,
        'enterprisetrusted showimportAndBind value incorrect');
    assertTrue(
        lists[1]!.hideIfEmpty, 'enterprisetrusted hideifEmpty value incorrect');
    assertFalse(
        lists[1]!.hideHeader, 'enterprisetrusted hideHeader value incorrect');
  });
});
