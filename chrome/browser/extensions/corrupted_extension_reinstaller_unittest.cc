// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/corrupted_extension_reinstaller.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/browser_task_environment.h"
#include "extensions/common/mojom/manifest.mojom-shared.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {

namespace {
const char kDummyExtensionId[] = "whatever";
}

class TestReinstallerTracker {
 public:
  TestReinstallerTracker()
      : action_(base::BindRepeating(&TestReinstallerTracker::ReinstallAction,
                                    base::Unretained(this))) {
    CorruptedExtensionReinstaller::set_reinstall_action_for_test(&action_);
  }

  TestReinstallerTracker(const TestReinstallerTracker&) = delete;
  TestReinstallerTracker& operator=(const TestReinstallerTracker&) = delete;

  ~TestReinstallerTracker() {
    CorruptedExtensionReinstaller::set_reinstall_action_for_test(nullptr);
  }
  void ReinstallAction(base::OnceClosure callback,
                       base::TimeDelta reinstall_delay) {
    ++call_count_;
    saved_callback_ = std::move(callback);
  }
  void Proceed() {
    DCHECK(saved_callback_);
    DCHECK(!saved_callback_->is_null());
    // Run() will set |saved_callback_| again, so use a temporary.
    base::OnceClosure callback = std::move(saved_callback_.value());
    saved_callback_.reset();
    std::move(callback).Run();
  }
  int call_count() { return call_count_; }

 private:
  int call_count_ = 0;
  std::optional<base::OnceClosure> saved_callback_;
  CorruptedExtensionReinstaller::ReinstallCallback action_;
};

class CorruptedExtensionReinstallerUnittest : public testing::Test {
 public:
  CorruptedExtensionReinstallerUnittest() = default;
  CorruptedExtensionReinstallerUnittest(
      const CorruptedExtensionReinstallerUnittest&) = delete;
  CorruptedExtensionReinstallerUnittest& operator=(
      const CorruptedExtensionReinstallerUnittest&) = delete;
  ~CorruptedExtensionReinstallerUnittest() override = default;

 private:
  content::BrowserTaskEnvironment task_environment_;
};

// Tests that a single extension corruption will keep retrying reinstallation.
TEST_F(CorruptedExtensionReinstallerUnittest, Retry) {
  TestingProfile profile;
  auto* reinstaller = CorruptedExtensionReinstaller::Get(&profile);
  reinstaller->ExpectReinstallForCorruption(
      kDummyExtensionId,
      CorruptedExtensionReinstaller::PolicyReinstallReason::
          CORRUPTION_DETECTED_WEBSTORE,
      mojom::ManifestLocation::kInternal);

  TestReinstallerTracker tracker;

  reinstaller->NotifyExtensionDisabledDueToCorruption();
  EXPECT_EQ(1, tracker.call_count());
  tracker.Proceed();
  EXPECT_EQ(2, tracker.call_count());
  tracker.Proceed();
  EXPECT_EQ(3, tracker.call_count());
}

// Tests that CorruptedExtensionReinstaller doesn't schedule a
// CheckForExternalUpdates() when one is already in-flight through PostTask.
TEST_F(CorruptedExtensionReinstallerUnittest,
       DoNotScheduleWhenAlreadyInflight) {
  TestingProfile profile;
  auto* reinstaller = CorruptedExtensionReinstaller::Get(&profile);
  reinstaller->ExpectReinstallForCorruption(
      kDummyExtensionId,
      CorruptedExtensionReinstaller::PolicyReinstallReason::
          CORRUPTION_DETECTED_WEBSTORE,
      mojom::ManifestLocation::kInternal);

  TestReinstallerTracker tracker;

  reinstaller->NotifyExtensionDisabledDueToCorruption();
  EXPECT_EQ(1, tracker.call_count());
  reinstaller->NotifyExtensionDisabledDueToCorruption();
  // Resolve the reinstall attempt.
  tracker.Proceed();
  EXPECT_EQ(2, tracker.call_count());
  reinstaller->NotifyExtensionDisabledDueToCorruption();
  // Not resolving the pending attempt will not produce further calls.
  EXPECT_EQ(2, tracker.call_count());
}

}  // namespace extensions
