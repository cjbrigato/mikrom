// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/shell/test/test_shell_main_delegate.h"

#include <variant>

#include "base/command_line.h"
#include "content/public/browser/browser_main_runner.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/network_service_test_helper.h"
#include "content/public/utility/content_utility_client.h"

namespace {

class TestShellContentUtilityClient : public content::ContentUtilityClient {
 public:
  TestShellContentUtilityClient() {
    network_service_test_helper_ = content::NetworkServiceTestHelper::Create();
  }

  TestShellContentUtilityClient(const TestShellContentUtilityClient&) = delete;
  TestShellContentUtilityClient& operator=(
      const TestShellContentUtilityClient&) = delete;

  ~TestShellContentUtilityClient() override {}

 private:
  std::unique_ptr<content::NetworkServiceTestHelper>
      network_service_test_helper_;
};

}  // namespace

namespace extensions {

TestShellMainDelegate::TestShellMainDelegate() = default;

TestShellMainDelegate::~TestShellMainDelegate() = default;

content::ContentUtilityClient*
TestShellMainDelegate::CreateContentUtilityClient() {
  utility_client_ = std::make_unique<TestShellContentUtilityClient>();
  return utility_client_.get();
}

}  // namespace extensions
