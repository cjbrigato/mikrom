// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/user_education/interactive_feature_promo_test.h"

#include <string>
#include <utility>
#include <variant>

#include "base/feature_list.h"
#include "base/memory/ref_counted.h"
#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"
#include "base/strings/to_string.h"
#include "base/test/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engine_choice/search_engine_choice_dialog_service.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/user_education/user_education_service.h"
#include "chrome/browser/user_education/user_education_service_factory.h"
#include "chrome/test/interaction/feature_engagement_initialized_observer.h"
#include "chrome/test/interaction/interaction_test_util_browser.h"
#include "chrome/test/interaction/interactive_browser_test.h"
#include "chrome/test/user_education/interactive_feature_promo_test_internal.h"
#include "components/user_education/common/feature_promo/feature_promo_controller.h"
#include "components/user_education/common/feature_promo/feature_promo_registry.h"
#include "components/user_education/common/feature_promo/feature_promo_result.h"
#include "components/user_education/common/feature_promo/feature_promo_specification.h"
#include "components/user_education/common/user_education_features.h"
#include "components/user_education/views/help_bubble_view.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/interaction/element_tracker.h"
#include "ui/base/interaction/interaction_sequence.h"
#include "ui/views/interaction/element_tracker_views.h"

DEFINE_LOCAL_CUSTOM_ELEMENT_EVENT_TYPE(kShowPromoResultReceived);

InteractiveFeaturePromoTestApi::InteractiveFeaturePromoTestApi(
    TrackerMode tracker_mode,
    ClockMode clock_mode,
    InitialSessionState initial_session_state)
    : InteractiveBrowserTestApi(
          std::make_unique<internal::InteractiveFeaturePromoTestPrivate>(
              std::make_unique<InteractionTestUtilBrowser>(),
              std::move(tracker_mode),
              clock_mode,
              initial_session_state)) {}

InteractiveFeaturePromoTestApi::~InteractiveFeaturePromoTestApi() = default;

void InteractiveFeaturePromoTestApi::SetControllerMode(
    ControllerMode controller_mode) {
  test_impl().SetControllerMode(controller_mode);
}

InteractiveFeaturePromoTestApi::MockTracker*
InteractiveFeaturePromoTestApi::GetMockTrackerFor(Browser* browser) {
  return test_impl().GetMockTrackerFor(browser);
}

void InteractiveFeaturePromoTestApi::RegisterTestFeature(
    Browser* browser,
    user_education::FeaturePromoSpecification spec) {
  UserEducationServiceFactory::GetForBrowserContext(browser->profile())
      ->feature_promo_registry()
      .RegisterFeature(std::move(spec));
}

InteractiveFeaturePromoTestApi::MultiStep
InteractiveFeaturePromoTestApi::WaitForFeatureEngagementReady() {
  // Use a refcounted value to pass information between steps, since there is no
  // access to the browser proper in the pure API class.
  auto browser = base::MakeRefCounted<base::RefCountedData<Browser*>>(nullptr);
  auto steps = Steps(
      // Ensure that the correct tracker for the current context is used.
      WithView(kBrowserViewElementId,
               [this, browser](BrowserView* browser_view) {
                 browser->data = browser_view->browser();
                 CHECK(!test_impl().GetMockTrackerFor(browser->data));
               }),
      ObserveState(kFeatureEngagementInitializedState,
                   [browser]() { return browser->data; }),
      WaitForState(kFeatureEngagementInitializedState, true),
      StopObservingState(kFeatureEngagementInitializedState));
  AddDescriptionPrefix(steps, "WaitForFeatureEngagementReady()");
  return steps;
}

InteractiveFeaturePromoTestApi::StepBuilder
InteractiveFeaturePromoTestApi::AdvanceTime(NewTime time) {
  return std::move(Do([this, time]() {
                     test_impl().AdvanceTime(time);
                   }).SetDescription("AdvanceTime()"));
}

InteractiveFeaturePromoTestApi::StepBuilder
InteractiveFeaturePromoTestApi::SetLastActive(NewTime time) {
  return std::move(Do([this, time]() {
                     test_impl().SetLastActive(time);
                   }).SetDescription("SetLastActive()"));
}

InteractiveFeaturePromoTestApi::MultiStep
InteractiveFeaturePromoTestApi::MaybeShowPromo(
    user_education::FeaturePromoParams params,
    ShowPromoResult show_promo_result) {
  // Always attempt to show the promo.
  bool is_web_bubble = false;
  ui::ElementIdentifier custom_bubble_id;
  user_education::FeaturePromoResult expected_result;
  if (std::holds_alternative<WebUiHelpBubbleShown>(show_promo_result)) {
    is_web_bubble = true;
    expected_result = user_education::FeaturePromoResult::Success();
  } else if (auto* const shown =
                 std::get_if<CustomHelpBubbleShown>(&show_promo_result)) {
    custom_bubble_id = shown->expected_id;
    expected_result = user_education::FeaturePromoResult::Success();
  } else {
    expected_result =
        std::get<user_education::FeaturePromoResult>(show_promo_result);
  }
  const base::Feature& iph_feature = *params.feature;
  using Result = base::RefCountedData<user_education::FeaturePromoResult>;
  auto start_result = base::MakeRefCounted<Result>();
  auto steps = Steps(
      std::move(
          WithView(
              kBrowserViewElementId,
              [this, start_result, params = std::move(params),
               expected_result](BrowserView* browser_view) mutable {
                const base::Feature& iph_feature = *params.feature;

                // If using a mock tracker, ensure that it returns the correct
                // status.
                auto* const tracker =
                    test_impl().GetMockTrackerFor(browser_view->browser());
                if (tracker) {
                  if (expected_result) {
                    EXPECT_CALL(*tracker,
                                ShouldTriggerHelpUI(testing::Ref(iph_feature)))
                        .WillOnce(testing::Return(true));
                  } else if (user_education::FeaturePromoResult::
                                 kBlockedByConfig == expected_result) {
                    EXPECT_CALL(*tracker,
                                ShouldTriggerHelpUI(testing::Ref(iph_feature)))
                        .WillOnce(testing::Return(false));
                  } else {
                    EXPECT_CALL(*tracker,
                                ShouldTriggerHelpUI(testing::Ref(iph_feature)))
                        .Times(0);
                  }
                }

                // Set up the callback to tell us the result.
                ui::SafeElementReference browser_el(
                    views::ElementTrackerViews::GetInstance()
                        ->GetElementForView(browser_view));
                params.show_promo_result_callback = base::BindLambdaForTesting(
                    [el = std::move(browser_el),
                     old_cb = std::move(params.show_promo_result_callback),
                     start_result](user_education::FeaturePromoResult
                                       promo_result) mutable {
                      CHECK(el);
                      start_result->data = promo_result;
                      if (old_cb) {
                        std::move(old_cb).Run(promo_result);
                      }
                      ui::ElementTracker::GetFrameworkDelegate()
                          ->NotifyCustomEvent(el.get(),
                                              kShowPromoResultReceived);
                    });

                // Attempt to show the promo.
                browser_view->MaybeShowFeaturePromo(std::move(params));

                // If the promo showed, expect it to be dismissed at some point.
                if (expected_result) {
                  if (tracker) {
                    EXPECT_CALL(*tracker, Dismissed(testing::Ref(iph_feature)));
                  }
                } else if (user_education::features::IsUserEducationV25()) {
                  switch (test_impl().clock_mode()) {
                    case ClockMode::kUseTestClock:
                      test_impl().AdvanceTime(
                          user_education::features::GetLowPriorityTimeout() +
                          base::Seconds(1));
                      break;
                    case ClockMode::kUseDefaultClock:
                      CHECK(test_impl()
                                .use_shortened_timeouts_for_internal_testing())
                          << "Tests that verify an IPH has not been shown that "
                             "also use a live (default) clock must use "
                             "set_use_shortened_timeouts_for_internal_testing()"
                             " to prevent tests from failing or flaking due to "
                             "test timeout.";
                  }
                }
              })
              .SetDescription("Try to show promo")),
      WaitForEvent(kBrowserViewElementId, kShowPromoResultReceived),
      CheckResult([start_result]() { return start_result->data; },
                  expected_result));

  // If success is expected, add steps to wait for the bubble to be shown and
  // verify that the correct promo is showing.
  if (is_web_bubble) {
    steps += CheckPromoImpl(iph_feature, /*requested=*/true,
                            /*include_queued=*/false);
  } else if (custom_bubble_id) {
    steps += Steps(WaitForShow(custom_bubble_id),
                   CheckPromoImpl(iph_feature, /*requested=*/true,
                                  /*include_queued=*/false));
  } else if (expected_result) {
    steps += WaitForPromo(iph_feature);
  }

  AddDescriptionPrefix(
      steps, base::StrCat({"MaybeShowPromo( ", iph_feature.name, ", ",
                           is_web_bubble ? std::string("WebUI Help Bubble")
                                         : base::ToString(expected_result),
                           " )"}));
  return steps;
}

InteractiveFeaturePromoTestApi::MultiStep
InteractiveFeaturePromoTestApi::WaitForPromo(const base::Feature& iph_feature) {
  auto steps =
      Steps(WaitForShow(
                user_education::HelpBubbleView::kHelpBubbleElementIdForTesting),
            CheckPromoImpl(iph_feature, /*requested=*/true,
                           /*include_queued=*/false));

  AddDescriptionPrefix(
      steps, base::StrCat({"WaitForPromo( ", iph_feature.name, " )"}));
  return steps;
}

InteractiveFeaturePromoTestApi::StepBuilder
InteractiveFeaturePromoTestApi::CheckPromoRequested(
    const base::Feature& iph_feature,
    bool requested) {
  return CheckPromoImpl(iph_feature, requested, true);
}

InteractiveFeaturePromoTestApi::StepBuilder
InteractiveFeaturePromoTestApi::CheckPromoImpl(const base::Feature& iph_feature,
                                               bool requested,
                                               bool include_queued) {
  return std::move(
      WithElement(
          kBrowserViewElementId,
          [&iph_feature, requested, include_queued](
              ui::InteractionSequence* seq, ui::TrackedElement* browser_el) {
            bool actual = false;
            if (seq->IsCurrentStepInAnyContextForTesting()) {
              for (const auto browser : *BrowserList::GetInstance()) {
                if (browser->window()->IsFeaturePromoActive(iph_feature) ||
                    (include_queued &&
                     browser->window()->IsFeaturePromoQueued(iph_feature))) {
                  actual = true;
                  break;
                }
              }
            } else {
              auto* const browser = AsView<BrowserView>(browser_el);
              actual = browser->IsFeaturePromoActive(iph_feature) ||
                       (include_queued &&
                        browser->IsFeaturePromoQueued(iph_feature));
            }
            if (actual != requested) {
              seq->FailForTesting();
            }
          })
          .SetDescription(base::StringPrintf("CheckPromoRequested(%s, %s)",
                                             iph_feature.name,
                                             base::ToString(requested))));
}

InteractiveFeaturePromoTestApi::MultiStep
InteractiveFeaturePromoTestApi::AbortPromo(const base::Feature& iph_feature,
                                           bool expected_result) {
  auto steps = Steps(CheckView(
      kBrowserViewElementId,
      [&iph_feature](BrowserView* browser_view) {
        return browser_view->AbortFeaturePromo(iph_feature);
      },
      expected_result));
  if (expected_result) {
    steps.push_back(WaitForHide(
        user_education::HelpBubbleView::kHelpBubbleElementIdForTesting));
  }

  AddDescriptionPrefix(steps,
                       base::StrCat({"AbortPromo( ", iph_feature.name, ", ",
                                     base::ToString(expected_result), " )"}));
  return steps;
}

InteractiveFeaturePromoTestApi::MultiStep
InteractiveFeaturePromoTestApi::PressClosePromoButton() {
  auto steps = Steps(
      PressButton(user_education::HelpBubbleView::kCloseButtonIdForTesting),
      WaitForHide(
          user_education::HelpBubbleView::kHelpBubbleElementIdForTesting));
  AddDescriptionPrefix(steps, "PressClosePromoButton()");
  return steps;
}

InteractiveFeaturePromoTestApi::MultiStep
InteractiveFeaturePromoTestApi::PressDefaultPromoButton() {
  auto steps = Steps(
      PressButton(user_education::HelpBubbleView::kDefaultButtonIdForTesting),
      WaitForHide(
          user_education::HelpBubbleView::kHelpBubbleElementIdForTesting));
  AddDescriptionPrefix(steps, "PressDefaultPromoButton()");
  return steps;
}

InteractiveFeaturePromoTestApi::MultiStep
InteractiveFeaturePromoTestApi::PressNonDefaultPromoButton() {
  auto steps = Steps(
      PressButton(
          user_education::HelpBubbleView::kFirstNonDefaultButtonIdForTesting),
      WaitForHide(
          user_education::HelpBubbleView::kHelpBubbleElementIdForTesting));
  AddDescriptionPrefix(steps, "PressNonDefaultPromoButton()");
  return steps;
}
