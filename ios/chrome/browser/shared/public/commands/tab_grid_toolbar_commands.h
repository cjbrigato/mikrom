// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_SHARED_PUBLIC_COMMANDS_TAB_GRID_TOOLBAR_COMMANDS_H_
#define IOS_CHROME_BROWSER_SHARED_PUBLIC_COMMANDS_TAB_GRID_TOOLBAR_COMMANDS_H_

#include "base/ios/block_types.h"

// Protocol used to send commands to the tab grid toolbars.
@protocol TabGridToolbarCommands

// Display an IPH to advertise the Tab Group view.
- (void)showSavedTabGroupIPH;

// Display the Guided Tour step that highlights the Incognito tab. `completion`
// will be executed after the step dismisses.
- (void)showGuidedTourIncognitoStepWithDismissalCompletion:
    (ProceduralBlock)completion;
- (void)showGuidedTourTabGroupStepWithDismissalCompletion:
    (ProceduralBlock)completion;

@end

#endif  // IOS_CHROME_BROWSER_SHARED_PUBLIC_COMMANDS_TAB_GRID_TOOLBAR_COMMANDS_H_
