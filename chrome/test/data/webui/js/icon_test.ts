// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {getFavicon, getFaviconForPageURL, getFileIconUrl} from 'chrome://resources/js/icon.js';
import {isAndroid, isChromeOS, isLinux, isMac, isWindows} from 'chrome://resources/js/platform.js';
import {assertEquals, assertFalse, assertTrue} from 'chrome://webui-test/chai_assert.js';

suite('IconTest', function() {
  test('GetFaviconForPageURL', function() {
    const url = 'http://foo.com';

    function getExpectedImageSet(size: number): string {
      const expectedDesktop = 'image-set(' +
          `url("chrome://favicon2/?size=${size}&scaleFactor=1x&pageUrl=` +
          encodeURIComponent(url) + '&allowGoogleServerFallback=0") 1x, ' +
          `url("chrome://favicon2/?size=${size}&scaleFactor=2x&pageUrl=` +
          encodeURIComponent(url) + '&allowGoogleServerFallback=0") 2x)';
      const expectedOther = 'image-set(' +
          `url("chrome://favicon2/?size=${size}&scaleFactor=1x&pageUrl=` +
          encodeURIComponent(url) + '&allowGoogleServerFallback=0") 1x, ' +
          `url("chrome://favicon2/?size=${size}&` +
          `scaleFactor=${window.devicePixelRatio}x&pageUrl=` +
          encodeURIComponent(url) + '&allowGoogleServerFallback=0") ' +
          `${window.devicePixelRatio}x)`;

      // Android simulator returns true for isLinux, so also check isAndroid.
      const isDesktop =
          (isMac || isChromeOS || isWindows || isLinux) && !isAndroid;
      return isDesktop ? expectedDesktop : expectedOther;
    }

    assertEquals(getExpectedImageSet(16), getFaviconForPageURL(url, false));
    assertEquals(
        getExpectedImageSet(24), getFaviconForPageURL(url, false, '', 24));
  });

  test('GetFaviconForPageURL_ForceLightMode', () => {
    const url = 'http://foo.com';
    assertFalse(
        getFaviconForPageURL(url, false, '', 16).includes('forceLightMode'));
    assertFalse(
        getFaviconForPageURL(url, false, '', 16, /* forceLightMode */ false)
            .includes('forceLightMode'));
    assertTrue(
        getFaviconForPageURL(url, false, '', 16, /* forceLightMode */ true)
            .includes('forceLightMode=true'));
  });

  test('GetFavicon', function() {
    const url = 'http://foo.com/foo.ico';
    const expectedDesktop = 'image-set(' +
        'url("chrome://favicon2/?size=16&scaleFactor=1x&iconUrl=' +
        encodeURIComponent('http://foo.com/foo.ico') + '") 1x, ' +
        'url("chrome://favicon2/?size=16&scaleFactor=2x&iconUrl=' +
        encodeURIComponent('http://foo.com/foo.ico') + '") 2x)';
    const expectedOther = 'image-set(' +
        'url("chrome://favicon2/?size=16&scaleFactor=1x&iconUrl=' +
        encodeURIComponent('http://foo.com/foo.ico') + '") 1x, ' +
        'url("chrome://favicon2/?size=16&' +
        `scaleFactor=${window.devicePixelRatio}x&iconUrl=` +
        encodeURIComponent('http://foo.com/foo.ico') + '") ' +
        `${window.devicePixelRatio}x)`;

    // Android simulator returns true for isLinux, so also check isAndroid.
    const isDesktop =
        (isMac || isChromeOS || isWindows || isLinux) && !isAndroid;
    const expected = isDesktop ? expectedDesktop : expectedOther;
    assertEquals(expected, getFavicon(url));
  });

  test('GetFileIconUrl', function() {
    assertEquals(
        getFileIconUrl('file path'),
        'chrome://fileicon/?path=file+path&scale=' + window.devicePixelRatio +
            'x');
  });
});
