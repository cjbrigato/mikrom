// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/send_tab_to_self/send_tab_to_self_client_service.h"

#include <memory>

#include "base/time/time.h"
#include "chrome/browser/send_tab_to_self/desktop_notification_handler.h"
#include "chrome/browser/send_tab_to_self/receiving_ui_handler.h"
#include "components/send_tab_to_self/test_send_tab_to_self_model.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace send_tab_to_self {

namespace {

// A test ReceivingUiHandler that keeps track of the number of entries for which
// DisplayNewEntry was called.
class TestReceivingUiHandler : public ReceivingUiHandler {
 public:
  TestReceivingUiHandler() = default;
  ~TestReceivingUiHandler() override = default;

  void DisplayNewEntries(
      const std::vector<const SendTabToSelfEntry*>& new_entries) override {
    number_displayed_entries_ = number_displayed_entries_ + new_entries.size();
  }

  void DismissEntries(const std::vector<std::string>& guids) override {}

  size_t number_displayed_entries() const { return number_displayed_entries_; }

 private:
  size_t number_displayed_entries_ = 0;
};

// Tests that the UI handlers gets notified of each entries that were added
// remotely.
TEST(SendTabToSelfClientServiceTest, MultipleEntriesAdded) {
  // Set up the test objects.
  TestSendTabToSelfModel test_model;
  TestReceivingUiHandler* test_handler = new TestReceivingUiHandler();
  SendTabToSelfClientService client_service(
      std::unique_ptr<TestReceivingUiHandler>(test_handler), &test_model);

  // Create 2 entries and simulated that they were both added remotely.
  SendTabToSelfEntry entry1("a", GURL("http://www.example-a.com"), "a site",
                            base::Time(), "device a", "device b");
  SendTabToSelfEntry entry2("b", GURL("http://www.example-b.com"), "b site",
                            base::Time(), "device b", "device a");
  client_service.EntriesAddedRemotely({&entry1, &entry2});

  EXPECT_EQ(2u, test_handler->number_displayed_entries());
}

}  // namespace

}  // namespace send_tab_to_self
