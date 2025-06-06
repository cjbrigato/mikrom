// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_ASH_EXPERIENCES_ARC_FILE_SYSTEM_FILE_SYSTEM_MOJOM_TRAITS_H_
#define CHROMEOS_ASH_EXPERIENCES_ARC_FILE_SYSTEM_FILE_SYSTEM_MOJOM_TRAITS_H_

#include "chromeos/ash/experiences/arc/mojom/file_system.mojom-shared.h"
#include "storage/browser/file_system/watcher_manager.h"

namespace mojo {

template <>
struct EnumTraits<arc::mojom::ChangeType, storage::WatcherManager::ChangeType> {
  static arc::mojom::ChangeType ToMojom(
      storage::WatcherManager::ChangeType type);
  static bool FromMojom(arc::mojom::ChangeType mojom_type,
                        storage::WatcherManager::ChangeType* type);
};

}  // namespace mojo

#endif  // CHROMEOS_ASH_EXPERIENCES_ARC_FILE_SYSTEM_FILE_SYSTEM_MOJOM_TRAITS_H_
