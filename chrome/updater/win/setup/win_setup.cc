// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/updater/win/setup/win_setup.h"

#include <shlobj.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/check.h"
#include "base/command_line.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/scoped_com_initializer.h"
#include "chrome/installer/util/work_item_list.h"
#include "chrome/updater/constants.h"
#include "chrome/updater/updater_branding.h"
#include "chrome/updater/updater_scope.h"
#include "chrome/updater/updater_version.h"
#include "chrome/updater/util/util.h"
#include "chrome/updater/util/win_util.h"
#include "chrome/updater/win/setup/setup_util.h"
#include "chrome/updater/win/win_constants.h"

namespace updater {
namespace {

// Returns a list of base file names which the setup copies to the install
// directory. The source of these files is either the unpacked metainstaller
// archive, or the `out` directory of the build, if a command line argument is
// present. In the former case, which is the normal execution flow, the files
// are enumerated from the directory where the metainstaller unpacked its
// contents. In the latter case, the file containing the run time dependencies
// of the updater (which is generated by GN at build time) is parsed, and the
// relevant file names are extracted.
std::vector<base::FilePath> GetSetupFiles(const base::FilePath& source_dir) {
  std::vector<base::FilePath> result;
  base::FileEnumerator it(
      source_dir, false, base::FileEnumerator::FileType::FILES,
      FILE_PATH_LITERAL("*"), base::FileEnumerator::FolderSearchPolicy::ALL,
      base::FileEnumerator::ErrorPolicy::STOP_ENUMERATION);
  it.ForEach([&result](const base::FilePath& file) {
    result.push_back(file.BaseName());
  });
  if (it.GetError() != base::File::Error::FILE_OK) {
    VLOG(2) << __func__ << " could not enumerate files : " << it.GetError();
    return {};
  }
  return result;
}

}  // namespace

int Setup(UpdaterScope scope) {
  VLOG(1) << __func__ << ", scope: " << scope;
  CHECK(!IsSystemInstall(scope) || ::IsUserAnAdmin());
  auto scoped_com_initializer =
      std::make_unique<base::win::ScopedCOMInitializer>(
          base::win::ScopedCOMInitializer::kMTA);

  const std::optional<base::FilePath> versioned_dir =
      GetVersionedInstallDirectory(scope);
  if (!versioned_dir) {
    LOG(ERROR) << "GetVersionedInstallDirectory failed.";
    return kErrorNoVersionedDirectory;
  }

  // Stop any processes that may be running under the versioned path before
  // installation.
  StopProcessesUnderPath(*versioned_dir, base::Seconds(15));

  base::FilePath exe_path;
  if (!base::PathService::Get(base::FILE_EXE, &exe_path)) {
    LOG(ERROR) << "PathService failed.";
    return kErrorPathServiceFailed;
  }

  const auto source_dir = exe_path.DirName();
  const auto setup_files = GetSetupFiles(source_dir);
  if (setup_files.empty()) {
    LOG(ERROR) << "No files to set up.";
    return kErrorFailedToGetSetupFiles;
  }

  std::optional<base::ScopedTempDir> temp_dir = CreateSecureTempDir();
  if (!temp_dir) {
    LOG(ERROR) << "CreateSecureTempDir failed.";
    return kErrorCreatingTempDir;
  }

  // All source files are installed in a flat directory structure inside the
  // versioned directory, hence the BaseName function call below.
  std::unique_ptr<WorkItemList> install_list(WorkItem::CreateWorkItemList());
  for (const auto& file : setup_files) {
    const base::FilePath target_path = versioned_dir->Append(file.BaseName());
    const base::FilePath source_path = source_dir.Append(file);
    install_list->AddCopyTreeWorkItem(source_path, target_path,
                                      temp_dir->GetPath(), WorkItem::ALWAYS);
  }

  const HKEY key = UpdaterScopeToHKeyRoot(scope);
  for (const auto& key_path :
       {GetRegistryKeyClientsUpdater(), GetRegistryKeyClientStateUpdater()}) {
    install_list->AddCreateRegKeyWorkItem(key, key_path, KEY_WOW64_32KEY);
    install_list->AddSetRegValueWorkItem(key, key_path, KEY_WOW64_32KEY,
                                         kRegValuePV, kUpdaterVersionUtf16,
                                         true);
    install_list->AddSetRegValueWorkItem(
        key, key_path, KEY_WOW64_32KEY, kRegValueName,
        base::UTF8ToWide(PRODUCT_FULLNAME_STRING), true);
  }

  const base::FilePath updater_exe = GetExecutableRelativePath();

  switch (scope) {
    case UpdaterScope::kUser:
      AddComServerWorkItems(versioned_dir->Append(updater_exe), true,
                            install_list.get());
      break;
    case UpdaterScope::kSystem:
      AddComServiceWorkItems(versioned_dir->Append(updater_exe), true,
                             install_list.get());
      break;
  }

  base::CommandLine run_updater_wake_command(
      versioned_dir->Append(updater_exe));
  run_updater_wake_command.AppendSwitch(kWakeSwitch);
  if (IsSystemInstall(scope)) {
    run_updater_wake_command.AppendSwitch(kSystemSwitch);
  }

  if (!IsSystemInstall(scope)) {
    RegisterUserRunAtStartup(GetTaskNamePrefix(scope), run_updater_wake_command,
                             install_list.get());
  }

  WorkItem* register_wake_task =
      new RegisterWakeTaskWorkItem(run_updater_wake_command, scope);

  // Updater installs even if the task scheduler wake task creation fails.
  register_wake_task->set_best_effort(true);
  install_list->AddWorkItem(register_wake_task);

  if (!install_list->Do()) {
    LOG(ERROR) << "Install failed, rolling back...";
    install_list->Rollback();
    LOG(ERROR) << "Rollback complete.";
    return kErrorFailedToRunInstallList;
  }

  VLOG(1) << "Setup succeeded.";

  return kErrorOk;
}

}  // namespace updater
