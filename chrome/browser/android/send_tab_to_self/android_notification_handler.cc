// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/send_tab_to_self/android_notification_handler.h"

#include <string>
#include <vector>

#include "base/android/jni_string.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/android/android_theme_resources.h"
#include "chrome/browser/android/resource_mapper.h"
#include "chrome/grit/generated_resources.h"
#include "components/messages/android/message_dispatcher_bridge.h"
#include "components/messages/android/message_enums.h"
#include "components/messages/android/message_wrapper.h"
#include "components/send_tab_to_self/metrics_util.h"
#include "components/send_tab_to_self/send_tab_to_self_entry.h"
#include "components/send_tab_to_self/send_tab_to_self_model.h"
#include "components/url_formatter/elide_url.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "chrome/android/chrome_jni_headers/NotificationManager_jni.h"
#include "chrome/android/chrome_jni_headers/SendTabToSelfNotificationReceiver_jni.h"

using base::android::AttachCurrentThread;
using base::android::ConvertUTF8ToJavaString;
using base::android::ScopedJavaLocalRef;

namespace send_tab_to_self {

namespace {

void LogDismissReason(messages::DismissReason dismiss_reason) {
  switch (dismiss_reason) {
    case messages::DismissReason::PRIMARY_ACTION:
      RecordNotificationOpened();
      break;
    case messages::DismissReason::TIMER:
      RecordNotificationTimedOut();
      break;
    case messages::DismissReason::GESTURE:
      RecordNotificationDismissed();
      break;
    case messages::DismissReason::UNKNOWN:
      RecordNotificationDismissReasonUnknown();
      break;
    default:
      RecordNotificationDismissed();
      break;
  }
}

}  // namespace

AndroidNotificationHandler::AndroidNotificationHandler(
    SendTabToSelfModel* send_tab_to_self_model)
    : send_tab_to_self_model_((send_tab_to_self_model)) {}

AndroidNotificationHandler::~AndroidNotificationHandler() {
  while (!queued_messages_.empty()) {
    messages::MessageDispatcherBridge::Get()->DismissMessage(
        queued_messages_.at(0).get(), messages::DismissReason::UNKNOWN);
  }
  DCHECK(queued_messages_.size() == 0);
}

void AndroidNotificationHandler::DisplayNewEntries(
    const std::vector<const SendTabToSelfEntry*>& new_entries) {
  std::vector<SendTabToSelfEntry> vector_copy;

  for (const SendTabToSelfEntry* entry : new_entries) {
    vector_copy.push_back(*entry);
  }

  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(&AndroidNotificationHandler::DisplayNewEntriesOnUIThread,
                     weak_factory_.GetWeakPtr(), std::move(vector_copy)));
}

void AndroidNotificationHandler::DisplayNewEntriesOnUIThread(
    const std::vector<SendTabToSelfEntry>& new_entries) {
  for (const SendTabToSelfEntry& entry : new_entries) {
    JNIEnv* env = AttachCurrentThread();

    // Set the expiration to 10 days from when the notification is displayed.
    base::Time expiraton_time = entry.GetSharedTime() + base::Days(10);

    ScopedJavaLocalRef<jclass> send_tab_to_self_notification_receiver_class =
        Java_SendTabToSelfNotificationReceiver_getSendTabToSelfNotificationReciever(
            env);

    Java_NotificationManager_showNotification(
        env, ConvertUTF8ToJavaString(env, entry.GetGUID()),
        ConvertUTF8ToJavaString(env, entry.GetURL().spec()),
        ConvertUTF8ToJavaString(env, entry.GetTitle()),
        ConvertUTF8ToJavaString(env, entry.GetDeviceName()),
        expiraton_time.InMillisecondsSinceUnixEpoch(),
        send_tab_to_self_notification_receiver_class);
  }
}

void AndroidNotificationHandler::DismissEntries(
    const std::vector<std::string>& guids) {
  JNIEnv* env = AttachCurrentThread();

  for (const std::string& guid : guids) {
    Java_NotificationManager_hideNotification(
        env, ConvertUTF8ToJavaString(env, guid));
  }
}

void AndroidNotificationHandler::OnMessageOpened(GURL url, std::string guid) {
  if (!web_contents_) {
    return;
  }

  content::OpenURLParams params(url, content::Referrer(),
                                WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                ui::PAGE_TRANSITION_AUTO_TOPLEVEL, false);
  params.should_replace_current_entry = false;
  web_contents_->OpenURL(params, /*navigation_handle_callback=*/{});
  send_tab_to_self_model_->DismissEntry(guid);
}

void AndroidNotificationHandler::OnMessageDismissed(
    messages::MessageWrapper* message,
    std::string guid,
    messages::DismissReason dismiss_reason) {
  // Any reason other than UNKNOWN indicates the notification was displayed.
  if (dismiss_reason != messages::DismissReason::UNKNOWN) {
    send_tab_to_self::RecordNotificationShown();
  }
  for (unsigned int i = 0; i < queued_messages_.size(); i++) {
    if (queued_messages_.at(i).get() == message) {
      queued_messages_.erase(queued_messages_.begin() + i);
      send_tab_to_self_model_->DismissEntry(guid);
      LogDismissReason(dismiss_reason);
    }
  }
}

void AndroidNotificationHandler::UpdateWebContents(
    content::WebContents* web_contents) {
  DCHECK(web_contents);
  web_contents_ = web_contents->GetWeakPtr();
  while (!pending_messages_.empty()) {
    queued_messages_.push_back(std::move(pending_messages_.front()));
    pending_messages_.pop();
    messages::MessageDispatcherBridge::Get()->EnqueueWindowScopedMessage(
        std::move(queued_messages_.back().get()),
        web_contents_->GetTopLevelNativeWindow(),
        messages::MessagePriority::kNormal);
  }
}

}  // namespace send_tab_to_self
