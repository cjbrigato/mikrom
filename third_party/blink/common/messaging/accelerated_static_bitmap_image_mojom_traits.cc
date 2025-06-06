// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/public/common/messaging/accelerated_static_bitmap_image_mojom_traits.h"

#include "components/viz/common/resources/shared_image_format_utils.h"
#include "gpu/command_buffer/common/shared_image_usage.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "ui/gfx/color_space.h"

namespace {

using Callback = base::OnceCallback<void(const gpu::SyncToken&)>;

// Implements mojom::ImageReleaseCallback.
// The passed in callback will be destroyed once the mojo pipe
// is destroyed or the callback is invoked via the Release interface
// call.
// It is required that desruction of the passed callback is enough to
// release the image. E.g. if the reference is bound to it.
class ReleaseCallbackImpl : public blink::mojom::ImageReleaseCallback {
 public:
  explicit ReleaseCallbackImpl(Callback callback)
      : callback_(std::move(callback)) {}

  void Release(const gpu::SyncToken& sync_token) override {
    std::move(callback_).Run(sync_token);
  }

 private:
  Callback callback_;
};

void Release(
    mojo::PendingRemote<blink::mojom::ImageReleaseCallback> pending_remote,
    const gpu::SyncToken& sync_token) {
  mojo::Remote<blink::mojom::ImageReleaseCallback> remote(
      std::move(pending_remote));
  remote->Release(sync_token);
}

}  // namespace

namespace mojo {

// static
mojo::PendingRemote<blink::mojom::ImageReleaseCallback> StructTraits<
    blink::mojom::AcceleratedStaticBitmapImage::DataView,
    blink::AcceleratedImageInfo>::release_callback(blink::AcceleratedImageInfo&
                                                       input) {
  mojo::PendingRemote<blink::mojom::ImageReleaseCallback> callback;
  MakeSelfOwnedReceiver(
      std::make_unique<ReleaseCallbackImpl>(std::move(input.release_callback)),
      callback.InitWithNewPipeAndPassReceiver());
  return callback;
}

bool StructTraits<blink::mojom::AcceleratedStaticBitmapImage::DataView,
                  blink::AcceleratedImageInfo>::
    Read(blink::mojom::AcceleratedStaticBitmapImage::DataView data,
         blink::AcceleratedImageInfo* out) {
  SkImageInfo image_info;
  if (!data.ReadSharedImage(&out->shared_image) ||
      !data.ReadSyncToken(&out->sync_token) ||
      !data.ReadImageInfo(&image_info)) {
    return false;
  }

  out->size = gfx::Size(image_info.width(), image_info.height());
  out->format =
      viz::SkColorTypeToSinglePlaneSharedImageFormat(image_info.colorType());
  out->alpha_type = image_info.alphaType();
  out->color_space = image_info.refColorSpace()
                         ? gfx::ColorSpace(*image_info.refColorSpace())
                         : gfx::ColorSpace::CreateSRGB();

  auto callback = data.TakeReleaseCallback<
      mojo::PendingRemote<blink::mojom::ImageReleaseCallback>>();
  if (!callback) {
    return false;
  }
  out->release_callback = base::BindOnce(&Release, std::move(callback));

  return true;
}

}  // namespace mojo
