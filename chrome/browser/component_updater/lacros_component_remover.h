// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_COMPONENT_UPDATER_LACROS_COMPONENT_REMOVER_H_
#define CHROME_BROWSER_COMPONENT_UPDATER_LACROS_COMPONENT_REMOVER_H_

namespace base {
class FilePath;
}  // namespace base

namespace component_updater {

void DeleteStatefulLacros(const base::FilePath& user_data_dir);

}  // namespace component_updater

#endif  // CHROME_BROWSER_COMPONENT_UPDATER_LACROS_COMPONENT_REMOVER_H_
