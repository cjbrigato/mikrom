// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WIN_CONFLICTS_MODULE_DATABASE_H_
#define CHROME_BROWSER_WIN_CONFLICTS_MODULE_DATABASE_H_

#include <map>
#include <memory>

#include "base/memory/scoped_refptr.h"
#include "base/observer_list.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/win/conflicts/module_info.h"
#include "chrome/browser/win/conflicts/module_inspector.h"
#include "chrome/browser/win/conflicts/third_party_metrics_recorder.h"
#include "content/public/common/process_type.h"

class ModuleDatabaseObserver;

namespace base {
class FilePath;
class SequencedTaskRunner;
}  // namespace base

// A class that keeps track of all modules loaded across Chrome processes.
//
// It is also the main class behind third-party modules tracking, and owns the
// different classes that required to identify incompatible applications and
// record metrics.
//
// This is effectively a singleton, but doesn't use base::Singleton. The intent
// is for the object to be created when Chrome is single-threaded, and for it
// be set as the process-wide singleton via SetInstance.
class ModuleDatabase : public ModuleDatabaseEventSource {
 public:
  // Structures for maintaining information about modules.
  using ModuleMap = std::map<ModuleInfoKey, ModuleInfoData>;
  using ModuleInfo = ModuleMap::value_type;

  // The Module Database becomes idle after this timeout expires without any
  // module events.
  static constexpr base::TimeDelta kIdleTimeout = base::Seconds(10);

  // Creates the ModuleDatabase. Must be created and set on the sequence
  // returned by GetTaskRunner().
  ModuleDatabase();

  ModuleDatabase(const ModuleDatabase&) = delete;
  ModuleDatabase& operator=(const ModuleDatabase&) = delete;

  ~ModuleDatabase() override;

  // Returns the SequencedTaskRunner on which the ModuleDatabase lives. Can be
  // called on any thread.
  static scoped_refptr<base::SequencedTaskRunner> GetTaskRunner();

  // Retrieves the singleton global instance of the ModuleDatabase. Must only be
  // called on the sequence returned by GetTaskRunner().
  static ModuleDatabase* GetInstance();

  // Sets the global instance of the ModuleDatabase. Ownership is passed to the
  // global instance and deliberately leaked, unless manually cleaned up. This
  // has no locking and should be called when Chrome is single threaded.
  static void SetInstance(std::unique_ptr<ModuleDatabase> module_database);

  // Returns true if the ModuleDatabase is idle. This means that no modules are
  // currently being inspected, and no new module events have been observed in
  // the last 10 seconds.
  bool IsIdle();

  // Indicates that a new registered shell extension was found.
  void OnShellExtensionEnumerated(const base::FilePath& path,
                                  uint32_t size_of_image,
                                  uint32_t time_date_stamp);

  // Indicates that all shell extensions have been enumerated.
  void OnShellExtensionEnumerationFinished();

  // Indicates that a new registered input method editor was found.
  void OnImeEnumerated(const base::FilePath& path,
                       uint32_t size_of_image,
                       uint32_t time_date_stamp);

  // Indicates that all input method editors have been enumerated.
  void OnImeEnumerationFinished();

  // Indicates that a module has been loaded. The data passed to this function
  // is taken as gospel, so if it originates from a remote process it should be
  // independently validated first. (In practice, see ModuleEventSinkImpl for
  // details of where this happens.)
  void OnModuleLoad(content::ProcessType process_type,
                    const base::FilePath& module_path,
                    uint32_t module_size,
                    uint32_t module_time_date_stamp);

  // Forwards the module load event to the ModuleDatabase global instance via
  // OnModuleLoad() on the ModuleDatabase task runner. Can be called on any
  // threads. Provided for convenience.
  static void HandleModuleLoadEvent(content::ProcessType process_type,
                                    const base::FilePath& module_path,
                                    uint32_t module_size,
                                    uint32_t module_time_date_stamp);

  void OnModuleBlocked(const base::FilePath& module_path,
                       uint32_t module_size,
                       uint32_t module_time_date_stamp);

  // Marks the module as added to the module blocklist cache, which means it
  // will be blocked on the next browser launch.
  void OnModuleAddedToBlocklist(const base::FilePath& module_path,
                                uint32_t module_size,
                                uint32_t module_time_date_stamp);

  // TODO(chrisha): Module analysis code, and various accessors for use by
  // chrome://conflicts.

  // Adds or removes an observer.
  // Note that when adding an observer, OnNewModuleFound() will immediately be
  // called once for all modules that are already loaded before returning to the
  // caller. In addition, if the ModuleDatabase is currently idle,
  // OnModuleDatabaseIdle() will also be invoked.
  //
  // ModuleDatabaseEventSource:
  void AddObserver(ModuleDatabaseObserver* observer) override;
  void RemoveObserver(ModuleDatabaseObserver* observer) override;

  void StartInspection();

 private:
  friend class TestModuleDatabase;
  friend class ModuleDatabaseTest;
  friend class ModuleEventSinkImplTest;

  ModuleInfo* CreateModuleInfo(const base::FilePath& module_path,
                               uint32_t module_size,
                               uint32_t module_time_date_stamp);

  // Finds or creates a mutable ModuleInfo entry. Returns true if the module
  // info was created.
  bool FindOrCreateModuleInfo(const base::FilePath& module_path,
                              uint32_t module_size,
                              uint32_t module_time_date_stamp,
                              ModuleInfo** module_info);

  // Returns true if the enumeration of the IMEs and the shell extensions is
  // finished.
  //
  // To avoid sending an improperly tagged module to an observer (in case a race
  // condition happens and the module is loaded before the enumeration is done),
  // it's important that this function returns true before any calls to
  // OnNewModuleFound() is made.
  bool RegisteredModulesEnumerated();

  // Called when RegisteredModulesEnumerated() becomes true. Notifies the
  // observers of each already inspected modules and checks if the idle state
  // should be entered.
  void OnRegisteredModulesEnumerated();

  // Callback for ModuleInspector.
  void OnModuleInspected(const ModuleInfoKey& module_key,
                         ModuleInspectionResult inspection_result);

  // If the ModuleDatabase is truly idle, calls EnterIdleState().
  void OnDelayExpired();

  // Notifies the observers that ModuleDatabase is now idle.
  void EnterIdleState();

  // Notifies the |observer| of already found and inspected modules via
  // OnNewModuleFound().
  void NotifyLoadedModules(ModuleDatabaseObserver* observer);

  // A map of all known modules.
  ModuleMap modules_;

  base::RetainingOneShotTimer idle_timer_;

  // Indicates if the ModuleDatabase has started processing module load events.
  bool has_started_processing_;

  // Indicates if all shell extensions have been enumerated.
  bool shell_extensions_enumerated_;

  // Indicates if all input method editors have been enumerated.
  bool ime_enumerated_;

  // Inspects new modules on a blocking task runner.
  ModuleInspector module_inspector_;

  // Holds observers.
  base::ObserverList<ModuleDatabaseObserver>::Unchecked observer_list_;

  // Records metrics on third-party modules.
  ThirdPartyMetricsRecorder third_party_metrics_;

  SEQUENCE_CHECKER(sequence_checker_);
};

#endif  // CHROME_BROWSER_WIN_CONFLICTS_MODULE_DATABASE_H_
