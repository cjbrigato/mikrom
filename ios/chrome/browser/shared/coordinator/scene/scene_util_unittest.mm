// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/shared/coordinator/scene/scene_util.h"

#import <UIKit/UIKit.h>

#import "base/ios/ios_util.h"
#import "base/strings/sys_string_conversions.h"
#import "ios/chrome/browser/shared/coordinator/scene/scene_util_test_support.h"
#import "testing/platform_test.h"

using SceneUtilTest = PlatformTest;

// Tests that the identifier returned by SessionIdentifierForScene() for a
// Scene is either the UIKit identifier (if multi-scene is supported by the
// device) or a known constants otherwise.
TEST_F(SceneUtilTest, SessionIdentifierForScene) {
  NSString* identifier = [[NSUUID UUID] UUIDString];
  id scene = FakeSceneWithIdentifier(identifier);

  std::string expected = "{SyntheticIdentifier}";
  if (base::ios::IsMultipleScenesSupported()) {
    expected = base::SysNSStringToUTF8(identifier);
  }

  EXPECT_EQ(expected, SessionIdentifierForScene(scene));
}
