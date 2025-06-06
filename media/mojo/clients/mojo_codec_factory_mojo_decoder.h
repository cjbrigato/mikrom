// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_MOJO_CLIENTS_MOJO_CODEC_FACTORY_MOJO_DECODER_H_
#define MEDIA_MOJO_CLIENTS_MOJO_CODEC_FACTORY_MOJO_DECODER_H_

#include <memory>
#include <variant>

#include "base/task/sequenced_task_runner.h"
#include "media/base/decoder.h"
#include "media/base/overlay_info.h"
#include "media/base/video_decoder.h"
#include "media/mojo/clients/mojo_codec_factory.h"
#include "media/mojo/mojom/interface_factory.mojom.h"
#include "media/mojo/mojom/video_decoder.mojom.h"
#include "media/video/gpu_video_accelerator_factories.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace media {

// MojoCodecFactoryMojoDecoder gets hardware decoder resources
// via media::mojom::InterfaceFactory. Use it when mojo-based video decoder is
// enabled.
class MojoCodecFactoryMojoDecoder final : public media::MojoCodecFactory {
 public:
  MojoCodecFactoryMojoDecoder(
      scoped_refptr<base::SequencedTaskRunner> media_task_runner,
      scoped_refptr<viz::ContextProviderCommandBuffer> context_provider,
      bool video_decode_accelerator_enabled,
      bool video_encode_accelerator_enabled,
      mojo::PendingRemote<media::mojom::VideoEncodeAcceleratorProvider>
          pending_vea_provider_remote,
      mojo::PendingRemote<media::mojom::InterfaceFactory>
          pending_interface_factory_remote);
  ~MojoCodecFactoryMojoDecoder() override;

  std::unique_ptr<media::VideoDecoder> CreateVideoDecoder(
      media::GpuVideoAcceleratorFactories* gpu_factories,
      media::MediaLog* media_log,
      media::RequestOverlayInfoCB request_overlay_info_cb,
      const gfx::ColorSpace& rendering_color_space) override;

 private:
  void BindOnTaskRunner(
      mojo::PendingRemote<media::mojom::InterfaceFactory> interface_factory);
  void OnGetSupportedDecoderConfigs(
      const media::SupportedVideoDecoderConfigs& supported_configs,
      media::VideoDecoderType decoder_type);

  mojo::Remote<media::mojom::InterfaceFactory> interface_factory_;

  mojo::Remote<media::mojom::VideoDecoder> video_decoder_;
};

}  // namespace media

#endif  // MEDIA_MOJO_CLIENTS_MOJO_CODEC_FACTORY_MOJO_DECODER_H_
