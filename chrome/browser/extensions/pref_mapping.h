// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_PREF_MAPPING_H_
#define CHROME_BROWSER_EXTENSIONS_PREF_MAPPING_H_

#include <map>
#include <memory>
#include <string>

#include "base/containers/span.h"
#include "base/memory/singleton.h"
#include "build/build_config.h"
#include "extensions/buildflags/buildflags.h"
#include "extensions/common/permissions/api_permission.h"

static_assert(BUILDFLAG(ENABLE_EXTENSIONS_CORE));

namespace extensions {

class PrefTransformerInterface;

struct PrefMappingEntry {
  // Name of the preference referenced by the extension API JSON.
  const char* extension_pref;

  // Name of the preference in the PrefStores.
  const char* browser_pref;

  // Permission required to read and observe this preference.
  // Use APIPermissionID::kInvalid for `read_permission` to express that
  // the read permission should not be granted.
  extensions::mojom::APIPermissionID read_permission;

  // Permission required to write this preference.
  // Use APIPermissionID::kInvalid for `write_permission` to express that
  // the write permission should not be granted.
  extensions::mojom::APIPermissionID write_permission;
};

class PrefMapping {
 public:
  PrefMapping(const PrefMapping&) = delete;
  PrefMapping& operator=(const PrefMapping&) = delete;

  static PrefMapping* GetInstance();

  static base::span<const PrefMappingEntry> GetMappings();

  bool FindBrowserPrefForExtensionPref(
      const std::string& extension_pref,
      std::string* browser_pref,
      extensions::mojom::APIPermissionID* read_permission,
      extensions::mojom::APIPermissionID* write_permission) const;

  bool FindEventForBrowserPref(
      const std::string& browser_pref,
      std::string* event_name,
      extensions::mojom::APIPermissionID* permission) const;

  PrefTransformerInterface* FindTransformerForBrowserPref(
      const std::string& browser_pref) const;

  void RegisterPrefTransformer(
      const std::string& browser_pref,
      std::unique_ptr<PrefTransformerInterface> transformer);

 private:
  friend struct base::DefaultSingletonTraits<PrefMapping>;

  PrefMapping();
  ~PrefMapping();

  struct PrefMapData {
    PrefMapData() = default;

    PrefMapData(const std::string& pref_name,
                extensions::mojom::APIPermissionID read,
                extensions::mojom::APIPermissionID write)
        : pref_name(pref_name),
          read_permission(read),
          write_permission(write) {}

    // Browser or extension preference to which the data maps.
    std::string pref_name;

    // Permission needed to read the preference.
    extensions::mojom::APIPermissionID read_permission =
        extensions::mojom::APIPermissionID::kInvalid;

    // Permission needed to write the preference.
    extensions::mojom::APIPermissionID write_permission =
        extensions::mojom::APIPermissionID::kInvalid;
  };

  using PrefMap = std::map<std::string, PrefMapData>;

  // Mapping from extension pref keys to browser pref keys and permissions.
  PrefMap mapping_;

  // Mapping from browser pref keys to extension event names and permissions.
  PrefMap event_mapping_;

  // Mapping from browser pref keys to transformers.
  std::map<std::string, std::unique_ptr<PrefTransformerInterface>>
      transformers_;

  std::unique_ptr<PrefTransformerInterface> identity_transformer_;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_PREF_MAPPING_H_
