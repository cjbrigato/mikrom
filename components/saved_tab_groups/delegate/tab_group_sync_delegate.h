// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SAVED_TAB_GROUPS_DELEGATE_TAB_GROUP_SYNC_DELEGATE_H_
#define COMPONENTS_SAVED_TAB_GROUPS_DELEGATE_TAB_GROUP_SYNC_DELEGATE_H_

#include <memory>
#include <set>

#include "base/uuid.h"
#include "components/saved_tab_groups/public/saved_tab_group.h"
#include "components/saved_tab_groups/public/tab_group_sync_service.h"
#include "components/saved_tab_groups/public/types.h"

namespace tab_groups {

// Home for platform specific logic for tab group sync. Contains helper methods
// for applying incoming remote updates to the local tab model, which platforms
// need to implement. Ideally should own a local mutation helper that will apply
// the mutation. Also should own the local tab model observer which will observe
// the local tab model and invoke TabGroupSyncService directly or via a remote
// mutation helper that will propagate the changes to sync.
class TabGroupSyncDelegate {
 public:
  // A RAII class that indicates to the TabGroupSyncDelegate that several
  // operations will happen before it is necessary to reflect all the updates.
  class [[maybe_unused, nodiscard]] ScopedBatchOperation {
   public:
    virtual ~ScopedBatchOperation() = default;
  };

  virtual ~TabGroupSyncDelegate() = default;

  // Notify the delegate that several operations are about to happen. The
  // returned token should be kept alive until the operations complete, at which
  // point it must be deleted.
  virtual std::unique_ptr<ScopedBatchOperation> StartBatchOperation();

  // Called to open a given saved tab group in the local tab model.
  // The `context` can be used to specify the browser window in which the tab
  // group should be opened.
  virtual std::optional<LocalTabGroupID> HandleOpenTabGroupRequest(
      const base::Uuid& sync_tab_group_id,
      std::unique_ptr<TabGroupActionContext> context) = 0;

  // Called to pause / resume the local observer.
  virtual std::unique_ptr<ScopedLocalObservationPauser>
  CreateScopedLocalObserverPauser() = 0;

  // Called to create a local tab group for the given sync representation.
  virtual void CreateLocalTabGroup(const SavedTabGroup& tab_group) = 0;

  // Called to close the specified local tab group.
  virtual void CloseLocalTabGroup(const LocalTabGroupID& local_id) = 0;

  // Called to start listening for changes to a local group. The local group
  // will be updated based on the connected saved group. Desktop only.
  virtual void ConnectLocalTabGroup(const SavedTabGroup& group) = 0;

  // Called to stop listening for changes to a local group. Desktop only.
  virtual void DisconnectLocalTabGroup(const LocalTabGroupID& local_id) = 0;

  // Called to update local tab group to match the given saved tab group. This
  // will open new tabs, close tabs, and navigate tabs to match the saved group.
  // Connects to the local tab group if it is not already connected and begins
  // listening for changes to the local group.
  virtual void UpdateLocalTabGroup(const SavedTabGroup& group) = 0;

  // Called to get all the local tab group IDs across all local tab models.
  virtual std::vector<LocalTabGroupID> GetLocalTabGroupIds() = 0;

  // Called to get the local tab IDs associated with a given tab group.
  virtual std::vector<LocalTabID> GetLocalTabIdsForTabGroup(
      const LocalTabGroupID& local_tab_group_id) = 0;

  // Called to get the currently selected tabs from the tab model. The result
  // should contain selected tabs across all browser windows.
  virtual std::set<LocalTabID> GetSelectedTabs() = 0;

  // Called to get the title of a tab from the tab model.
  virtual std::u16string GetTabTitle(const LocalTabID& local_tab_id) = 0;

  // Local To Remote mutation methods.

  // Helper function to create a SavedTabGroup for the given local tab group ID.
  // Caller is supposed to handle the SavedTabGroup, e.g. add to the sync
  // service.
  virtual std::unique_ptr<SavedTabGroup> CreateSavedTabGroupFromLocalGroup(
      const LocalTabGroupID& local_tab_group_id) = 0;
};

}  // namespace tab_groups

#endif  // COMPONENTS_SAVED_TAB_GROUPS_DELEGATE_TAB_GROUP_SYNC_DELEGATE_H_
