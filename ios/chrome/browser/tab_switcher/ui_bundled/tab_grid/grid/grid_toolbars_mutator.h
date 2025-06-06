// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_TAB_SWITCHER_UI_BUNDLED_TAB_GRID_GRID_GRID_TOOLBARS_MUTATOR_H_
#define IOS_CHROME_BROWSER_TAB_SWITCHER_UI_BUNDLED_TAB_GRID_GRID_GRID_TOOLBARS_MUTATOR_H_

#import <Foundation/Foundation.h>

#import "ios/chrome/browser/tab_switcher/ui_bundled/tab_grid/tab_grid_paging.h"

@class TabGridToolbarsConfiguration;
@protocol TabGridToolbarsGridDelegate;

// Allows grids mediator to reflect toolbars needs in tab grid toolbars' model.
@protocol GridToolbarsMutator <NSObject>

// Sends to tab grid toolbars model the needed toolbar configuration.
- (void)setToolbarConfiguration:(TabGridToolbarsConfiguration*)configuration;

// Sends to tab grid toolbars model which delegates should handle buttons'
// actions.
- (void)setToolbarsButtonsDelegate:(id<TabGridToolbarsGridDelegate>)delegate;

// Sends NO to disable all buttons and save the current configuration or YES to
// enable the saved configuration.
- (void)setButtonsEnabled:(BOOL)enabled;

// Sends YES to hide toolbars on the incognito page on the tab switcher when
// authentication is required to give the full screen blur effect, NO to reshow
// toolbars.
- (void)setIncognitoToolbarsBackgroundHidden:(BOOL)hidden;

@end

#endif  // IOS_CHROME_BROWSER_TAB_SWITCHER_UI_BUNDLED_TAB_GRID_GRID_GRID_TOOLBARS_MUTATOR_H_
