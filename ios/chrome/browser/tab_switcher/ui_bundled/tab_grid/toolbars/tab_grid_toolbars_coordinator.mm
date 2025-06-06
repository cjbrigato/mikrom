// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/tab_switcher/ui_bundled/tab_grid/toolbars/tab_grid_toolbars_coordinator.h"

#import <ostream>

#import "base/check.h"
#import "components/feature_engagement/public/feature_constants.h"
#import "ios/chrome/browser/bubble/ui_bundled/bubble_view_controller_presenter.h"
#import "ios/chrome/browser/feature_engagement/model/tracker_factory.h"
#import "ios/chrome/browser/first_run/ui_bundled/guided_tour/guided_tour_coordinator.h"
#import "ios/chrome/browser/shared/model/browser/browser.h"
#import "ios/chrome/browser/shared/model/profile/profile_ios.h"
#import "ios/chrome/browser/shared/public/commands/command_dispatcher.h"
#import "ios/chrome/browser/shared/public/commands/tab_grid_toolbar_commands.h"
#import "ios/chrome/browser/tab_switcher/ui_bundled/tab_grid/grid/grid_toolbars_mutator.h"
#import "ios/chrome/browser/tab_switcher/ui_bundled/tab_grid/toolbars/tab_grid_bottom_toolbar.h"
#import "ios/chrome/browser/tab_switcher/ui_bundled/tab_grid/toolbars/tab_grid_page_control.h"
#import "ios/chrome/browser/tab_switcher/ui_bundled/tab_grid/toolbars/tab_grid_toolbars_main_tab_grid_delegate.h"
#import "ios/chrome/browser/tab_switcher/ui_bundled/tab_grid/toolbars/tab_grid_toolbars_mediator.h"
#import "ios/chrome/browser/tab_switcher/ui_bundled/tab_grid/toolbars/tab_grid_top_toolbar.h"
#import "ios/chrome/grit/ios_strings.h"
#import "ui/base/l10n/l10n_util_mac.h"

@interface TabGridToolbarsCoordinator () <GuidedTourCoordinatorDelegate,
                                          TabGridToolbarCommands>
@end

@implementation TabGridToolbarsCoordinator {
  // Mediator of all tab grid toolbars.
  TabGridToolbarsMediator* _mediator;
  // Coordinator for the first step of the guided tour.
  GuidedTourCoordinator* _guidedTourCoordinator;
  ProceduralBlock _guidedTourCompletionBlock;
}

- (void)start {
  Browser* browser = self.browser;
  CHECK(!browser->GetProfile()->IsOffTheRecord());

  _mediator =
      [[TabGridToolbarsMediator alloc] initWithModeHolder:self.modeHolder];

  [self setupTopToolbar];
  [self setupBottomToolbar];

  _mediator.topToolbarConsumer = self.topToolbar;
  _mediator.bottomToolbarConsumer = self.bottomToolbar;
  _mediator.webStateList = browser->GetWebStateList();

  [browser->GetCommandDispatcher()
      startDispatchingToTarget:self
                   forProtocol:@protocol(TabGridToolbarCommands)];
}

- (void)stop {
  [_mediator disconnect];
  _mediator = nil;
}

#pragma mark - Property Implementation.

- (id<GridToolbarsMutator>)toolbarsMutator {
  CHECK(_mediator)
      << "TabGridToolbarsCoordinator's -start should be called before.";
  return _mediator;
}

#pragma mark - TabGridToolbarCommands

- (void)showSavedTabGroupIPH {
  feature_engagement::Tracker* tracker =
      feature_engagement::TrackerFactory::GetForProfile(self.profile);
  if (!tracker->WouldTriggerHelpUI(
          feature_engagement::kIPHiOSSavedTabGroupClosed)) {
    return;
  }

  NSString* IPHTitle =
      l10n_util::GetNSString(IDS_IOS_TAB_GRID_SAVED_TAB_GROUPS);
  __weak __typeof(self) weakSelf = self;
  BubbleViewControllerPresenter* presenter =
      [[BubbleViewControllerPresenter alloc]
               initWithText:IPHTitle
                      title:nil
             arrowDirection:BubbleArrowDirectionUp
                  alignment:BubbleAlignmentCenter
                 bubbleType:BubbleViewTypeDefault
            pageControlPage:BubblePageControlPageNone
          dismissalCallback:^(IPHDismissalReasonType reason) {
            [weakSelf savedTabGroupIPHDismissed];
          }];

  CGRect lastSegmentFrame = [self.topToolbar.pageControl lastSegmentFrame];

  CGPoint anchorPoint = CGPointMake(CGRectGetMidX(lastSegmentFrame),
                                    CGRectGetMaxY(lastSegmentFrame));

  if (![presenter canPresentInView:self.baseViewController.view
                       anchorPoint:anchorPoint] ||
      !tracker->ShouldTriggerHelpUI(
          feature_engagement::kIPHiOSSavedTabGroupClosed)) {
    return;
  }

  [self.topToolbar highlightPageControlItem:TabGridPageTabGroups];
  [presenter presentInViewController:self.baseViewController
                         anchorPoint:anchorPoint];
}

- (void)showGuidedTourIncognitoStepWithDismissalCompletion:
    (ProceduralBlock)completion {
  [self.topToolbar highlightPageControlItem:TabGridPageIncognitoTabs];
  _guidedTourCoordinator = [[GuidedTourCoordinator alloc]
            initWithStep:GuidedTourStep::kTabGridIncognito
      baseViewController:self.baseViewController
                 browser:self.browser
                delegate:self];
  [_guidedTourCoordinator start];
  _guidedTourCompletionBlock = completion;
}

- (void)showGuidedTourTabGroupStepWithDismissalCompletion:
    (ProceduralBlock)completion {
  [self.topToolbar highlightPageControlItem:TabGridPageTabGroups];
  _guidedTourCoordinator = [[GuidedTourCoordinator alloc]
            initWithStep:GuidedTourStep::kTabGridTabGroup
      baseViewController:self.baseViewController
                 browser:self.browser
                delegate:self];
  [_guidedTourCoordinator start];
  _guidedTourCompletionBlock = completion;
}

#pragma mark - GuidedTourCoordinatorDelegate

- (void)nextTappedForStep:(GuidedTourStep)step {
  [self.topToolbar resetLastPageControlHighlight];
}

// Indicates to the delegate that the `step` was dismissed.
- (void)stepCompleted:(GuidedTourStep)step {
  [_guidedTourCoordinator stop];
  _guidedTourCoordinator = nil;
  _guidedTourCompletionBlock();
}

#pragma mark - Private

// Callback for when the saved tab group IPH is dismissed.
- (void)savedTabGroupIPHDismissed {
  [self.topToolbar resetLastPageControlHighlight];
  feature_engagement::Tracker* tracker =
      feature_engagement::TrackerFactory::GetForProfile(self.profile);
  tracker->Dismissed(feature_engagement::kIPHiOSSavedTabGroupClosed);
}

// Configures the top toolbar.
- (void)setupTopToolbar {
  // In iOS 13+, constraints break if the UIToolbar is initialized with a null
  // or zero rect frame. An arbitrary non-zero frame fixes this issue.
  TabGridTopToolbar* topToolbar =
      [[TabGridTopToolbar alloc] initWithFrame:CGRectMake(0, 0, 100, 100)];
  self.topToolbar = topToolbar;
  topToolbar.translatesAutoresizingMaskIntoConstraints = NO;
  [topToolbar setSearchBarDelegate:self.searchDelegate];

  // Configure and initialize the page control.
  [topToolbar.pageControl addTarget:self
                             action:@selector(pageControlChangedValue:)
                   forControlEvents:UIControlEventValueChanged];
  [topToolbar.pageControl addTarget:self
                             action:@selector(pageControlChangedPageByDrag:)
                   forControlEvents:TabGridPageChangeByDragEvent];
  [topToolbar.pageControl addTarget:self
                             action:@selector(pageControlChangedPageByTap:)
                   forControlEvents:TabGridPageChangeByTapEvent];
}

- (void)setupBottomToolbar {
  TabGridBottomToolbar* bottomToolbar = [[TabGridBottomToolbar alloc] init];
  self.bottomToolbar = bottomToolbar;
  bottomToolbar.translatesAutoresizingMaskIntoConstraints = NO;
}

#pragma mark - Control actions

- (void)pageControlChangedValue:(id)sender {
  [self.toolbarTabGridDelegate pageControlChangedValue:sender];
}

- (void)pageControlChangedPageByDrag:(id)sender {
  [self.toolbarTabGridDelegate pageControlChangedPageByDrag:sender];
}

- (void)pageControlChangedPageByTap:(id)sender {
  [self.toolbarTabGridDelegate pageControlChangedPageByTap:sender];
}

@end
