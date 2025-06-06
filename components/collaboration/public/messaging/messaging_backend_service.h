// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_COLLABORATION_PUBLIC_MESSAGING_MESSAGING_BACKEND_SERVICE_H_
#define COMPONENTS_COLLABORATION_PUBLIC_MESSAGING_MESSAGING_BACKEND_SERVICE_H_

#include <set>

#include "base/functional/callback_forward.h"
#include "base/observer_list_types.h"
#include "base/scoped_observation_traits.h"
#include "base/supports_user_data.h"
#include "base/uuid.h"
#include "components/collaboration/public/messaging/activity_log.h"
#include "components/collaboration/public/messaging/message.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/saved_tab_groups/public/types.h"

namespace collaboration::messaging {

class MessagingBackendService : public KeyedService,
                                public base::SupportsUserData {
 public:
  class PersistentMessageObserver : public base::CheckedObserver {
   public:
    // Invoked once when the service is initialized. This is invoked only once
    // and is immediately invoked (re-entrant) if the service was initialized
    // before the observer was added. You can invoke `IsInitialized()` if you
    // want to know the state and whether this is going to happen or not.
    virtual void OnMessagingBackendServiceInitialized() = 0;

    // Invoked when the frontend needs to display a specific persistent message.
    virtual void DisplayPersistentMessage(PersistentMessage message) = 0;

    // Invoked when the frontend needs to hide a specific persistent message.
    virtual void HidePersistentMessage(PersistentMessage message) = 0;
  };

  // A delegate for showing instant (one-off) messages for the current platform.
  // This needs to be provided to the `MessagingBackendService` through
  // `MessagingBackendService::SetInstantMessageDelegate(...)`.
  class InstantMessageDelegate : public base::CheckedObserver {
   public:
    // Callback for informing the backend service whether a message could be
    // displayed.
    using SuccessCallback = base::OnceCallback<void(bool)>;

    // Invoked when the frontend needs to display an instant message.
    // When a decision has been made whether it can be displayed or not, invoke
    // `success_callback` with `true` if it was displayed, and `false`
    // otherwise. This enables the backend to either:
    // *   Success: Clear the message from internal storage.
    // *   Failure: Prepare the message to be redelivered at a later time.
    virtual void DisplayInstantaneousMessage(
        InstantMessage message,
        SuccessCallback success_callback) = 0;

    // Invoked when the frontend should hide instant messages.  This is intended
    // to be a no-op if the message is not currently displayed or not in the
    // queue to be displayed. The provided message IDs are the IDs of the
    // messages that should be hidden, and they are the same IDs as the
    // `InstantMessage::attributions[].id` values from the `InstantMessage`
    // argument originally passed to `DisplayInstantaneousMessage(..)`.
    virtual void HideInstantaneousMessage(
        const std::set<base::Uuid>& message_ids) = 0;
  };

  ~MessagingBackendService() override = default;

  // Sets the delegate for instant (one-off) messages. This must outlive this
  // class.
  virtual void SetInstantMessageDelegate(
      InstantMessageDelegate* instant_message_delegate) = 0;

  // Methods for controlling observation of persistent messages.
  virtual void AddPersistentMessageObserver(
      PersistentMessageObserver* observer) = 0;
  virtual void RemovePersistentMessageObserver(
      PersistentMessageObserver* observer) = 0;

  virtual bool IsInitialized() = 0;

  // Queries for all currently displaying persistent messages.
  // Will return an empty result if the service has not been initialized.
  // Use IsInitialized() to check initialization state, or listen for broadcasts
  // of PersistentMessageObserver::OnMessagingBackendServiceInitialized().
  virtual std::vector<PersistentMessage> GetMessagesForTab(
      tab_groups::EitherTabID tab_id,
      std::optional<PersistentNotificationType> type) = 0;
  virtual std::vector<PersistentMessage> GetMessagesForGroup(
      tab_groups::EitherGroupID group_id,
      std::optional<PersistentNotificationType> type) = 0;
  virtual std::vector<PersistentMessage> GetMessages(
      std::optional<PersistentNotificationType> type) = 0;

  // Central method to query the list of rows to be shown in the activity log
  // UI. Will return an empty list if the service has not been initialized.
  virtual std::vector<ActivityLogItem> GetActivityLog(
      const ActivityLogQueryParams& params) = 0;

  // Invoked to clear all dirty messages for a tab group. Meant to be invoked
  // from the activity card which when dismissed clears out all the individual
  // tab messages. Doesn't apply to instant messages.
  virtual void ClearDirtyTabMessagesForGroup(
      const data_sharing::GroupId& collaboration_group_id) = 0;

  // Invoked to clear a given persistent message. This will clear the specified
  // dirty bit on the message entry of the database. If std::nullopt is passed,
  // all dirty bits of that message will be cleared.
  virtual void ClearPersistentMessage(
      const base::Uuid& message_id,
      std::optional<PersistentNotificationType> type) = 0;

  // Deprecated. Do not use. Use ClearPersistentMessage instead.
  // Invoked to remove a list of given messages from the backend storage.
  virtual void RemoveMessages(const std::vector<base::Uuid>& message_ids) = 0;

  // Testing-only API for setting activity log.
  virtual void AddActivityLogForTesting(
      data_sharing::GroupId collaboration_id,
      const std::vector<ActivityLogItem>& activity_log) = 0;
};

}  // namespace collaboration::messaging

namespace base {
template <>
struct ScopedObservationTraits<
    collaboration::messaging::MessagingBackendService,
    collaboration::messaging::MessagingBackendService::
        PersistentMessageObserver> {
  static void AddObserver(
      collaboration::messaging::MessagingBackendService* source,
      collaboration::messaging::MessagingBackendService::
          PersistentMessageObserver* observer) {
    source->AddPersistentMessageObserver(observer);
  }
  static void RemoveObserver(
      collaboration::messaging::MessagingBackendService* source,
      collaboration::messaging::MessagingBackendService::
          PersistentMessageObserver* observer) {
    source->RemovePersistentMessageObserver(observer);
  }
};
}  // namespace base

#endif  // COMPONENTS_COLLABORATION_PUBLIC_MESSAGING_MESSAGING_BACKEND_SERVICE_H_
