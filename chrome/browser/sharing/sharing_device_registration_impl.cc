// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sharing/sharing_device_registration_impl.h"

#include <stdint.h>

#include <vector>

#include "base/base64url.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/metrics/histogram_functions.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "components/gcm_driver/crypto/p256_key_util.h"
#include "components/gcm_driver/instance_id/instance_id_driver.h"
#include "components/optimization_guide/core/optimization_guide_features.h"
#include "components/prefs/pref_service.h"
#include "components/sharing_message/buildflags.h"
#include "components/sharing_message/pref_names.h"
#include "components/sharing_message/sharing_constants.h"
#include "components/sharing_message/sharing_device_registration_result.h"
#include "components/sharing_message/sharing_sync_preference.h"
#include "components/sharing_message/sharing_target_device_info.h"
#include "components/sharing_message/sharing_utils.h"
#include "components/sync/service/sync_service.h"
#include "components/sync_device_info/device_info.h"
#include "crypto/ec_private_key.h"

#if BUILDFLAG(IS_ANDROID)
#include "chrome/android/chrome_jni_headers/SharingJNIBridge_jni.h"
#endif

using instance_id::InstanceID;
using sync_pb::SharingSpecificFields;

SharingDeviceRegistrationImpl::SharingDeviceRegistrationImpl(
    PrefService* pref_service,
    SharingSyncPreference* sharing_sync_preference,
    instance_id::InstanceIDDriver* instance_id_driver,
    syncer::SyncService* sync_service)
    : pref_service_(pref_service),
      sharing_sync_preference_(sharing_sync_preference),
      instance_id_driver_(instance_id_driver),
      sync_service_(sync_service) {}

SharingDeviceRegistrationImpl::~SharingDeviceRegistrationImpl() = default;

void SharingDeviceRegistrationImpl::RegisterDevice(
    RegistrationCallback callback) {
  if (!CanSendViaSenderID(sync_service_)) {
    OnSharingTargetInfoRetrieved(std::move(callback),
                                 SharingDeviceRegistrationResult::kSuccess,
                                 /*sharing_target_info=*/std::nullopt);
    return;
  }

  // Attempt to register using sender ID when enabled.
  RetrieveTargetInfo(
      kSharingSenderID,
      base::BindOnce(
          &SharingDeviceRegistrationImpl::OnSharingTargetInfoRetrieved,
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void SharingDeviceRegistrationImpl::RetrieveTargetInfo(
    const std::string& sender_id,
    TargetInfoCallback callback) {
  instance_id_driver_->GetInstanceID(kSharingFCMAppID)
      ->GetToken(
          sender_id, instance_id::kGCMScope,
          /*time_to_live=*/base::TimeDelta(),
          /*flags=*/{InstanceID::Flags::kBypassScheduler},
          base::BindOnce(&SharingDeviceRegistrationImpl::OnFCMTokenReceived,
                         weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                         sender_id));
}

void SharingDeviceRegistrationImpl::OnFCMTokenReceived(
    TargetInfoCallback callback,
    const std::string& sender_id,
    const std::string& fcm_token,
    instance_id::InstanceID::Result result) {
  switch (result) {
    case InstanceID::SUCCESS:
      instance_id_driver_->GetInstanceID(kSharingFCMAppID)
          ->GetEncryptionInfo(
              sender_id,
              base::BindOnce(
                  &SharingDeviceRegistrationImpl::OnEncryptionInfoReceived,
                  weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                  fcm_token));
      break;
    case InstanceID::NETWORK_ERROR:
    case InstanceID::SERVER_ERROR:
    case InstanceID::ASYNC_OPERATION_PENDING:
      std::move(callback).Run(
          SharingDeviceRegistrationResult::kFcmTransientError, std::nullopt);
      break;
    case InstanceID::INVALID_PARAMETER:
    case InstanceID::UNKNOWN_ERROR:
    case InstanceID::DISABLED:
      std::move(callback).Run(SharingDeviceRegistrationResult::kFcmFatalError,
                              std::nullopt);
      break;
  }
}

void SharingDeviceRegistrationImpl::OnEncryptionInfoReceived(
    TargetInfoCallback callback,
    const std::string& fcm_token,
    std::string p256dh,
    std::string auth_secret) {
  std::move(callback).Run(
      SharingDeviceRegistrationResult::kSuccess,
      std::make_optional(syncer::DeviceInfo::SharingTargetInfo{
          fcm_token, p256dh, auth_secret}));
}

void SharingDeviceRegistrationImpl::OnSharingTargetInfoRetrieved(
    RegistrationCallback callback,
    SharingDeviceRegistrationResult result,
    std::optional<syncer::DeviceInfo::SharingTargetInfo> sharing_target_info) {
  if (result != SharingDeviceRegistrationResult::kSuccess) {
    std::move(callback).Run(result);
    return;
  }

  if (!sharing_target_info) {
    std::move(callback).Run(SharingDeviceRegistrationResult::kInternalError);
    return;
  }

  base::UmaHistogramBoolean("Sharing.LocalSharingTargetInfoSupportsSync",
                            !!sharing_target_info);
  std::set<SharingSpecificFields::EnabledFeatures> enabled_features =
      GetEnabledFeatures();
  syncer::DeviceInfo::SharingInfo sharing_info(
      sharing_target_info ? std::move(*sharing_target_info)
                          : syncer::DeviceInfo::SharingTargetInfo(),
      /*chime_representative_target_id=*/std::string(),
      std::move(enabled_features));
  sharing_sync_preference_->SetLocalSharingInfo(std::move(sharing_info));
  sharing_sync_preference_->SetFCMRegistration(
      SharingSyncPreference::FCMRegistration(base::Time::Now()));
  std::move(callback).Run(SharingDeviceRegistrationResult::kSuccess);
}

void SharingDeviceRegistrationImpl::UnregisterDevice(
    RegistrationCallback callback) {
  auto registration = sharing_sync_preference_->GetFCMRegistration();
  if (!registration) {
    std::move(callback).Run(
        SharingDeviceRegistrationResult::kDeviceNotRegistered);
    return;
  }

  sharing_sync_preference_->ClearLocalSharingInfo();

  DeleteFCMToken(kSharingSenderID, std::move(callback));
}

void SharingDeviceRegistrationImpl::DeleteFCMToken(
    const std::string& sender_id,
    RegistrationCallback callback) {
  instance_id_driver_->GetInstanceID(kSharingFCMAppID)
      ->DeleteToken(
          sender_id, instance_id::kGCMScope,
          base::BindOnce(&SharingDeviceRegistrationImpl::OnFCMTokenDeleted,
                         weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void SharingDeviceRegistrationImpl::OnFCMTokenDeleted(
    RegistrationCallback callback,
    InstanceID::Result result) {
  switch (result) {
    case InstanceID::SUCCESS:
      // INVALID_PARAMETER is expected if InstanceID.GetToken hasn't been
      // invoked since restart.
    case InstanceID::INVALID_PARAMETER:
      sharing_sync_preference_->ClearFCMRegistration();
      std::move(callback).Run(SharingDeviceRegistrationResult::kSuccess);
      return;
    case InstanceID::NETWORK_ERROR:
    case InstanceID::SERVER_ERROR:
    case InstanceID::ASYNC_OPERATION_PENDING:
      std::move(callback).Run(
          SharingDeviceRegistrationResult::kFcmTransientError);
      return;
    case InstanceID::UNKNOWN_ERROR:
    case InstanceID::DISABLED:
      std::move(callback).Run(SharingDeviceRegistrationResult::kFcmFatalError);
      return;
  }

  NOTREACHED();
}

std::set<SharingSpecificFields::EnabledFeatures>
SharingDeviceRegistrationImpl::GetEnabledFeatures() const {
  // Used in tests
  if (enabled_features_testing_value_) {
    return enabled_features_testing_value_.value();
  }

  std::set<SharingSpecificFields::EnabledFeatures> enabled_features;
  if (IsClickToCallSupported()) {
    enabled_features.insert(SharingSpecificFields::CLICK_TO_CALL_V2);
  }
  if (IsSharedClipboardSupported()) {
    enabled_features.insert(SharingSpecificFields::SHARED_CLIPBOARD_V2);
  }
  if (IsSmsFetcherSupported()) {
    enabled_features.insert(SharingSpecificFields::SMS_FETCHER);
  }
  if (IsRemoteCopySupported()) {
    enabled_features.insert(SharingSpecificFields::REMOTE_COPY);
  }
  if (IsOptimizationGuidePushNotificationSupported()) {
    enabled_features.insert(
        SharingSpecificFields::OPTIMIZATION_GUIDE_PUSH_NOTIFICATION);
  }
#if BUILDFLAG(ENABLE_DISCOVERY)
  enabled_features.insert(SharingSpecificFields::DISCOVERY);
#endif

  return enabled_features;
}

bool SharingDeviceRegistrationImpl::IsClickToCallSupported() const {
#if BUILDFLAG(IS_ANDROID)
  JNIEnv* env = jni_zero::AttachCurrentThread();
  return Java_SharingJNIBridge_isTelephonySupported(env);
#else
  return false;
#endif
}

bool SharingDeviceRegistrationImpl::IsSharedClipboardSupported() const {
  // Check the enterprise policy for Shared Clipboard.
  if (pref_service_ &&
      !pref_service_->GetBoolean(prefs::kSharedClipboardEnabled)) {
    return false;
  }
  return true;
}

bool SharingDeviceRegistrationImpl::IsSmsFetcherSupported() const {
#if BUILDFLAG(IS_ANDROID)
  return true;
#else
  return false;
#endif
}

bool SharingDeviceRegistrationImpl::IsRemoteCopySupported() const {
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
  return true;
#else
  return false;
#endif
}

bool SharingDeviceRegistrationImpl::
    IsOptimizationGuidePushNotificationSupported() const {
  return optimization_guide::features::IsOptimizationHintsEnabled() &&
         optimization_guide::features::IsPushNotificationsEnabled();
}

void SharingDeviceRegistrationImpl::SetEnabledFeaturesForTesting(
    std::set<SharingSpecificFields::EnabledFeatures> enabled_features) {
  enabled_features_testing_value_ = std::move(enabled_features);
}
