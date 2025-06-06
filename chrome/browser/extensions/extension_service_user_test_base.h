// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_SERVICE_USER_TEST_BASE_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_SERVICE_USER_TEST_BASE_H_

#include "build/chromeos_buildflags.h"
#include "chrome/browser/extensions/extension_service_test_base.h"
#include "extensions/buildflags/buildflags.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/ash/login/users/fake_chrome_user_manager.h"
#include "components/user_manager/scoped_user_manager.h"
#endif

static_assert(BUILDFLAG(ENABLE_EXTENSIONS_CORE));

namespace extensions {

// Test class used to setup test users.
class ExtensionServiceUserTestBase : public ExtensionServiceTestBase {
 public:
  ExtensionServiceUserTestBase();
  ~ExtensionServiceUserTestBase() override;

#if BUILDFLAG(IS_CHROMEOS)
  void SetUp() override;

  void TearDown() override;

  void LoginChromeOSUser(const user_manager::User* user,
                         const AccountId& account_id);

  ash::FakeChromeUserManager* GetFakeUserManager() const {
    return static_cast<ash::FakeChromeUserManager*>(
        user_manager::UserManager::Get());
  }
#endif  // BUILDFLAG(IS_CHROMEOS)

  // Set the testing profile for the test as a guest if `is_guest` is `true`.
  // On ChromeOS, also login a `user_manager::User` and set it to be a guest
  // account if `is_guest` is `true`.
  void MaybeSetUpTestUser(bool is_guest);

 protected:
  // Alternatively, a subclass may pass a BrowserTaskEnvironment directly.
  explicit ExtensionServiceUserTestBase(
      std::unique_ptr<content::BrowserTaskEnvironment> task_environment);

#if BUILDFLAG(IS_CHROMEOS)
  AccountId account_id_;

 private:
  std::unique_ptr<user_manager::ScopedUserManager> scoped_user_manager_;
#endif  // BUILDFLAG(IS_CHROMEOS)
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_SERVICE_USER_TEST_BASE_H_
