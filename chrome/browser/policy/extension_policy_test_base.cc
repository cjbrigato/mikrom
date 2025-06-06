// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/extension_policy_test_base.h"

#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/policy/policy_test_utils.h"
#include "chrome/browser/profiles/profile.h"

namespace policy {

const base::FilePath::CharType kTestExtensionsDir[] =
    FILE_PATH_LITERAL("extensions");

ExtensionPolicyTestBase::ExtensionPolicyTestBase() = default;

ExtensionPolicyTestBase::~ExtensionPolicyTestBase() = default;

scoped_refptr<const extensions::Extension>
ExtensionPolicyTestBase::LoadUnpackedExtension(
    const base::FilePath::StringType& name) {
  base::FilePath extension_path =
      GetTestFilePath(base::FilePath(kTestExtensionsDir), base::FilePath(name));
  auto* profile = chrome_test_utils::GetProfile(this);
  extensions::ChromeTestExtensionLoader loader(profile);
  return loader.LoadExtension(extension_path);
}

}  // namespace policy
