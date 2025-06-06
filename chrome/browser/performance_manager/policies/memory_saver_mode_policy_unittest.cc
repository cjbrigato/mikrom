// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/performance_manager/policies/memory_saver_mode_policy.h"

#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chrome/browser/performance_manager/test_support/page_discarding_utils.h"
#include "components/performance_manager/public/decorators/tab_page_decorator.h"
#include "components/performance_manager/public/features.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/performance_manager/public/user_tuning/tab_revisit_tracker.h"
#include "components/prefs/testing_pref_service.h"

namespace performance_manager::policies {

namespace {
// These discard timeouts are based on the values in
// `MemorySaverModePolicy::GetTimeBeforeDiscardForCurrentMode`.
constexpr base::TimeDelta AGGRESSIVE_TIMEOUT = base::Hours(2);
constexpr base::TimeDelta MEDIUM_TIMEOUT = base::Hours(4);
constexpr base::TimeDelta CONSERVATIVE_TIMEOUT = base::Hours(6);
}  // namespace

class TestTabRevisitTracker : public TabRevisitTracker {
 public:
  void SetStateBundle(const TabPageDecorator::TabHandle* tab_handle,
                      StateBundle bundle) {
    state_bundles_[tab_handle] = bundle;
  }

 private:
  StateBundle GetStateForTabHandle(
      const TabPageDecorator::TabHandle* tab_handle) override {
    auto it = state_bundles_.find(tab_handle);
    // Some of these tests don't exercise behavior around the
    // TabRevisitTrackerState. Instead of requiring all of them to set up proper
    // state explicitly, just return a default constructed bundle. The only
    // field that is being used from MemorySaverModePolicy is `num_revisits`,
    // and it being default initialized to 0 is what we'd want anyway.
    if (it != state_bundles_.end()) {
      return it->second;
    }
    return {};
  }

  std::map<const TabPageDecorator::TabHandle*, TabRevisitTracker::StateBundle>
      state_bundles_;
};

class MemorySaverModeTest : public testing::GraphTestHarnessWithMockDiscarder {
 public:
  void SetUp() override {
    testing::GraphTestHarnessWithMockDiscarder::SetUp();

    graph()->PassToGraph(
        std::make_unique<performance_manager::TabPageDecorator>());
    std::unique_ptr<TestTabRevisitTracker> tab_revisit_tracker =
        std::make_unique<TestTabRevisitTracker>();
    tab_revisit_tracker_ = tab_revisit_tracker.get();
    graph()->PassToGraph(std::move(tab_revisit_tracker));

    // This is usually called when the profile is created. Fake it here since it
    // doesn't happen in tests.
    DiscardEligibilityPolicy::GetFromGraph(graph())
        ->SetNoDiscardPatternsForProfile(
            static_cast<PageNode*>(page_node())->GetBrowserContextID(), {});

    auto policy = std::make_unique<MemorySaverModePolicy>();
    policy_ = policy.get();
    graph()->PassToGraph(std::move(policy));

    // Recreates `page_node()`, so it's captured by `TabPageDecorator`.
    RecreateNodes();
  }

  void TearDown() override {
    graph()->TakeFromGraph(policy_);
    testing::GraphTestHarnessWithMockDiscarder::TearDown();
  }

  MemorySaverModePolicy* policy() { return policy_; }

 protected:
  // Creates a `PageNode` with a main frame. The created `PageNode` does not
  // meet all the conditions to be discardable.
  PageNodeImpl* CreateOtherPageNode() {
    other_process_node_ = CreateNode<performance_manager::ProcessNodeImpl>();
    other_page_node_ = CreateNode<performance_manager::PageNodeImpl>();
    other_main_frame_node_ = CreateFrameNodeAutoId(other_process_node_.get(),
                                                   other_page_node_.get());
    return other_page_node_.get();
  }

  void ResetOtherPage() {
    other_main_frame_node_.reset();
    other_page_node_.reset();
    other_page_node_.reset();
  }

  TestTabRevisitTracker* tab_revisit_tracker() { return tab_revisit_tracker_; }

 private:
  raw_ptr<MemorySaverModePolicy, DanglingUntriaged> policy_;

  performance_manager::TestNodeWrapper<performance_manager::PageNodeImpl>
      other_page_node_;
  performance_manager::TestNodeWrapper<performance_manager::ProcessNodeImpl>
      other_process_node_;
  performance_manager::TestNodeWrapper<performance_manager::FrameNodeImpl>
      other_main_frame_node_;

  raw_ptr<TestTabRevisitTracker> tab_revisit_tracker_;
};

TEST_F(MemorySaverModeTest, NoDiscardIfMemorySaverOff) {
  EXPECT_EQ(page_node()->GetType(), PageType::kTab);
  page_node()->SetIsVisible(true);
  page_node()->SetIsVisible(false);
  task_env().FastForwardUntilNoTasksRemain();
  ::testing::Mock::VerifyAndClearExpectations(discarder());
}

TEST_F(MemorySaverModeTest, DiscardAfterBackgrounded) {
  EXPECT_EQ(page_node()->GetType(), PageType::kTab);
  page_node()->SetIsVisible(true);
  policy()->OnMemorySaverModeChanged(true);

  EXPECT_CALL(*discarder(), DiscardPageNodeImpl(page_node()))
      .WillOnce(::testing::Return(true));
  page_node()->SetIsVisible(false);

  task_env().FastForwardBy(policy()->GetTimeBeforeDiscardForTesting());
  ::testing::Mock::VerifyAndClearExpectations(discarder());
}

TEST_F(MemorySaverModeTest, DiscardAfterAggressiveTimeout) {
  EXPECT_EQ(page_node()->GetType(), PageType::kTab);
  page_node()->SetIsVisible(true);
  policy()->SetMode(
      user_tuning::prefs::MemorySaverModeAggressiveness::kAggressive);
  policy()->OnMemorySaverModeChanged(true);

  EXPECT_EQ(policy()->GetTimeBeforeDiscardForTesting(), AGGRESSIVE_TIMEOUT);

  EXPECT_CALL(*discarder(), DiscardPageNodeImpl(page_node()))
      .WillOnce(::testing::Return(true));
  page_node()->SetIsVisible(false);

  task_env().FastForwardBy(policy()->GetTimeBeforeDiscardForTesting());
  ::testing::Mock::VerifyAndClearExpectations(discarder());
}

TEST_F(MemorySaverModeTest, DiscardAfterConservativeTimeout) {
  EXPECT_EQ(page_node()->GetType(), PageType::kTab);
  page_node()->SetIsVisible(true);
  policy()->SetMode(
      user_tuning::prefs::MemorySaverModeAggressiveness::kConservative);
  policy()->OnMemorySaverModeChanged(true);

  EXPECT_EQ(policy()->GetTimeBeforeDiscardForTesting(), CONSERVATIVE_TIMEOUT);

  EXPECT_CALL(*discarder(), DiscardPageNodeImpl(page_node()))
      .WillOnce(::testing::Return(true));
  page_node()->SetIsVisible(false);

  task_env().FastForwardBy(policy()->GetTimeBeforeDiscardForTesting());
  ::testing::Mock::VerifyAndClearExpectations(discarder());
}

TEST_F(MemorySaverModeTest, DontDiscardAfterBackgroundedIfSuspended) {
  EXPECT_EQ(page_node()->GetType(), PageType::kTab);
  page_node()->SetIsVisible(true);
  policy()->OnMemorySaverModeChanged(true);
  page_node()->SetIsVisible(false);

  // The tab isn't discarded if the elapsed time was spent with the device
  // suspended.
  task_env().SuspendedFastForwardBy(base::Hours(10));
  ::testing::Mock::VerifyAndClearExpectations(discarder());

  // Advance only one hour, there should still not be a discard.
  task_env().FastForwardBy(base::Hours(1));
  ::testing::Mock::VerifyAndClearExpectations(discarder());

  // Suspend again for more than the expected time, no discard should happen.
  task_env().SuspendedFastForwardBy(base::Hours(10));
  ::testing::Mock::VerifyAndClearExpectations(discarder());

  EXPECT_CALL(*discarder(), DiscardPageNodeImpl(page_node()))
      .WillOnce(::testing::Return(true));

  // Finally advance un-suspended until the time is elapsed, the tab should be
  // discarded.
  task_env().FastForwardBy(MEDIUM_TIMEOUT - base::Hours(1));
  ::testing::Mock::VerifyAndClearExpectations(discarder());
}

TEST_F(MemorySaverModeTest, DontDiscardIfPageIsNotATab) {
  // This case will be using a different page node, so make the default one
  // visible so it's not discarded.
  page_node()->SetIsVisible(true);
  policy()->OnMemorySaverModeChanged(true);

  PageNodeImpl* page_node = CreateOtherPageNode();
  EXPECT_EQ(PageType::kUnknown, page_node->GetType());

  // Fulfill all conditions to be discardable, except setting type to `kTab`.
  page_node->SetIsVisible(false);
  page_node->SetIsAudible(false);
  const auto kUrl = GURL("https://foo.com");
  page_node->OnMainFrameNavigationCommitted(
      false, base::TimeTicks::Now(), 42, kUrl, "text/html",
      /*notification_permission_status=*/blink::mojom::PermissionStatus::ASK);
  (*page_node->main_frame_nodes().begin())
      ->OnNavigationCommitted(kUrl, url::Origin::Create(kUrl),
                              /*same_document=*/false,
                              /*is_served_from_back_forward_cache=*/false);

  task_env().FastForwardUntilNoTasksRemain();
  ::testing::Mock::VerifyAndClearExpectations(discarder());
}

// The tab shouldn't be discarded if it's playing audio. There are many other
// conditions that prevent discarding, but they're implemented in
// `DiscardEligibilityPolicy` and therefore tested there.
TEST_F(MemorySaverModeTest, DontDiscardIfPlayingAudio) {
  EXPECT_EQ(page_node()->GetType(), PageType::kTab);
  page_node()->SetIsVisible(true);
  policy()->OnMemorySaverModeChanged(true);

  page_node()->SetIsAudible(true);

  page_node()->SetIsVisible(false);
  task_env().FastForwardUntilNoTasksRemain();
  ::testing::Mock::VerifyAndClearExpectations(discarder());
}

TEST_F(MemorySaverModeTest, DontDiscardIfAlreadyNotVisibleWhenModeEnabled) {
  EXPECT_EQ(page_node()->GetType(), PageType::kTab);
  page_node()->SetIsVisible(true);
  page_node()->SetIsVisible(false);

  // Shouldn't be discarded yet
  task_env().FastForwardUntilNoTasksRemain();
  ::testing::Mock::VerifyAndClearExpectations(discarder());

  // Advance time by the usual discard interval, minus 10 seconds.
  task_env().FastForwardBy(policy()->GetTimeBeforeDiscardForTesting() -
                           base::Seconds(10));
  ::testing::Mock::VerifyAndClearExpectations(discarder());

  policy()->OnMemorySaverModeChanged(true);

  // The page should not be discarded 10 seconds after the mode is changed.
  task_env().FastForwardBy(base::Seconds(10));
  ::testing::Mock::VerifyAndClearExpectations(discarder());

  // Instead, it should be discarded after the usual discard interval.
  EXPECT_CALL(*discarder(), DiscardPageNodeImpl(page_node()))
      .WillOnce(::testing::Return(true));
  task_env().FastForwardBy(policy()->GetTimeBeforeDiscardForTesting() -
                           base::Seconds(10));
  ::testing::Mock::VerifyAndClearExpectations(discarder());
}

TEST_F(MemorySaverModeTest, NoDiscardIfPageNodeRemoved) {
  // This case will be using a different page node, so make the default one
  // visible so it's not discarded.
  page_node()->SetIsVisible(true);
  policy()->OnMemorySaverModeChanged(true);

  PageNodeImpl* page_node = CreateOtherPageNode();
  testing::MakePageNodeDiscardable(page_node, task_env());
  EXPECT_EQ(PageType::kTab, page_node->GetType());

  page_node->SetIsVisible(false);
  ResetOtherPage();

  task_env().FastForwardUntilNoTasksRemain();
  ::testing::Mock::VerifyAndClearExpectations(discarder());
}

TEST_F(MemorySaverModeTest, PageNodeDiscardedIfTypeChanges) {
  // This case will be using a different page node, so make the default one
  // visible so it's not discarded.
  page_node()->SetIsVisible(true);
  policy()->OnMemorySaverModeChanged(true);

  PageNodeImpl* page_node = CreateOtherPageNode();
  EXPECT_EQ(PageType::kUnknown, page_node->GetType());

  testing::MakePageNodeDiscardable(page_node, task_env());
  EXPECT_EQ(page_node->GetType(), PageType::kTab);

  EXPECT_CALL(*discarder(), DiscardPageNodeImpl(page_node))
      .WillOnce(::testing::Return(true));
  task_env().FastForwardBy(policy()->GetTimeBeforeDiscardForTesting());
  ::testing::Mock::VerifyAndClearExpectations(discarder());
}

TEST_F(MemorySaverModeTest,
       DiscardAfterTimeForCurrentModeIfNumRevisitsUnderMax) {
  EXPECT_EQ(page_node()->GetType(), PageType::kTab);
  page_node()->SetIsVisible(true);
  policy()->OnMemorySaverModeChanged(true);

  page_node()->SetIsVisible(false);
  EXPECT_EQ(policy()->GetTimeBeforeDiscardForTesting(), MEDIUM_TIMEOUT);

  // Advancing by less than 4 hours shouldn't discard.
  task_env().FastForwardBy(policy()->GetTimeBeforeDiscardForTesting() -
                           base::Seconds(10));
  ::testing::Mock::VerifyAndClearExpectations(discarder());

  EXPECT_CALL(*discarder(), DiscardPageNodeImpl(page_node()))
      .WillOnce(::testing::Return(true));
  task_env().FastForwardBy(base::Seconds(10));
  ::testing::Mock::VerifyAndClearExpectations(discarder());
}

TEST_F(MemorySaverModeTest, DontDiscardIfAboveMaxNumRevisits) {
  EXPECT_EQ(page_node()->GetType(), PageType::kTab);
  page_node()->SetIsVisible(true);
  policy()->OnMemorySaverModeChanged(true);

  TabRevisitTracker::StateBundle state;
  state.num_revisits =
      100;  // needs to be > 5 because the mode is set to "conservative".
  tab_revisit_tracker()->SetStateBundle(
      TabPageDecorator::FromPageNode(page_node()), state);

  page_node()->SetIsVisible(false);
  EXPECT_EQ(policy()->GetTimeBeforeDiscardForTesting(), MEDIUM_TIMEOUT);

  // Advancing by 4 hours shouldn't discard because the tab has been revisited
  // too many times.
  task_env().FastForwardBy(policy()->GetTimeBeforeDiscardForTesting());
  ::testing::Mock::VerifyAndClearExpectations(discarder());
}

}  // namespace performance_manager::policies
