// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_SHARED_PUBLIC_COMMANDS_BWG_COMMANDS_H_
#define IOS_CHROME_BROWSER_SHARED_PUBLIC_COMMANDS_BWG_COMMANDS_H_

// Commands relating to the BWG flow.
@protocol BWGCommands

// Starts the BWG flow.
- (void)startBWGFlow;

// Dismiss the BWG flow.
- (void)dismissBWGFlow;

@end

#endif  // IOS_CHROME_BROWSER_SHARED_PUBLIC_COMMANDS_BWG_COMMANDS_H_
