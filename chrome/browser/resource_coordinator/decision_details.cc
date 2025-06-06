// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/resource_coordinator/decision_details.h"

#include <array>

#include "base/check.h"

namespace resource_coordinator {

namespace {

// These are intended to be human readable descriptions of the various failure
// reasons. They don't need to be localized as they are for a developer-only
// WebUI.
auto kDecisionFailureReasonStrings = std::to_array<const char*>({
    "Browser opted out via enterprise policy",
    "Tab opted out via origin trial",
    "Origin is in global disallowlist",
    "Origin has been observed playing audio while backgrounded",
    "Origin has been observed updating favicon while backgrounded",
    "Origin is temporarily protected while under observation",
    "Origin has been observed updating title while backgrounded",
    "Tab is currently capturing the camera and/or microphone",
    "Tab has been protected by an extension",
    "Tab contains unsubmitted text form entry",
    "Tab contains a PDF",
    "Tab content is being mirrored/cast",
    "Tab is currently emitting audio",
    "Tab is currently using WebSockets",
    "Tab is currently using WebUSB",
    "Tab is currently visible",
    "Tab is currently using DevTools",
    "Tab is currently capturing a window or screen",
    "Tab is sharing its BrowsingInstance with another tab",
    "Tab is currently connected to a bluetooth device",
    "Tab is currently holding a WebLock",
    "Tab is currently holding an IndexedDB lock",
    "Tab has notification permission ",
    "Tab is a web application window",
    "Tab is displaying content in picture-in-picture",
});
static_assert(std::size(kDecisionFailureReasonStrings) ==
                  static_cast<size_t>(DecisionFailureReason::MAX),
              "kDecisionFailureReasonStrings not up to date with enum");

auto kDecisionSuccessReasonStrings = std::to_array<const char*>({
    "Tab opted in via origin trial",
    "Origin is in global allowlist",
    "Origin has locally been observed to be safe via heuristic logic",
});
static_assert(std::size(kDecisionSuccessReasonStrings) ==
                  static_cast<size_t>(DecisionSuccessReason::MAX),
              "kDecisionSuccessReasonStrings not up to date with enum");

}  // namespace

const char* ToString(DecisionFailureReason failure_reason) {
  if (failure_reason == DecisionFailureReason::INVALID ||
      failure_reason == DecisionFailureReason::MAX)
    return nullptr;
  return kDecisionFailureReasonStrings[static_cast<size_t>(failure_reason)];
}

const char* ToString(DecisionSuccessReason success_reason) {
  if (success_reason == DecisionSuccessReason::INVALID ||
      success_reason == DecisionSuccessReason::MAX)
    return nullptr;
  return kDecisionSuccessReasonStrings[static_cast<size_t>(success_reason)];
}

DecisionDetails::Reason::Reason()
    : success_reason_(DecisionSuccessReason::INVALID),
      failure_reason_(DecisionFailureReason::INVALID) {}

DecisionDetails::Reason::Reason(DecisionSuccessReason success_reason)
    : success_reason_(success_reason),
      failure_reason_(DecisionFailureReason::INVALID) {
  DCHECK(IsSuccess());
}

DecisionDetails::Reason::Reason(DecisionFailureReason failure_reason)
    : success_reason_(DecisionSuccessReason::INVALID),
      failure_reason_(failure_reason) {
  DCHECK(IsFailure());
}

DecisionDetails::Reason::Reason(const Reason& rhs) = default;
DecisionDetails::Reason::~Reason() = default;

DecisionDetails::Reason& DecisionDetails::Reason::operator=(const Reason& rhs) =
    default;

bool DecisionDetails::Reason::IsValid() const {
  return IsSuccess() || IsFailure();
}

bool DecisionDetails::Reason::IsSuccess() const {
  if (success_reason_ == DecisionSuccessReason::INVALID ||
      success_reason_ == DecisionSuccessReason::MAX ||
      failure_reason_ != DecisionFailureReason::INVALID)
    return false;
  return true;
}

bool DecisionDetails::Reason::IsFailure() const {
  if (failure_reason_ == DecisionFailureReason::INVALID ||
      failure_reason_ == DecisionFailureReason::MAX ||
      success_reason_ != DecisionSuccessReason::INVALID)
    return false;
  return true;
}

DecisionSuccessReason DecisionDetails::Reason::success_reason() const {
  DCHECK(IsSuccess());
  return success_reason_;
}

DecisionFailureReason DecisionDetails::Reason::failure_reason() const {
  DCHECK(IsFailure());
  return failure_reason_;
}

const char* DecisionDetails::Reason::ToString() const {
  if (!IsValid())
    return nullptr;
  if (IsSuccess())
    return ::resource_coordinator::ToString(success_reason_);
  DCHECK(IsFailure());
  return ::resource_coordinator::ToString(failure_reason_);
}

DecisionDetails::DecisionDetails() : toggled_(false) {}

DecisionDetails::~DecisionDetails() = default;

DecisionDetails& DecisionDetails::operator=(DecisionDetails&& rhs) {
  toggled_ = rhs.toggled_;
  reasons_ = std::move(rhs.reasons_);
  rhs.Clear();
  return *this;
}

bool DecisionDetails::AddReason(const Reason& reason) {
  reasons_.push_back(reason);
  return CheckIfToggled();
}

bool DecisionDetails::AddReason(DecisionFailureReason failure_reason) {
  reasons_.push_back(Reason(failure_reason));
  return CheckIfToggled();
}

bool DecisionDetails::AddReason(DecisionSuccessReason success_reason) {
  reasons_.push_back(Reason(success_reason));
  return CheckIfToggled();
}

bool DecisionDetails::IsPositive() const {
  // A decision without supporting reasons is negative by default.
  if (reasons_.empty())
    return false;
  return reasons_.front().IsSuccess();
}

DecisionSuccessReason DecisionDetails::SuccessReason() const {
  DCHECK(!reasons_.empty());
  return reasons_.front().success_reason();
}

DecisionFailureReason DecisionDetails::FailureReason() const {
  DCHECK(!reasons_.empty());
  return reasons_.front().failure_reason();
}

std::vector<std::string> DecisionDetails::GetFailureReasonStrings() const {
  std::vector<std::string> reasons;
  for (const auto& reason : reasons_) {
    if (reason.IsSuccess())
      break;
    reasons.push_back(reason.ToString());
  }
  return reasons;
}

void DecisionDetails::Clear() {
  reasons_.clear();
  toggled_ = false;
}

bool DecisionDetails::CheckIfToggled() {
  if (toggled_)
    return true;
  if (reasons_.size() <= 1)
    return false;
  // Determine if the last reason is of a different type than the one before. If
  // so, then the toggle has occurred.
  toggled_ = reasons_[reasons_.size() - 1].IsSuccess() !=
             reasons_[reasons_.size() - 2].IsSuccess();
  return toggled_;
}

}  // namespace resource_coordinator
