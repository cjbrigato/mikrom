// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SHARING_SHARING_DEVICE_REGISTRATION_IMPL_H_
#define CHROME_BROWSER_SHARING_SHARING_DEVICE_REGISTRATION_IMPL_H_

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/gcm_driver/instance_id/instance_id.h"
#include "components/sync/protocol/device_info_specifics.pb.h"
#include "components/sync_device_info/device_info.h"
#include "components/sharing_message/sharing_device_registration.h"

class PrefService;

namespace instance_id {
class InstanceIDDriver;
}  // namespace instance_id

namespace syncer {
class SyncService;
}  // namespace syncer

enum class SharingDeviceRegistrationResult;
class SharingSyncPreference;

// Responsible for registering and unregistering device with
// SharingSyncPreference.
class SharingDeviceRegistrationImpl : public SharingDeviceRegistration {
 public:
  using RegistrationCallback =
      base::OnceCallback<void(SharingDeviceRegistrationResult)>;
  using TargetInfoCallback = base::OnceCallback<void(
      SharingDeviceRegistrationResult,
      std::optional<syncer::DeviceInfo::SharingTargetInfo>)>;

  SharingDeviceRegistrationImpl(PrefService* pref_service,
                            SharingSyncPreference* prefs,
                            instance_id::InstanceIDDriver* instance_id_driver,
                            syncer::SyncService* sync_service);

  SharingDeviceRegistrationImpl(const SharingDeviceRegistrationImpl&) = delete;
  SharingDeviceRegistrationImpl& operator=(const SharingDeviceRegistrationImpl&) =
      delete;

  ~SharingDeviceRegistrationImpl() override;

  // Registers device with sharing sync preferences. Takes a |callback| function
  // which receives the result of FCM registration for device.
  void RegisterDevice(RegistrationCallback callback) override;

  // Un-registers device with sharing sync preferences.
  void UnregisterDevice(RegistrationCallback callback) override;

  // Returns if device can handle receiving phone numbers for calling.
  bool IsClickToCallSupported() const override;

  // Returns if device can handle receiving of shared clipboard contents.
  bool IsSharedClipboardSupported() const override;

  // Returns if device can handle receiving of sms fetcher requests.
  bool IsSmsFetcherSupported() const override;

  // Returns if device can handle receiving of remote copy contents.
  bool IsRemoteCopySupported() const override;

  // Returns if device can handle receiving of optimization guide push
  // notification.
  bool IsOptimizationGuidePushNotificationSupported() const override;

    // For testing
  void SetEnabledFeaturesForTesting(
      std::set<sync_pb::SharingSpecificFields_EnabledFeatures>
          enabled_features) override;

 private:
  FRIEND_TEST_ALL_PREFIXES(SharingDeviceRegistrationImplTest,
                           RegisterDeviceTest_Success);

  void RetrieveTargetInfo(const std::string& sender_id,
                          TargetInfoCallback callback);

  void OnFCMTokenReceived(TargetInfoCallback callback,
                          const std::string& sender_id,
                          const std::string& fcm_token,
                          instance_id::InstanceID::Result result);

  void OnEncryptionInfoReceived(TargetInfoCallback callback,
                                const std::string& fcm_token,
                                std::string p256dh,
                                std::string auth_secret);

  void OnSharingTargetInfoRetrieved(
      RegistrationCallback callback,
      SharingDeviceRegistrationResult result,
      std::optional<syncer::DeviceInfo::SharingTargetInfo> sharing_target_info);

  void DeleteFCMToken(const std::string& sender_id,
                      RegistrationCallback callback);

  void OnFCMTokenDeleted(RegistrationCallback callback,
                         instance_id::InstanceID::Result result);

  // Computes and returns a set of all enabled features on the device.
  std::set<sync_pb::SharingSpecificFields_EnabledFeatures> GetEnabledFeatures()
      const;

  raw_ptr<PrefService> pref_service_;
  raw_ptr<SharingSyncPreference> sharing_sync_preference_;
  raw_ptr<instance_id::InstanceIDDriver> instance_id_driver_;
  raw_ptr<syncer::SyncService> sync_service_;
  std::optional<std::set<sync_pb::SharingSpecificFields_EnabledFeatures>>
      enabled_features_testing_value_;

  base::WeakPtrFactory<SharingDeviceRegistrationImpl> weak_ptr_factory_{this};
};

#endif  // CHROME_BROWSER_SHARING_SHARING_DEVICE_REGISTRATION_IMPL_H_
