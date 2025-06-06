// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ANDROID_BUILD_INFO_H_
#define BASE_ANDROID_BUILD_INFO_H_

#include <jni.h>

#include <string>
#include <vector>

#include "base/android/android_info.h"
#include "base/base_export.h"
#include "base/memory/singleton.h"

namespace base::android {

// DEPRECATED: Please use android_info::SdkVersion.
//
// This enumeration maps to the values returned by BuildInfo::sdk_int(),
// indicating the Android release associated with a given SDK version.
enum SdkVersion {
  SDK_VERSION_JELLY_BEAN = android_info::SDK_VERSION_JELLY_BEAN,
  SDK_VERSION_JELLY_BEAN_MR1 = android_info::SDK_VERSION_JELLY_BEAN_MR1,
  SDK_VERSION_JELLY_BEAN_MR2 = android_info::SDK_VERSION_JELLY_BEAN_MR2,
  SDK_VERSION_KITKAT = android_info::SDK_VERSION_KITKAT,
  SDK_VERSION_KITKAT_WEAR = android_info::SDK_VERSION_KITKAT_WEAR,
  SDK_VERSION_LOLLIPOP = android_info::SDK_VERSION_LOLLIPOP,
  SDK_VERSION_LOLLIPOP_MR1 = android_info::SDK_VERSION_LOLLIPOP_MR1,
  SDK_VERSION_MARSHMALLOW = android_info::SDK_VERSION_MARSHMALLOW,
  SDK_VERSION_NOUGAT = android_info::SDK_VERSION_NOUGAT,
  SDK_VERSION_NOUGAT_MR1 = android_info::SDK_VERSION_NOUGAT_MR1,
  SDK_VERSION_OREO = android_info::SDK_VERSION_OREO,
  SDK_VERSION_O_MR1 = android_info::SDK_VERSION_O_MR1,
  SDK_VERSION_P = android_info::SDK_VERSION_P,
  SDK_VERSION_Q = android_info::SDK_VERSION_Q,
  SDK_VERSION_R = android_info::SDK_VERSION_R,
  SDK_VERSION_S = android_info::SDK_VERSION_S,
  SDK_VERSION_Sv2 = android_info::SDK_VERSION_Sv2,
  SDK_VERSION_T = android_info::SDK_VERSION_T,
  SDK_VERSION_U = android_info::SDK_VERSION_U,
  SDK_VERSION_V = android_info::SDK_VERSION_V,
};

// DEPRECATED: Use AndroidInfo, DeviceInfo or ApkInfo instead.
// These are more efficient because they only retrieve the data being queried.
//
// BuildInfo is a singleton class that stores android build and device
// information. It will be called from Android specific code and gets used
// primarily in crash reporting.
class BASE_EXPORT BuildInfo {
 public:
  BuildInfo(const BuildInfo&) = delete;
  BuildInfo& operator=(const BuildInfo&) = delete;

  ~BuildInfo();

  // Static factory method for getting the singleton BuildInfo instance.
  // Note that ownership is not conferred on the caller and the BuildInfo in
  // question isn't actually freed until shutdown. This is ok because there
  // should only be one instance of BuildInfo ever created.
  static BuildInfo* GetInstance();

  const std::string& device() const { return device_; }

  const std::string& manufacturer() const { return manufacturer_; }

  const std::string& model() const { return model_; }

  const std::string& brand() const { return brand_; }

  const std::string& android_build_id() const { return android_build_id_; }

  const std::string& android_build_fp() const { return android_build_fp_; }

  const std::string& gms_version_code() const;

  void set_gms_version_code_for_test(const std::string& gms_version_code);

  // The package name of the host app which has loaded WebView, retrieved from
  // the application context. In the context of the SDK Runtime, the package
  // name of the app that owns this particular instance of the SDK Runtime will
  // also be included. e.g.
  // com.google.android.sdksandbox:com:com.example.myappwithads
  const std::string& host_package_name() const { return host_package_name_; }

  // The application name (e.g. "Chrome"). For WebView, this is name of the
  // embedding app. In the context of the SDK Runtime, this is the name of the
  // app that owns this particular instance of the SDK Runtime.
  const std::string& host_version_code() const { return host_version_code_; }

  // By default: same as versionCode. For WebView: versionCode of the embedding
  // app. In the context of the SDK Runtime, this is the versionCode of the app
  // that owns this particular instance of the SDK Runtime.
  const std::string& host_package_label() const { return host_package_label_; }

  // The SHA256 of the public certificate used to sign the host application.
  // This will default to an empty string if we were unable to retrieve it.
  const std::string host_signing_cert_sha256();

  const std::string& package_version_code() const {
    return package_version_code_;
  }

  const std::string& package_version_name() const {
    return package_version_name_;
  }

  const std::string& package_name() const { return package_name_; }

  const std::string& resources_version() const { return resources_version_; }

  const std::string& build_type() const { return build_type_; }

  const std::string& board() const { return board_; }

  const std::string& installer_package_name() const {
    return installer_package_name_;
  }

  const std::string& abi_name() const { return abi_name_; }

  int sdk_int() const { return sdk_int_; }

  // Returns the targetSdkVersion of the currently running app. If called from a
  // library, this returns the embedding app's targetSdkVersion.
  //
  // This can only be compared to finalized SDK versions, never against
  // pre-release Android versions. For pre-release Android versions, see the
  // targetsAtLeast*() methods in BuildInfo.java.
  int target_sdk_version() const { return target_sdk_version_; }

  bool is_debug_android() const { return is_debug_android_; }

  bool is_tv() const { return is_tv_; }

  const std::string& version_incremental() const {
    return version_incremental_;
  }

  const std::string& hardware() const { return hardware_; }

  bool is_automotive() const { return is_automotive_; }

  const std::string& codename() const { return codename_; }

  bool is_foldable() const { return is_foldable_; }

  bool is_desktop() const { return is_desktop_; }

  // Available only on Android T+.
  int32_t vulkan_deqp_level() const { return vulkan_deqp_level_; }

  // Available only on android S+. For S-, this method returns empty string.
  const std::string& soc_manufacturer() const { return soc_manufacturer_; }

  bool is_debug_app() const { return is_debug_app_; }

 private:
  friend struct BuildInfoSingletonTraits;

  explicit BuildInfo();

  const std::string brand_;
  const std::string device_;
  const std::string android_build_id_;
  const std::string manufacturer_;
  const std::string model_;
  const int sdk_int_;
  const std::string build_type_;
  const std::string board_;
  const std::string host_package_name_;
  const std::string host_version_code_;
  const std::string host_package_label_;
  const std::string package_name_;
  const std::string package_version_code_;
  const std::string package_version_name_;
  const std::string android_build_fp_;
  const std::string installer_package_name_;
  const std::string abi_name_;
  const std::string resources_version_;
  const int target_sdk_version_;
  const bool is_debug_android_;
  const bool is_tv_;
  const std::string version_incremental_;
  const std::string hardware_;
  const bool is_automotive_;
  const std::string codename_;
  const int32_t vulkan_deqp_level_;
  const bool is_foldable_;
  const std::string soc_manufacturer_;
  const bool is_debug_app_;
  const bool is_desktop_;
};

}  // namespace base::android

#endif  // BASE_ANDROID_BUILD_INFO_H_
