// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/xr/xr_cube_map.h"

#include <algorithm>
#include <bit>
#include <cstring>

#include "base/bit_cast.h"
#include "device/vr/public/mojom/vr_service.mojom-blink.h"
#include "third_party/blink/renderer/modules/webgl/webgl_rendering_context_base.h"
#include "third_party/blink/renderer/modules/webgl/webgl_texture.h"
#include "third_party/blink/renderer/platform/bindings/exception_code.h"
#include "third_party/blink/renderer/platform/graphics/gpu/drawing_buffer.h"

namespace {

// This is an inversion of FloatToHalfFloat in ui/gfx/half_float.cc
float HalfFloatToFloat(const uint16_t input) {
  const uint32_t tmp = (input & 0x7fff) << 13 | (input & 0x8000) << 16;
  const float tmp2 = base::bit_cast<float>(tmp);
  return tmp2 / 1.9259299444e-34f;
}

// Linear to sRGB converstion as given in
// https://www.khronos.org/registry/OpenGL/extensions/ARB/ARB_framebuffer_sRGB.txt
uint8_t LinearToSrgb(float cl) {
  float cs = std::clamp(
      cl < 0.0031308f ? 12.92f * cl : 1.055f * std::pow(cl, 0.41666f) - 0.055f,
      0.0f, 1.0f);
  return static_cast<uint8_t>(255.0f * cs + 0.5f);
}

void Rgba16fToSrgba8(base::span<const uint16_t> input,
                     base::span<uint8_t> output) {
  CHECK_EQ(input.size(), output.size());
  CHECK_EQ(input.size() % 4, 0u);

  for (size_t i = 0; i < input.size(); ++i) {
    // Every fourth input element is the alpha channel, which should remain in
    // standard space.
    if ((i + 1) % 4 == 0) {
      // We won't support non-opaque alpha to make the conversion a bit faster.
      output[i] = 255;
    } else {
      output[i] = LinearToSrgb(HalfFloatToFloat(input[i]));
    }
  }
}

}  // namespace

namespace blink {

XRCubeMap::XRCubeMap(const device::mojom::blink::XRCubeMap& cube_map) {
  constexpr auto kNumComponentsPerPixel =
      device::mojom::blink::XRCubeMap::kNumComponentsPerPixel;
  static_assert(kNumComponentsPerPixel == 4,
                "XRCubeMaps are expected to be in the RGBA16F format");

  // Cube map sides must all be a power-of-two image. Note that we can skip the
  // division by |kNumComponentsPerPixel| at this time, since it is *also* a
  // power-of-two.
  bool valid = std::has_single_bit(cube_map.width_and_height);
  const size_t expected_size = cube_map.width_and_height *
                               cube_map.width_and_height *
                               kNumComponentsPerPixel;
  valid &= cube_map.positive_x.size() == expected_size;
  valid &= cube_map.negative_x.size() == expected_size;
  valid &= cube_map.positive_y.size() == expected_size;
  valid &= cube_map.negative_y.size() == expected_size;
  valid &= cube_map.positive_z.size() == expected_size;
  valid &= cube_map.negative_z.size() == expected_size;
  CHECK(valid);

  width_and_height_ = cube_map.width_and_height;
  positive_x_ = cube_map.positive_x;
  negative_x_ = cube_map.negative_x;
  positive_y_ = cube_map.positive_y;
  negative_y_ = cube_map.negative_y;
  positive_z_ = cube_map.positive_z;
  negative_z_ = cube_map.negative_z;
}

WebGLTexture* XRCubeMap::updateWebGLEnvironmentCube(
    WebGLRenderingContextBase* context,
    WebGLTexture* texture,
    GLenum internal_format,
    GLenum format,
    GLenum type) const {
  // Ensure a texture was supplied from the passed context and with an
  // appropriate bound target.
  DCHECK(texture);
  DCHECK(!texture->HasEverBeenBound() ||
         texture->GetTarget() == GL_TEXTURE_CUBE_MAP);

  auto* gl = context->ContextGL();
  texture->SetTarget(GL_TEXTURE_CUBE_MAP);
  gl->BindTexture(GL_TEXTURE_CUBE_MAP, texture->Object());

  // Cannot generate mip-maps for half-float textures, so use linear filtering
  gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  const std::array<base::span<const uint16_t>, 6> cubemap_images = {
      positive_x_, negative_x_, positive_y_,
      negative_y_, positive_z_, negative_z_,
  };
  const std::array<GLenum, 6> cubemap_targets = {
      GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
      GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
      GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
  };

  // Update image for each side of the cube map in the requested format,
  // either "srgb8" or "rgba16f".
  if (type == GL_UNSIGNED_BYTE) {
    // If we've been asked to provide the textures with UNSIGNED_BYTE
    // components it means the light probe was created with the "srgb8" format.
    // Since ARCore provides texture as half float components, we need to do a
    // conversion first to support this path.
    // TODO(https://crbug.com/1148605): Do conversions off the main JS thread.
    WTF::wtf_size_t component_count = width_and_height_ * width_and_height_ * 4;
    WTF::Vector<uint8_t> sRGB(component_count);
    for (int i = 0; i < 6; ++i) {
      Rgba16fToSrgba8(cubemap_images[i], sRGB);
      auto target = cubemap_targets[i];

      gl->TexImage2D(target, 0, internal_format, width_and_height_,
                     width_and_height_, 0, format, type, sRGB.data());
    }
  } else if (type == GL_HALF_FLOAT || type == GL_HALF_FLOAT_OES) {
    // If we've been asked to provide the textures with one of the HALF_FLOAT
    // types it means the light probe was created with the "rgba16f" format.
    // This is ARCore's native format, so no conversion is needed.
    for (int i = 0; i < 6; ++i) {
      auto image = cubemap_images[i];
      auto target = cubemap_targets[i];

      gl->TexImage2D(target, 0, internal_format, width_and_height_,
                     width_and_height_, 0, format, type, image.data());
    }
  } else {
    // No other formats are accepted.
    NOTREACHED();
  }

  DrawingBuffer::Client* client = static_cast<DrawingBuffer::Client*>(context);
  client->DrawingBufferClientRestoreTextureCubeMapBinding();

  // Debug check for success
  DCHECK(gl->GetError() == GL_NO_ERROR);

  return texture;
}

}  // namespace blink
