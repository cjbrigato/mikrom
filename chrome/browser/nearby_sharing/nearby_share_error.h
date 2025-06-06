// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NEARBY_SHARING_NEARBY_SHARE_ERROR_H_
#define CHROME_BROWSER_NEARBY_SHARING_NEARBY_SHARE_ERROR_H_

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused. If entries are added, kMaxValue should
// be updated. Keep in sync with the NearbyShareError UMA enum defined in
// //tools/metrics/histograms/metadata/nearby/enums.xml.
//
// LINT.IfChange(NearbyShareError)
enum class NearbyShareError {
  kPublicCertificateHasNoBluetoothMacAddress = 0,
  kPublicCertificateHasInvalidBluetoothMacAddress = 1,
  kRegisterSendSurfaceAlreadyRegistered = 2,
  kRegisterSendSurfaceNoAvailableConnectionMedium = 3,
  kUnregisterSendSurfaceUnknownTransferUpdateCallback = 4,
  kRegisterReceiveSurfaceNoAvailableConnectionMedium = 5,
  kRegisterReceiveSurfaceTransferCallbackAlreadyRegisteredDifferentState = 6,
  kSendAttachmentsNotScanning = 7,
  kSendAttachmentsUnknownShareTarget = 8,
  kSendAttachmentsNoAttachments = 9,
  kSendAttachmentsCouldNotCreateLocalEndpointInfo = 10,
  kAcceptUnknownShareTarget = 11,
  kAcceptNotAwaitingLocalConfirmation = 12,
  kRejectUnknownShareTarget = 13,
  kCancelUnknownShareTarget = 14,
  kMaxNearbyProcessRestart = 15,
  kBindToNearbyProcessReferenceExistsOrDisabled = 16,
  kBindToNearbyProcessFailedToGetReference = 17,
  kOnIncomingConnectionAcceptedFailedToGetDecoder = 18,
  kGetBluetoothAdapterUnsupported = 19,
  kStartFastInitiationAdvertisingFailed = 20,
  HandleEndpointDiscoveredFailedToGetDecoder = 21,
  kOutgoingAdvertisementDecodedFailedToParse = 22,
  kOutgoingDecryptedCertificateFailedToCreateShareTarget = 23,
  kSendPayloadsMissingConnection = 24,
  kSendPayloadsMissingTransferUpdateCallback = 25,
  kSendPayloadsMissingEndpointId = 26,
  kPayloadPathsRegisteredFailed = 27,
  kPayloadPathsRegisteredUnknownShareTarget = 28,
  kPayloadPathsRegisteredMissingTransferUpdateCallback = 29,
  kPayloadPathsRegisteredMissingEndpointId = 30,
  kOutgoingConnectionFailedtoInitiateConnection = 31,
  kSendIntroductionFailedToGetShareTarget = 32,
  kSendIntroductionMissingTransferUpdateCallback = 33,
  kSendIntroductionNoSendTransferCallbacks = 34,
  kSendIntroductionNoPayloads = 35,
  kCreatePayloadsNoAttachments = 36,
  kCreatePayloadsNoFileOrTextPayloads = 37,
  kCreatePayloadsFilePayloadWithoutPath = 38,
  kOnCreatePayloadsFailed = 39,
  kOnOpenFilesFailed = 40,
  kFailUnknownShareTarget = 41,
  kIncomingAdvertisementDecodedInvalidConnection = 42,
  kIncomingAdvertisementDecodedFailedToParse = 43,
  kCloseConnectionInvalidConnection = 44,
  kIncomingDecryptedCertificateInvalidConnection = 45,
  kIncomingDecryptedCertificateFailedToCreateShareTarget = 46,
  kRunPairedKeyVerificationFailedToReadAuthenticationToken = 47,
  kIncomingConnectionKeyVerificationInvalidConnectionOrEndpointId = 48,
  kIncomingConnectionKeyVerificationFailed = 49,
  kIncomingConnectionKeyVerificationUnknownResult = 50,
  kOutgoingConnectionKeyVerificationMissingConnection = 51,
  kOutgoingConnectionKeyVerificationMissingTransferUpdateCallback = 52,
  kOutgoingConnectionKeyVerificationFailed = 53,
  kOutgoingConnectionKeyVerificationUnknownResult = 54,
  kReceivedIntroductionMissingConnection = 55,
  kReceivedIntroductionInvalidFrame = 56,
  kReceivedIntroductionInvalidAttachmentSize = 57,
  kReceivedIntroductionTotalFileSizeOverflow = 58,
  kReceivedIntroductionInvalidTextAttachmentSize = 59,
  kReceivedIntroductionInvalidWifiSSID = 60,
  kReceivedIntroductionShareTargetNoAttachment = 61,
  kReceiveConnectionResponseMissingConnection = 62,
  kReceiveConnectionResponseMissingTransferUpdateCallback = 63,
  kReceiveConnectionResponseInvalidFrame = 64,
  kReceiveConnectionResponseNotEnoughSpace = 65,
  kReceiveConnectionResponseUnsupportedAttachmentType = 66,
  kReceiveConnectionResponseTimedOut = 67,
  kReceiveConnectionResponseConnectionFailed = 68,
  kStorageCheckCompletedNotEnoughSpace = 69,
  kStorageCheckCompletedMissingConnection = 70,
  kStorageCheckCompletedMissingTransferUpdateCallback = 71,
  kStorageCheckCompletedNoIncomingShareTarget = 72,
  kStorageCheckCompletedNoFramesReader = 73,
  kFrameReadNoFrameReader = 74,
  kIncomingMutualAcceptanceTimeout = 75,
  kOutgoingMutualAcceptanceTimeout = 76,
  kCreateShareTargetFailedToRetreivePublicCertificate = 77,
  kCreateShareTargetFailedToRetreiveDeviceName = 78,
  kIncomingPayloadsCompleteMissingConnection = 79,
  kIncomingPayloadsCompleteMissingPayloadId = 80,
  kIncomingPayloadsCompleteMissingPayload = 81,
  kIncomingPayloadsCompleteMissingTextPayloadId = 82,
  kIncomingPayloadsCompleteMissingTextPayload = 83,
  kIncomingPayloadsCompleteTextPayloadEmptyBytes = 84,
  kIncomingPayloadsCompleteMissingWifiPayloadId = 85,
  kIncomingPayloadsCompleteMissingWifiPayload = 86,
  kIncomingPayloadsCompleteWifiPayloadEmptyBytes = 87,
  kIncomingPayloadsCompleteWifiFailedToParse = 88,
  kIncomingPayloadsCompleteWifiNoPassword = 89,
  kIncomingPayloadsCompleteWifiHiddenNetwork = 90,
  kDisconnectFailedToGetShareTargetInfo = 91,
  kDisconnectMissingEndpointId = 92,
  kGetBluetoothMacAddressForShareTargetNoShareTargetInfo = 93,
  kGetBluetoothMacAddressForShareTargetNoDecryptedPublicCertificate = 94,
  kStartAdvertisingFailed = 95,
  kStopAdvertisingFailed = 96,
  kStartDiscoveryFailed = 97,
  kMaxValue = kStartDiscoveryFailed
};
// LINT.ThenChange(//tools/metrics/histograms/metadata/nearby/enums.xml:NearbyShareError)

#endif  // CHROME_BROWSER_NEARBY_SHARING_NEARBY_SHARE_ERROR_H_
