// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/updater/win/manifest_util.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/version.h"
#include "base/win/windows_version.h"
#include "chrome/updater/util/win_util.h"
#include "chrome/updater/win/protocol_parser_xml.h"
#include "components/update_client/protocol_parser.h"
#include "components/update_client/utils.h"

namespace updater {
namespace {

constexpr char kArchAmd64Omaha3[] = "x64";

std::optional<base::FilePath> GetOfflineManifest(
    const base::FilePath& offline_dir,
    const std::string& app_id) {
  // Check manifest with fixed name first.
  base::FilePath manifest_path = offline_dir.Append(L"OfflineManifest.gup");
  if (base::PathExists(manifest_path)) {
    return manifest_path;
  }

  // Then check the legacy app specific manifest.
  manifest_path =
      offline_dir.AppendUTF8(app_id).AddExtension(FILE_PATH_LITERAL(".gup"));
  return base::PathExists(manifest_path)
             ? std::optional<base::FilePath>(manifest_path)
             : std::nullopt;
}

std::optional<ProtocolParserXML::Results> ParseOfflineManifest(
    const base::FilePath& offline_dir,
    const std::string& app_id) {
  std::optional<base::FilePath> manifest_path =
      GetOfflineManifest(offline_dir, app_id);
  if (!manifest_path) {
    VLOG(2) << "Cannot find manifest file in: " << offline_dir;
    return std::nullopt;
  }

  std::optional<int64_t> file_size = base::GetFileSize(manifest_path.value());
  if (!file_size.has_value()) {
    VLOG(2) << "Cannot determine manifest file size.";
    return std::nullopt;
  }

  static constexpr int64_t kMaxManifestSize = 1024 * 1024;
  if (file_size.value() > kMaxManifestSize) {
    VLOG(2) << "Manifest file is too large.";
    return std::nullopt;
  }

  std::string contents(file_size.value(), '\0');
  if (base::ReadFile(manifest_path.value(), &contents[0], file_size.value()) ==
      -1) {
    VLOG(2) << "Failed to load manifest file: " << manifest_path.value();
    return std::nullopt;
  }
  std::optional<ProtocolParserXML::Results> parsed_manifest =
      ProtocolParserXML::Parse(contents);
  VLOG_IF(2, !parsed_manifest)
      << "Failed to parse XML manifest file: " << manifest_path.value();
  return parsed_manifest;
}

}  // namespace

void ReadInstallCommandFromManifest(
    const std::wstring& offline_dir_guid,
    const std::string& app_id,
    const std::string& install_data_index,
    OfflineManifestSystemRequirements& requirements,
    std::string& installer_version,
    base::FilePath& installer_path,
    std::string& install_args,
    std::string& install_data) {
  if (offline_dir_guid.empty()) {
    VLOG(1) << "Unexpected: offline install without an offline directory.";
    return;
  }

  if (!IsGuid(offline_dir_guid)) {
    VLOG(1) << "Unexpected: offline directory needs to be a GUID: "
            << offline_dir_guid;
    return;
  }

  const base::FilePath offline_dir = [&offline_dir_guid] {
    base::FilePath offline_dir;
    return base::PathService::Get(base::DIR_EXE, &offline_dir)
               ? offline_dir.Append(L"Offline").Append(offline_dir_guid)
               : base::FilePath();
  }();
  if (offline_dir.empty()) {
    VLOG(1) << "Unexpected: offline directory empty.";
    return;
  }

  const std::optional<ProtocolParserXML::Results> manifest_parsed =
      ParseOfflineManifest(offline_dir, app_id);
  if (!manifest_parsed) {
    return;
  }

  requirements = manifest_parsed->system_requirements;
  const std::vector<ProtocolParserXML::App>& app_list = manifest_parsed->apps;
  auto it = std::ranges::find_if(
      app_list, [&app_id](const ProtocolParserXML::App& result) {
        return base::EqualsCaseInsensitiveASCII(result.app_id, app_id);
      });
  if (it == std::end(app_list)) {
    VLOG(2) << "No manifest data for app: " << app_id;
    return;
  }

  installer_version = it->manifest.version;
  installer_path = [&offline_dir, &app_id, &it] {
    const base::FilePath app_dir(offline_dir.AppendUTF8(app_id));
    const base::FilePath path(app_dir.AppendUTF8(it->manifest.run));
    return base::PathExists(path)
               ? path
               : base::FileEnumerator(
                     app_dir, false, base::FileEnumerator::FILES, {},
                     base::FileEnumerator::FolderSearchPolicy::ALL,
                     base::FileEnumerator::ErrorPolicy::IGNORE_ERRORS)
                     .Next();
  }();
  install_args = it->manifest.arguments;

  if (!install_data_index.empty()) {
    auto data_iter =
        std::ranges::find(it->data, install_data_index,
                          &ProtocolParserXML::Data::install_data_index);
    if (data_iter == std::end(it->data)) {
      VLOG(2) << "Install data index not found: " << install_data_index;
      return;
    }
    install_data = data_iter->text;
  }
}

bool IsArchitectureSupported(const std::string& arch,
                             const std::string& current_architecture) {
  if (arch.empty()) {
    return true;
  }

  // This code accounts for Omaha 3 Offline manifests having `arch` as "x64",
  // but `GetArchitecture` returning "x86_64" for amd64.
  if (arch == current_architecture ||
      (arch == kArchAmd64Omaha3 &&
       current_architecture == update_client::kArchAmd64)) {
    return true;
  }

  using IsWow64GuestMachineSupportedFunc = HRESULT(WINAPI*)(USHORT, BOOL*);
  const IsWow64GuestMachineSupportedFunc is_wow64_guest_machine_supported =
      reinterpret_cast<IsWow64GuestMachineSupportedFunc>(::GetProcAddress(
          ::GetModuleHandle(L"kernel32.dll"), "IsWow64GuestMachineSupported"));

  if (is_wow64_guest_machine_supported) {
    const base::flat_map<std::string, int> kNativeArchitectureStringsToImages =
        {
            {update_client::kArchIntel, IMAGE_FILE_MACHINE_I386},
            {kArchAmd64Omaha3, IMAGE_FILE_MACHINE_AMD64},
            {update_client::kArchAmd64, IMAGE_FILE_MACHINE_AMD64},
            {update_client::kArchArm64, IMAGE_FILE_MACHINE_ARM64},
        };

    const auto image = kNativeArchitectureStringsToImages.find(arch);
    if (image != kNativeArchitectureStringsToImages.end()) {
      BOOL is_machine_supported = false;
      if (SUCCEEDED(is_wow64_guest_machine_supported(
              static_cast<USHORT>(image->second), &is_machine_supported))) {
        return is_machine_supported;
      }
    }
  }

  return arch == update_client::kArchIntel;
}

bool IsPlatformCompatible(const std::string& platform) {
  return platform.empty() || base::ToLowerASCII(platform) == "win";
}

bool IsArchitectureCompatible(const std::string& arch_list,
                              const std::string& current_architecture) {
  std::vector<std::string> architectures = base::SplitString(
      arch_list, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  if (architectures.empty()) {
    return true;
  }

  std::ranges::sort(architectures);

  if (std::ranges::find_if(
          architectures, [&current_architecture](const std::string& narch) {
            if (narch[0] != '-') {
              return false;
            }

            const std::string arch = narch.substr(1);

            // This code accounts for Omaha 3 Offline manifests having `arch` as
            // "x64", but `GetArchitecture` returning "x86_64" for amd64.
            return (arch == current_architecture ||
                    (arch == kArchAmd64Omaha3 &&
                     current_architecture == update_client::kArchAmd64));
          }) != architectures.end()) {
    return false;
  }
  std::erase_if(architectures,
                [](const std::string& arch) { return arch[0] == '-'; });
  return architectures.empty() ||
         std::ranges::find_if(
             architectures, [&current_architecture](const std::string& arch) {
               return IsArchitectureSupported(arch, current_architecture);
             }) != architectures.end();
}

bool IsOSVersionCompatible(const std::string& min_os_version) {
  if (min_os_version.empty()) {
    return true;
  }

  // `base::win::OSInfo` gets the `major`, `minor` and `build` from
  // `::GetVersionEx`, and the `patch` from the `UBR` value under the registry
  // path `HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion`.
  const base::win::OSInfo::VersionNumber& version_number =
      base::win::OSInfo::GetInstance()->version_number();

  const base::Version current_version(
      {version_number.major, version_number.minor, version_number.build,
       version_number.patch});
  const base::Version other_version(min_os_version);
  return current_version.IsValid() && other_version.IsValid() &&
         (current_version.CompareTo(other_version) >= 0);
}

bool IsOsSupported(
    const OfflineManifestSystemRequirements& system_requirements) {
  return IsPlatformCompatible(system_requirements.platform) &&
         IsArchitectureCompatible(system_requirements.arch,
                                  update_client::GetArchitecture()) &&
         IsOSVersionCompatible(system_requirements.min_os_version);
}

}  // namespace updater
