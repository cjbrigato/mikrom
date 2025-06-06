/*
 * Copyright (C) 2007, 2010 Rob Buis <buis@kde.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "third_party/blink/renderer/core/svg/svg_view_spec.h"

#include "base/containers/span.h"
#include "third_party/blink/renderer/core/svg/svg_animated_preserve_aspect_ratio.h"
#include "third_party/blink/renderer/core/svg/svg_animated_rect.h"
#include "third_party/blink/renderer/core/svg/svg_parser_utilities.h"
#include "third_party/blink/renderer/core/svg/svg_preserve_aspect_ratio.h"
#include "third_party/blink/renderer/core/svg/svg_rect.h"
#include "third_party/blink/renderer/core/svg/svg_transform_list.h"
#include "third_party/blink/renderer/core/svg/svg_view_element.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/wtf/text/character_visitor.h"
#include "third_party/blink/renderer/platform/wtf/text/parsing_utilities.h"

namespace blink {

void SVGViewSpec::Trace(Visitor* visitor) const {
  visitor->Trace(view_box_);
  visitor->Trace(preserve_aspect_ratio_);
  visitor->Trace(transform_);
}

const SVGViewSpec* SVGViewSpec::CreateFromFragment(const String& fragment) {
  SVGViewSpec* view_spec = MakeGarbageCollected<SVGViewSpec>();
  if (!view_spec->ParseViewSpec(fragment))
    return nullptr;
  return view_spec;
}

const SVGViewSpec* SVGViewSpec::CreateForViewElement(
    const SVGViewElement& view) {
  SVGViewSpec* view_spec = MakeGarbageCollected<SVGViewSpec>();
  if (view.HasValidViewBox())
    view_spec->view_box_ = view.viewBox()->CurrentValue()->Clone();
  if (view.preserveAspectRatio()->IsSpecified()) {
    view_spec->preserve_aspect_ratio_ =
        view.preserveAspectRatio()->CurrentValue()->Clone();
  }
  if (view.hasAttribute(svg_names::kZoomAndPanAttr))
    view_spec->zoom_and_pan_ = view.zoomAndPan();
  return view_spec;
}

const SVGViewSpec* SVGViewSpec::CreateFromAspectRatio(
    const SVGPreserveAspectRatio* preserve_aspect_ratio) {
  if (!preserve_aspect_ratio) {
    return nullptr;
  }
  SVGViewSpec* view_spec = MakeGarbageCollected<SVGViewSpec>();
  view_spec->preserve_aspect_ratio_ = preserve_aspect_ratio;
  return view_spec;
}

bool SVGViewSpec::ParseViewSpec(const String& spec) {
  if (spec.empty())
    return false;
  return WTF::VisitCharacters(
      spec, [&](auto chars) { return ParseViewSpecInternal(chars); });
}

namespace {

enum ViewSpecFunctionType {
  kUnknown,
  kPreserveAspectRatio,
  kTransform,
  kViewBox,
  kViewTarget,
  kZoomAndPan,
};

template <typename CharType>
static ViewSpecFunctionType ScanViewSpecFunction(const CharType*& ptr,
                                                 const CharType* end) {
  DCHECK_LT(ptr, end);
  switch (*ptr) {
    case 'v':
      if (UNSAFE_TODO(SkipToken(ptr, end, "viewBox"))) {
        return kViewBox;
      }
      if (UNSAFE_TODO(SkipToken(ptr, end, "viewTarget"))) {
        return kViewTarget;
      }
      break;
    case 'z':
      if (UNSAFE_TODO(SkipToken(ptr, end, "zoomAndPan"))) {
        return kZoomAndPan;
      }
      break;
    case 'p':
      if (UNSAFE_TODO(SkipToken(ptr, end, "preserveAspectRatio"))) {
        return kPreserveAspectRatio;
      }
      break;
    case 't':
      if (UNSAFE_TODO(SkipToken(ptr, end, "transform"))) {
        return kTransform;
      }
      break;
  }
  return kUnknown;
}

}  // namespace

template <typename CharType>
bool SVGViewSpec::ParseViewSpecInternal(base::span<const CharType> chars) {
  const CharType* ptr = chars.data();
  const CharType* end = UNSAFE_TODO(ptr + chars.size());
  if (!UNSAFE_TODO(SkipToken(ptr, end, "svgView"))) {
    return false;
  }

  size_t position = ptr - chars.data();
  if (!SkipExactly<CharType>(chars, '(', position)) {
    return false;
  }
  ptr = UNSAFE_TODO(chars.data() + position);

  while (ptr < end && *ptr != ')') {
    ViewSpecFunctionType function_type = ScanViewSpecFunction(ptr, end);
    if (function_type == kUnknown)
      return false;

    position = ptr - chars.data();
    if (!SkipExactly<CharType>(chars, '(', position)) {
      return false;
    }
    ptr = UNSAFE_TODO(chars.data() + position);

    switch (function_type) {
      case kViewBox: {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        if (!(ParseNumber(ptr, end, x) && ParseNumber(ptr, end, y) &&
              ParseNumber(ptr, end, width) &&
              ParseNumber(ptr, end, height, kDisallowWhitespace)))
          return false;
        if (width < 0 || height < 0)
          return false;
        view_box_ = MakeGarbageCollected<SVGRect>(x, y, width, height);
        break;
      }
      case kViewTarget: {
        // Ignore arguments.
        UNSAFE_TODO(SkipUntil<CharType>(ptr, end, ')'));
        break;
      }
      case kZoomAndPan:
        zoom_and_pan_ = SVGZoomAndPan::Parse(ptr, end);
        if (zoom_and_pan_ == kSVGZoomAndPanUnknown)
          return false;
        break;
      case kPreserveAspectRatio: {
        auto* preserve_aspect_ratio =
            MakeGarbageCollected<SVGPreserveAspectRatio>();
        // TODO(crbug.com/351564777): This file is relying on the
        // behavior of the `preserve_aspect_ratio->Parse` mutating the
        // first argument. Set ptr to span.data() for now.
        auto span = UNSAFE_TODO(base::span(ptr, end));
        if (!preserve_aspect_ratio->Parse(span, false)) {
          return false;
        }
        ptr = span.data();
        preserve_aspect_ratio_ = preserve_aspect_ratio;
        break;
      }
      case kTransform: {
        auto* transform = MakeGarbageCollected<SVGTransformList>();
        transform->Parse(ptr, end);
        transform_ = transform;
        break;
      }
      default:
        NOTREACHED();
    }

    position = ptr - chars.data();
    if (!SkipExactly<CharType>(chars, ')', position)) {
      return false;
    }

    SkipExactly<CharType>(chars, ';', position);
    ptr = UNSAFE_TODO(chars.data() + position);
  }
  return SkipExactly<CharType>(chars, ')', position);
}

}  // namespace blink
