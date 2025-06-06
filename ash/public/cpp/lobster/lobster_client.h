// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_PUBLIC_CPP_LOBSTER_LOBSTER_CLIENT_H_
#define ASH_PUBLIC_CPP_LOBSTER_LOBSTER_CLIENT_H_

#include <optional>
#include <string>

#include "ash/public/cpp/ash_public_export.h"
#include "ash/public/cpp/lobster/lobster_result.h"
#include "ash/public/cpp/lobster/lobster_session.h"
#include "ash/public/cpp/lobster/lobster_system_state.h"
#include "ash/public/cpp/lobster/lobster_text_input_context.h"
#include "base/functional/callback.h"

class AccountId;

namespace ash {

class ASH_PUBLIC_EXPORT LobsterClient {
 public:
  using StatusCallback = base::OnceCallback<void(bool)>;
  virtual ~LobsterClient() = default;

  virtual void SetActiveSession(LobsterSession* session) = 0;
  virtual LobsterSystemState GetSystemState(
      const LobsterTextInputContext& text_input_context) = 0;
  virtual void RequestCandidates(const std::string& query,
                                 int num_candidates,
                                 RequestCandidatesCallback) = 0;
  virtual void InflateCandidate(uint32_t seed,
                                const std::string& query,
                                InflateCandidateCallback) = 0;
  virtual void QueueInsertion(const std::string& image_bytes,
                              StatusCallback insert_status_callback) = 0;
  virtual void ShowDisclaimerUI() = 0;
  virtual void LoadUI(std::optional<std::string> query,
                      LobsterMode mode,
                      const gfx::Rect& caret_bounds) = 0;
  virtual void ShowUI() = 0;
  virtual void CloseUI() = 0;
  // Returns the account ID associated with this client.
  // As the class is created by getting the active user profile, this is
  // equivalent to getting the active user's account ID when this class was
  // created.
  virtual const AccountId& GetAccountId() = 0;
  virtual void AnnounceLater(const std::u16string& message) = 0;
};

}  // namespace ash

#endif  // ASH_PUBLIC_CPP_LOBSTER_LOBSTER_CLIENT_H_
