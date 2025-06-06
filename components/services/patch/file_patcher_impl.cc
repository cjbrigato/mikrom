// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/services/patch/file_patcher_impl.h"

#include <utility>

#include "base/files/file.h"
#include "base/functional/callback.h"
#include "components/services/patch/public/mojom/file_patcher.mojom.h"
#include "components/zucchini/zucchini_integration.h"
#include "third_party/puffin/src/include/puffin/puffpatch.h"

namespace patch {

FilePatcherImpl::FilePatcherImpl() = default;

FilePatcherImpl::FilePatcherImpl(
    mojo::PendingReceiver<mojom::FilePatcher> receiver)
    : receiver_(this, std::move(receiver)) {}

FilePatcherImpl::~FilePatcherImpl() = default;

void FilePatcherImpl::PatchFilePuffPatch(base::File input_file,
                                         base::File patch_file,
                                         base::File output_file,
                                         PatchFilePuffPatchCallback callback) {
  const int patch_result_status = puffin::ApplyPuffPatch(
      std::move(input_file), std::move(patch_file), std::move(output_file));
  std::move(callback).Run(patch_result_status);
}

void FilePatcherImpl::PatchFileZucchini(
    base::File input_file,
    base::File patch_file,
    base::File output_file,
    base::OnceCallback<void(zucchini::status::Code)> callback) {
  std::move(callback).Run(zucchini::Apply(
      std::move(input_file), std::move(patch_file), std::move(output_file)));
}

}  // namespace patch
