// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/it2me/it2me_constants.h"

namespace remoting {

const char kFeatureAccessTokenAuth[] = "accessTokenAuth";
const char kFeatureAuthorizedHelper[] = "authorizedHelper";

const char kConnectMessage[] = "connect";
const char kUserName[] = "userName";
const char kAuthServiceWithToken[] = "authServiceWithToken";
const char kAccessToken[] = "accessToken";
const char kSignalingAccessToken[] = "signalingAccessToken";
const char kApiAccessToken[] = "apiAccessToken";
const char kLocalJid[] = "localJid";
const char kIsEnterpriseAdminUser[] = "isEnterpriseAdminUser";
const char kUseElevatedHost[] = "useElevatedHost";
const char kIceConfig[] = "iceConfig";
const char kAuthorizedHelper[] = "authorizedHelper";
const char kUseCorpSessionAuthz[] = "useCorpSessionAuthz";
const char kIsCorpUser[] = "isCorpUser";
const char kConnectResponse[] = "connectResponse";

const char kHostStateChangedMessage[] = "hostStateChanged";
const char kState[] = "state";
const char kHostStateError[] = "ERROR";
const char kHostStateStarting[] = "STARTING";
const char kHostStateRequestedAccessCode[] = "REQUESTED_ACCESS_CODE";
const char kHostStateDomainError[] = "INVALID_DOMAIN_ERROR";
const char kHostStateReceivedAccessCode[] = "RECEIVED_ACCESS_CODE";
const char kHostStateDisconnected[] = "DISCONNECTED";
const char kHostStateConnecting[] = "CONNECTING";
const char kHostStateConnected[] = "CONNECTED";
const char kAccessCode[] = "accessCode";
const char kAccessCodeLifetime[] = "accessCodeLifetime";
const char kClient[] = "client";
const char kDisconnectReason[] = "disconnectReason";

const char kDisconnectMessage[] = "disconnect";
const char kDisconnectResponse[] = "disconnectResponse";

const char kErrorMessage[] = "error";
const char kErrorMessageCode[] = "errorCode";

const char kNatPolicyChangedMessage[] = "natPolicyChanged";
const char kNatPolicyChangedMessageNatEnabled[] = "natTraversalEnabled";
const char kNatPolicyChangedMessageRelayEnabled[] = "relayConnectionsAllowed";

const char kPolicyErrorMessage[] = "policyError";

const char kUpdateAccessTokensMessage[] = "updateAccessTokens";

const char kSessionParamsDict[] = "sessionParamsDict";

const char kEnterpriseParamsDict[] = "enterpriseParamsDict";

const char kReconnectParamsDict[] = "reconnectParamsDict";
const char kReconnectSupportId[] = "reconnectSupportId";
const char kReconnectHostSecret[] = "reconnectHostSecret";
const char kReconnectPrivateKey[] = "reconnectPrivateKey";
const char kReconnectFtlDeviceId[] = "reconnectFtlDeviceId";
const char kReconnectClientFtlAddress[] = "reconnectClientFtlAddress";

}  // namespace remoting
