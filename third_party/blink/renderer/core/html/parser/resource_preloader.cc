// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/html/parser/resource_preloader.h"

#include <utility>

namespace blink {

void ResourcePreloader::TakeAndPreload(PreloadRequestStream& r) {
  PreloadRequestStream requests;
  requests.swap(r);

  for (auto& request : requests) {
    Preload(std::move(request));
  }
}

}  // namespace blink
