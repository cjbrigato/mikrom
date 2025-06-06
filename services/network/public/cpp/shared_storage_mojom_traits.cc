// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/public/cpp/shared_storage_mojom_traits.h"

#include "services/network/public/cpp/shared_storage_utils.h"

namespace mojo {

// static
bool StructTraits<
    network::mojom::SharedStorageKeyArgumentDataView,
    std::u16string>::Read(network::mojom::SharedStorageKeyArgumentDataView data,
                          std::u16string* out_key) {
  if (!data.ReadData(out_key)) {
    return false;
  }

  return network::IsValidSharedStorageKeyStringLength(out_key->size());
}

// static
bool StructTraits<network::mojom::SharedStorageValueArgumentDataView,
                  std::u16string>::
    Read(network::mojom::SharedStorageValueArgumentDataView data,
         std::u16string* out_value) {
  if (!data.ReadData(out_value)) {
    return false;
  }

  return network::IsValidSharedStorageValueStringLength(out_value->size());
}

// static
bool StructTraits<network::mojom::LockNameDataView, std::string>::Read(
    network::mojom::LockNameDataView data,
    std::string* out_value) {
  if (!data.ReadData(out_value)) {
    return false;
  }

  return !network::IsReservedLockName(*out_value);
}

// static
bool StructTraits<
    network::mojom::SharedStorageBatchUpdateMethodsArgumentDataView,
    std::vector<network::mojom::SharedStorageModifierMethodWithOptionsPtr>>::
    Read(network::mojom::SharedStorageBatchUpdateMethodsArgumentDataView data,
         std::vector<network::mojom::SharedStorageModifierMethodWithOptionsPtr>*
             out_value) {
  if (!data.ReadData(out_value)) {
    return false;
  }

  return network::IsValidSharedStorageBatchUpdateMethodsArgument(*out_value);
}

}  // namespace mojo
