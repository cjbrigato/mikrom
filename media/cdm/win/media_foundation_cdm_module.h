// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_CDM_WIN_MEDIA_FOUNDATION_CDM_MODULE_H_
#define MEDIA_CDM_WIN_MEDIA_FOUNDATION_CDM_MODULE_H_

#include <mfcontentdecryptionmodule.h>
#include <wrl.h>

#include <string>

#include "base/files/file_path.h"
#include "base/scoped_native_library.h"
#include "media/base/media_export.h"

namespace media {

// A singleton in the MediaFoundationService process in charge of CDM loading
// and IMFContentDecryptionModuleFactory activation.
class MEDIA_EXPORT MediaFoundationCdmModule {
 public:
  static MediaFoundationCdmModule* GetInstance();

  // Loads the CDM at `cdm_path` if not empty. So `ActivateCdmFactory()` can
  // activate the IMFContentDecryptionModuleFactory from the CDM later. If the
  // CDM is an OS or store CDM, `cdm_path` could be empty. See implementation
  // details in ActivateCdmFactory() for how OS or store CDMs are handled.
  // Must only be called once.
  bool Initialize(const base::FilePath& cdm_path);

  HRESULT GetCdmFactory(
      const std::string& key_system,
      Microsoft::WRL::ComPtr<IMFContentDecryptionModuleFactory>& cdm_factory);

  // Returns true if an OS CDM is used. For example, the PlayReady CDM.
  // OS CDMs are not loaded from a CDM path since they are part of the OS.
  bool IsOsCdm() const {
    return is_os_cdm_for_testing_.value_or(cdm_path_.empty());
  }

  void SetIsOsCdmForTesting(bool is_os_cdm) {
    is_os_cdm_for_testing_ = is_os_cdm;
  }

 private:
  MediaFoundationCdmModule();
  MediaFoundationCdmModule(const MediaFoundationCdmModule&) = delete;
  MediaFoundationCdmModule& operator=(const MediaFoundationCdmModule&) = delete;
  ~MediaFoundationCdmModule();

  HRESULT ActivateCdmFactory();

  // Declared first so it's destructed after `cdm_factory_`.
  base::ScopedNativeLibrary library_;

  // Indicates whether Initialize() has been called.
  bool initialized_ = false;

  // Indicates whether ActivateCdmFactory() has been called.
  bool activated_ = false;

  std::optional<bool> is_os_cdm_for_testing_;

  base::FilePath cdm_path_;
  std::string key_system_;
  Microsoft::WRL::ComPtr<IMFContentDecryptionModuleFactory> cdm_factory_;
};

}  // namespace media

#endif  // MEDIA_CDM_WIN_MEDIA_FOUNDATION_CDM_MODULE_H_
