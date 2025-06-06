// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/image/image_skia_util_mac.h"

#import <AppKit/AppKit.h>
#include <stddef.h>

#include <cmath>
#include <limits>
#include <memory>

#include "base/mac/mac_util.h"
#include "skia/ext/skia_utils_mac.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/resource/resource_scale_factor.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"

namespace {

// Returns true if the NSImage has no representations.
bool IsNSImageEmpty(NSImage* image) {
  return image.representations.count == 0;
}

}  // namespace

namespace gfx {

gfx::ImageSkia ImageSkiaFromNSImage(NSImage* image) {
  return ImageSkiaFromResizedNSImage(image, image.size);
}

gfx::ImageSkia ImageSkiaFromResizedNSImage(NSImage* image,
                                           NSSize desired_size) {
  // Resize and convert to ImageSkia simultaneously to save on computation.
  // TODO(pkotwicz): Separate resizing NSImage and converting to ImageSkia.
  // Convert to ImageSkia by finding the most appropriate NSImageRep for
  // each supported scale factor and resizing if necessary.

  if (IsNSImageEmpty(image))
    return gfx::ImageSkia();

  gfx::ImageSkia image_skia;
  const std::vector<ui::ResourceScaleFactor>& supported_scales =
      ui::GetSupportedResourceScaleFactors();
  for (const auto resource_scale : supported_scales) {
    const float scale = ui::GetScaleForResourceScaleFactor(resource_scale);
    NSAffineTransform* transform = [NSAffineTransform transform];
    [transform scaleBy:scale];
    NSDictionary<NSImageHintKey, id>* hints = @{NSImageHintCTM : transform};
    NSImageRep* best_match =
        [image bestRepresentationForRect:{NSZeroPoint, desired_size}
                                 context:NULL
                                   hints:hints];
    NSSize desired_size_for_scale =
        NSMakeSize(desired_size.width * scale, desired_size.height * scale);
    SkBitmap bitmap(
        skia::NSImageRepToSkBitmap(best_match, desired_size_for_scale, false));
    if (bitmap.isNull())
      continue;

    image_skia.AddRepresentation(gfx::ImageSkiaRep(bitmap, scale));
  }
  return image_skia;
}

NSImage* NSImageFromImageSkia(const gfx::ImageSkia& image_skia) {
  if (image_skia.isNull())
    return nil;

  NSImage* image = [[NSImage alloc] init];
  image_skia.EnsureRepsForSupportedScales();
  std::vector<gfx::ImageSkiaRep> image_reps = image_skia.image_reps();
  for (const auto& rep : image_reps) {
    [image addRepresentation:skia::SkBitmapToNSBitmapImageRep(rep.GetBitmap())];
  }

  image.size = NSMakeSize(image_skia.width(), image_skia.height());
  return image;
}

}  // namespace gfx
