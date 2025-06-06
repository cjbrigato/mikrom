// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_FULLSCREEN_UI_BUNDLED_FULLSCREEN_CONTROLLER_H_
#define IOS_CHROME_BROWSER_FULLSCREEN_UI_BUNDLED_FULLSCREEN_CONTROLLER_H_

#import <UIKit/UIKit.h>

#include "base/supports_user_data.h"
#include "ios/chrome/browser/shared/model/browser/browser_user_data.h"

@class ChromeBroadcaster;
class FullscreenControllerObserver;
@class ToolbarsSize;
enum class FullscreenExitReason;

// An object that observes scrolling events in the main content area and
// calculates how much of the toolbar should be visible as a result.  When the
// user scrolls down the screen, the toolbar should be hidden to allow more of
// the page's content to be visible.
class FullscreenController : public BrowserUserData<FullscreenController> {
 public:
  // The ChromeBroadcaster through the FullscreenController receives UI
  // information necessary to calculate fullscreen progress.
  // TODO(crbug.com/41358770): Once FullscreenController is a BrowserUserData,
  // remove this ad-hoc broadcaster and drive the animations via the Browser's
  // ChromeBroadcaster.
  virtual ChromeBroadcaster* broadcaster() = 0;

  // Getter and setter for the ToolbarsSize.
  virtual ToolbarsSize* GetToolbarsSize() const = 0;
  virtual void SetToolbarsSize(ToolbarsSize* toolbars_size) = 0;

  // Adds and removes FullscreenControllerObservers.
  virtual void AddObserver(FullscreenControllerObserver* observer) = 0;
  virtual void RemoveObserver(FullscreenControllerObserver* observer) = 0;

  // FullscreenController can be disabled when a feature requires that the
  // toolbar be fully visible.  Since there are multiple reasons fullscreen
  // might need to be disabled, this state is represented by a counter rather
  // than a single bool.  When a feature needs the toolbar to be visible, it
  // calls IncrementDisabledCounter().  After that feature no longer requires
  // the toolbar, it calls DecrementDisabledCounter().  IsEnabled() returns
  // true when the counter is equal to zero.  ScopedFullscreenDisabler can be
  // used to tie a disabled counter to an object's lifetime.
  virtual bool IsEnabled() const = 0;
  virtual void IncrementDisabledCounter() = 0;
  virtual void DecrementDisabledCounter() = 0;

  // Returns whether fullscreen is implemented by resizing the web view scroll
  // view rather than setting the content inset.
  virtual bool ResizesScrollView() const = 0;

  // FullscreenController isn't notified when the trait collection of the
  // browser is changed. This method is here to notify it.
  virtual void BrowserTraitCollectionChangedBegin() = 0;
  virtual void BrowserTraitCollectionChangedEnd() = 0;

  // Returns the current fullscreen progress value.  This is a float between 0.0
  // and 1.0, where 0.0 denotes that the toolbar should be completely hidden and
  // 1.0 denotes that the toolbar should be completely visible.
  virtual CGFloat GetProgress() const = 0;

  // Returns the max and min insets for the visible content area's viewport.
  // The max insets correspond to a progress of 1.0, and the min insets are for
  // progress 0.0.
  virtual UIEdgeInsets GetMinViewportInsets() const = 0;
  virtual UIEdgeInsets GetMaxViewportInsets() const = 0;

  // Returns the current insets for the visible content area's viewport.
  virtual UIEdgeInsets GetCurrentViewportInsets() const = 0;

  // Enters fullscreen mode, animating away toolbars and resetting the progress
  // to 0.0.  Calling this function while fullscreen is disabled has no effect.
  virtual void EnterFullscreen() = 0;

  // Needs to be cleanup.
  virtual void ExitFullscreen() = 0;

  // Exits fullscreen mode, animating in toolbars and resetting the progress to
  // 1.0.
  virtual void ExitFullscreen(FullscreenExitReason fullscreen_exit_reason) = 0;

  // Exits fullscreen without animation, resetting the progress to 1.0.
  virtual void ExitFullscreenWithoutAnimation() = 0;

  // Force fullscreen mode is used when the bottom omnibox is collapsed above
  // the keyboard or find-in-page is triggered. When the mode is active:
  // - Fullscreen progress is forced to 0 and should stay at 0.
  // - Updating browser insets if insets_update_enabled is true. (crbug/1490601)
  // When exiting the mode, fullscreen is reset.
  virtual bool IsForceFullscreenMode() const = 0;
  virtual void EnterForceFullscreenMode(bool insets_update_enabled) = 0;
  virtual void ExitForceFullscreenMode() = 0;

  // Force horizontal content resize, when content isn't tracking resize by
  // itself.
  virtual void ResizeHorizontalViewport() = 0;

 protected:
  FullscreenController(Browser* browser) : BrowserUserData(browser) {}

 private:
  friend class BrowserUserData<FullscreenController>;

  // Overload BrowserUserData<FullscreenController>::Create() since
  // FullscreenController is an abstract class and the factory needs
  // to create an instance of a sub-class.
  static std::unique_ptr<FullscreenController> Create(Browser* browser);
};

#endif  // IOS_CHROME_BROWSER_FULLSCREEN_UI_BUNDLED_FULLSCREEN_CONTROLLER_H_
