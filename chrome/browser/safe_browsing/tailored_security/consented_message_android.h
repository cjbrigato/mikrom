// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SAFE_BROWSING_TAILORED_SECURITY_CONSENTED_MESSAGE_ANDROID_H_
#define CHROME_BROWSER_SAFE_BROWSING_TAILORED_SECURITY_CONSENTED_MESSAGE_ANDROID_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "components/messages/android/message_enums.h"
#include "components/messages/android/message_wrapper.h"
#include "ui/android/window_android.h"

namespace content {
class WebContents;
}  // namespace content

namespace safe_browsing {

class TailoredSecurityConsentedModalAndroid {
 public:
  // Show the message for the given `web_contents`, when the Tailored security
  // setting has been `enabled`.
  TailoredSecurityConsentedModalAndroid(content::WebContents* web_contents,
                                        bool enabled,
                                        base::OnceClosure dismiss_callback,
                                        bool is_requested_by_synced_esb);
  ~TailoredSecurityConsentedModalAndroid();

 private:
  friend class TailoredSecurityConsentedModalAndroidTest;
  FRIEND_TEST_ALL_PREFIXES(TailoredSecurityConsentedModalAndroidTest,
                           HandleMessageDismissedWithSelfDeletingCallback);
  FRIEND_TEST_ALL_PREFIXES(
      TailoredSecurityConsentedModalAndroidTest,
      HandleAccepted_EsbSynced_NullWebContents_EnabledMessage_LogsFailed);
  FRIEND_TEST_ALL_PREFIXES(
      TailoredSecurityConsentedModalAndroidTest,
      HandleAccepted_EsbSynced_NullWebContents_DisabledMessage_LogsFailed);
  void DismissMessageInternal(messages::DismissReason dismiss_reason);
  void HandleSettingsClicked();
  void HandleMessageAccepted();
  void HandleMessageDismissed(messages::DismissReason dismiss_reason);

  std::unique_ptr<messages::MessageWrapper> message_;
  raw_ptr<content::WebContents> web_contents_;
  raw_ptr<ui::WindowAndroid> window_android_ = nullptr;
  base::OnceClosure dismiss_callback_;

  // Whether the message is shown for Tailored Security being enabled or
  // disabled.
  bool is_enable_message_;

  // Whether the message is requested by ESB As A Synced Setting flow.
  bool is_requested_by_synced_esb_;
};

}  // namespace safe_browsing

#endif  // CHROME_BROWSER_SAFE_BROWSING_TAILORED_SECURITY_CONSENTED_MESSAGE_ANDROID_H_
