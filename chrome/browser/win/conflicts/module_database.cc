// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/win/conflicts/module_database.h"

#include <tuple>
#include <utility>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/lazy_thread_pool_task_runner.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/win/conflicts/module_database_observer.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"

namespace {

ModuleDatabase* g_module_database = nullptr;

}  // namespace

// static
constexpr base::TimeDelta ModuleDatabase::kIdleTimeout;

ModuleDatabase::ModuleDatabase()
    : idle_timer_(FROM_HERE,
                  kIdleTimeout,
                  base::BindRepeating(&ModuleDatabase::OnDelayExpired,
                                      base::Unretained(this))),
      has_started_processing_(false),
      shell_extensions_enumerated_(false),
      ime_enumerated_(false),
      // ModuleDatabase owns |module_inspector_|, so it is safe to use
      // base::Unretained().
      module_inspector_(base::BindRepeating(&ModuleDatabase::OnModuleInspected,
                                            base::Unretained(this))) {
  AddObserver(&module_inspector_);
  AddObserver(&third_party_metrics_);
}

ModuleDatabase::~ModuleDatabase() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (this == g_module_database)
    g_module_database = nullptr;
}

// static
scoped_refptr<base::SequencedTaskRunner> ModuleDatabase::GetTaskRunner() {
  static base::LazyThreadPoolSequencedTaskRunner g_module_database_task_runner =
      LAZY_THREAD_POOL_SEQUENCED_TASK_RUNNER_INITIALIZER(
          base::TaskTraits(base::TaskPriority::BEST_EFFORT,
                           base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN));
  return g_module_database_task_runner.Get();
}

// static
ModuleDatabase* ModuleDatabase::GetInstance() {
  DCHECK(GetTaskRunner()->RunsTasksInCurrentSequence());
  return g_module_database;
}

// static
void ModuleDatabase::SetInstance(
    std::unique_ptr<ModuleDatabase> module_database) {
  DCHECK_EQ(nullptr, g_module_database);
  // This is deliberately leaked. It can be cleaned up by manually deleting the
  // ModuleDatabase.
  g_module_database = module_database.release();
}

bool ModuleDatabase::IsIdle() {
  return has_started_processing_ && RegisteredModulesEnumerated() &&
         !idle_timer_.IsRunning() && module_inspector_.IsIdle();
}

void ModuleDatabase::OnShellExtensionEnumerated(const base::FilePath& path,
                                                uint32_t size_of_image,
                                                uint32_t time_date_stamp) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  idle_timer_.Reset();

  ModuleInfo* module_info = nullptr;
  FindOrCreateModuleInfo(path, size_of_image, time_date_stamp, &module_info);
  module_info->second.module_properties |=
      ModuleInfoData::kPropertyShellExtension;
}

void ModuleDatabase::OnShellExtensionEnumerationFinished() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!shell_extensions_enumerated_);

  shell_extensions_enumerated_ = true;

  if (RegisteredModulesEnumerated())
    OnRegisteredModulesEnumerated();
}

void ModuleDatabase::OnImeEnumerated(const base::FilePath& path,
                                     uint32_t size_of_image,
                                     uint32_t time_date_stamp) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  idle_timer_.Reset();

  ModuleInfo* module_info = nullptr;
  FindOrCreateModuleInfo(path, size_of_image, time_date_stamp, &module_info);
  module_info->second.module_properties |= ModuleInfoData::kPropertyIme;
}

void ModuleDatabase::OnImeEnumerationFinished() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!ime_enumerated_);

  ime_enumerated_ = true;

  if (RegisteredModulesEnumerated())
    OnRegisteredModulesEnumerated();
}

void ModuleDatabase::OnModuleLoad(content::ProcessType process_type,
                                  const base::FilePath& module_path,
                                  uint32_t module_size,
                                  uint32_t module_time_date_stamp) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  DCHECK(process_type == content::PROCESS_TYPE_BROWSER ||
         process_type == content::PROCESS_TYPE_RENDERER)
      << "The current logic in ModuleBlocklistCacheUpdater does not support "
         "other process types yet. See https://crbug.com/662084 for details.";

  ModuleInfo* module_info = nullptr;
  bool new_module = FindOrCreateModuleInfo(
      module_path, module_size, module_time_date_stamp, &module_info);

  uint32_t old_module_properties = module_info->second.module_properties;

  // Mark the module as loaded.
  module_info->second.module_properties |=
      ModuleInfoData::kPropertyLoadedModule;

  // Update the list of process types that this module has been seen in.
  module_info->second.process_types |= ProcessTypeToBit(process_type);

  // Some observers care about a known module that is just now loading. Also
  // making sure that the module is ready to be sent to observers.
  bool is_known_module_loading =
      !new_module &&
      old_module_properties != module_info->second.module_properties;
  bool ready_for_notification =
      module_info->second.inspection_result && RegisteredModulesEnumerated();
  if (is_known_module_loading && ready_for_notification) {
    for (auto& observer : observer_list_) {
      observer.OnKnownModuleLoaded(module_info->first, module_info->second);
    }
  }
}

// static
void ModuleDatabase::HandleModuleLoadEvent(content::ProcessType process_type,
                                           const base::FilePath& module_path,
                                           uint32_t module_size,
                                           uint32_t module_time_date_stamp) {
  GetTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](content::ProcessType process_type,
             const base::FilePath& module_path, uint32_t module_size,
             uint32_t module_time_date_stamp) {
            ModuleDatabase::GetInstance()->OnModuleLoad(
                process_type, module_path, module_size, module_time_date_stamp);
          },
          process_type, module_path, module_size, module_time_date_stamp));
}

void ModuleDatabase::OnModuleBlocked(const base::FilePath& module_path,
                                     uint32_t module_size,
                                     uint32_t module_time_date_stamp) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ModuleInfo* module_info = nullptr;
  FindOrCreateModuleInfo(module_path, module_size, module_time_date_stamp,
                         &module_info);

  module_info->second.module_properties |= ModuleInfoData::kPropertyBlocked;
}

void ModuleDatabase::OnModuleAddedToBlocklist(const base::FilePath& module_path,
                                              uint32_t module_size,
                                              uint32_t module_time_date_stamp) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto iter = modules_.find(
      ModuleInfoKey(module_path, module_size, module_time_date_stamp));

  // Only known modules should be added to the blocklist.
  CHECK(iter != modules_.end());

  iter->second.module_properties |= ModuleInfoData::kPropertyAddedToBlocklist;
}

void ModuleDatabase::AddObserver(ModuleDatabaseObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  observer_list_.AddObserver(observer);

  // If the registered modules enumeration is not finished yet, the |observer|
  // will be notified later in OnRegisteredModulesEnumerated().
  if (!RegisteredModulesEnumerated())
    return;

  NotifyLoadedModules(observer);

  if (IsIdle())
    observer->OnModuleDatabaseIdle();
}

void ModuleDatabase::RemoveObserver(ModuleDatabaseObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  observer_list_.RemoveObserver(observer);
}

void ModuleDatabase::StartInspection() {
  module_inspector_.StartInspection();
}

bool ModuleDatabase::FindOrCreateModuleInfo(
    const base::FilePath& module_path,
    uint32_t module_size,
    uint32_t module_time_date_stamp,
    ModuleDatabase::ModuleInfo** module_info) {
  auto result = modules_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(module_path, module_size, module_time_date_stamp),
      std::forward_as_tuple());

  // New modules must be inspected.
  bool new_module = result.second;
  if (new_module) {
    has_started_processing_ = true;
    idle_timer_.Reset();

    module_inspector_.AddModule(result.first->first);
  }

  *module_info = &(*result.first);
  return new_module;
}

bool ModuleDatabase::RegisteredModulesEnumerated() {
  return shell_extensions_enumerated_ && ime_enumerated_;
}

void ModuleDatabase::OnRegisteredModulesEnumerated() {
  for (auto& observer : observer_list_)
    NotifyLoadedModules(&observer);

  if (IsIdle())
    EnterIdleState();
}

void ModuleDatabase::OnModuleInspected(
    const ModuleInfoKey& module_key,
    ModuleInspectionResult inspection_result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = modules_.find(module_key);
  if (it == modules_.end())
    return;

  it->second.inspection_result = std::move(inspection_result);

  if (RegisteredModulesEnumerated()) {
    for (auto& observer : observer_list_) {
      observer.OnNewModuleFound(it->first, it->second);
    }
  }

  // Notify the observers if this was the last outstanding module inspection and
  // the delay has already expired.
  if (IsIdle())
    EnterIdleState();
}

void ModuleDatabase::OnDelayExpired() {
  // Notify the observers if there are no outstanding module inspections.
  if (IsIdle())
    EnterIdleState();
}

void ModuleDatabase::EnterIdleState() {
  for (auto& observer : observer_list_)
    observer.OnModuleDatabaseIdle();
}

void ModuleDatabase::NotifyLoadedModules(ModuleDatabaseObserver* observer) {
  for (const auto& module : modules_) {
    if (module.second.inspection_result)
      observer->OnNewModuleFound(module.first, module.second);
  }
}
