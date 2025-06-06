// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {Uuid as mojoBase_mojom_Uuid,} from '//resources/mojo/mojo/public/mojom/base/uuid.mojom-webui.js';

import {MahiUntrustedPageCallbackRouter, MahiUntrustedServiceRemote, MantisUntrustedPageCallbackRouter, OcrUntrustedPageCallbackRouter, OcrUntrustedServiceRemote, UntrustedServiceFactory} from './media_app_ui_untrusted.mojom-webui.js';

// Used to make calls on the remote OcrUntrustedService interface. Singleton
// that client modules can use directly.
let ocrUntrustedService: OcrUntrustedServiceRemote;

// Use this subscribe to events e.g.
// `ocrCallbackRouter.onEventOccurred.addListener(handleEvent)`.
export const ocrCallbackRouter = new OcrUntrustedPageCallbackRouter();

// Used to create a connection to the services used in the untrusted context
// (Mahi, OCR, and Mantis).
const factoryRemote = UntrustedServiceFactory.getRemote();

// Called when a new file that may require OCR is loaded. Closes the existing
// pipe and establishes a new one.
export function connectToOcrUntrustedService() {
  if (ocrUntrustedService) {
    ocrUntrustedService.$.close();
  }
  ocrUntrustedService = new OcrUntrustedServiceRemote();
  factoryRemote.createOcrUntrustedService(
      ocrUntrustedService.$.bindNewPipeAndPassReceiver(),
      ocrCallbackRouter.$.bindNewPipeAndPassRemote());
  return ocrUntrustedService;
}

// Used to make calls on the remote MahiUntrustedService interface.
// Singleton that client modules can use directly.
let mahiUntrustedService: MahiUntrustedServiceRemote;

// Use this subscribe to Mahi concerned events e.g.
// `mahiCallbackRouter.eventOrRequest.addListener(handleEvent)`.
export const mahiCallbackRouter = new MahiUntrustedPageCallbackRouter();

// Called when a new PDF file that may support Mahi feature is loaded. Closes
// the existing pipe and establish a new one.
export function connectToMahiUntrustedService(fileName?: string) {
  if (mahiUntrustedService) {
    mahiUntrustedService.$.close();
  }
  mahiUntrustedService = new MahiUntrustedServiceRemote();
  factoryRemote.createMahiUntrustedService(
      mahiUntrustedService.$.bindNewPipeAndPassReceiver(),
      mahiCallbackRouter.$.bindNewPipeAndPassRemote(), fileName ?? '');
  return mahiUntrustedService;
}

// Use this to subscribe Mantis events or requests e.g.
// `mantisCallbackRouter.eventOrRequest.addListener(handleEvent)`.
export const mantisCallbackRouter = new MantisUntrustedPageCallbackRouter();

export async function isMantisAvailable() {
  const {result} = await factoryRemote.isMantisAvailable();
  return result;
}

export async function connectToMantisUntrustedService(
    dlcUuid: (mojoBase_mojom_Uuid|null)) {
  const {result} = await factoryRemote.createMantisUntrustedService(
      mantisCallbackRouter.$.bindNewPipeAndPassRemote(), dlcUuid);
  return result;
}
