// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TASK_MANAGER_PROVIDERS_TASK_PROVIDER_OBSERVER_H_
#define CHROME_BROWSER_TASK_MANAGER_PROVIDERS_TASK_PROVIDER_OBSERVER_H_

#include "chrome/browser/task_manager/providers/task.h"
#include "chrome/browser/task_manager/task_manager_observer.h"

namespace task_manager {

// Defines an interface for observing tasks addition / removal.
class TaskProviderObserver {
 public:
  TaskProviderObserver() = default;
  TaskProviderObserver(const TaskProviderObserver&) = delete;
  TaskProviderObserver& operator=(const TaskProviderObserver&) = delete;
  virtual ~TaskProviderObserver() = default;

  // This notifies of the event that a new |task| has been created.
  virtual void TaskAdded(Task* task) = 0;

  // This notifies of the event that a |task| is about to be removed. The task
  // is still valid during this call, after that it may never be used again by
  // the observer and references to it must not be kept.
  virtual void TaskRemoved(Task* task) = 0;

  // This notifies of the event that |task| has become unresponsive. This event
  // is only for tasks representing renderer processes.
  virtual void TaskUnresponsive(Task* task) {}

#if BUILDFLAG(IS_CHROMEOS)
  // This notifies of the event that the TaskIdsList maintained by
  // GetTaskIdsList() should be invalidated.
  virtual void TaskIdsListToBeInvalidated() {}
#endif
};

}  // namespace task_manager

#endif  // CHROME_BROWSER_TASK_MANAGER_PROVIDERS_TASK_PROVIDER_OBSERVER_H_
