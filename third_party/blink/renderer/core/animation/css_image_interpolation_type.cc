// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/animation/css_image_interpolation_type.h"

#include <memory>

#include "base/memory/ptr_util.h"
#include "base/memory/values_equivalent.h"
#include "third_party/blink/renderer/core/animation/underlying_value_owner.h"
#include "third_party/blink/renderer/core/css/css_crossfade_value.h"
#include "third_party/blink/renderer/core/css/css_numeric_literal_value.h"
#include "third_party/blink/renderer/core/css/css_primitive_value.h"
#include "third_party/blink/renderer/core/css/css_property_names.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver_state.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/core/style/style_image.h"

namespace blink {

namespace {
const StyleImage* GetStyleImage(const CSSProperty& property,
                                const ComputedStyle& style) {
  switch (property.PropertyID()) {
    case CSSPropertyID::kBorderImageSource:
      return style.BorderImageSource();
    case CSSPropertyID::kListStyleImage:
      return style.ListStyleImage().Get();
    case CSSPropertyID::kWebkitMaskBoxImageSource:
      return style.MaskBoxImageSource();
    default:
      NOTREACHED();
  }
}
}  // namespace

class CSSImageNonInterpolableValue final : public NonInterpolableValue {
 public:
  CSSImageNonInterpolableValue(CSSValue* start, CSSValue* end)
      : start_(start), end_(end), is_single_(start_ == end_) {
    DCHECK(start_);
    DCHECK(end_);
  }
  ~CSSImageNonInterpolableValue() final = default;

  void Trace(Visitor* visitor) const override {
    NonInterpolableValue::Trace(visitor);
    visitor->Trace(start_);
    visitor->Trace(end_);
  }

  bool IsSingle() const { return is_single_; }
  bool Equals(const CSSImageNonInterpolableValue& other) const {
    return base::ValuesEquivalent(start_, other.start_) &&
           base::ValuesEquivalent(end_, other.end_);
  }

  static CSSImageNonInterpolableValue* Merge(const NonInterpolableValue* start,
                                             const NonInterpolableValue* end);

  CSSValue* Crossfade(double progress) const {
    if (is_single_ || progress <= 0)
      return start_;
    if (progress >= 1)
      return end_;
    // https://drafts.csswg.org/css-images-4/#interpolating-images
    auto* progress_value = CSSNumericLiteralValue::Create(
        100.0 - progress * 100.0, CSSPrimitiveValue::UnitType::kPercentage);
    return MakeGarbageCollected<cssvalue::CSSCrossfadeValue>(
        /*is_prefixed_variant=*/false,
        HeapVector<std::pair<Member<CSSValue>, Member<CSSPrimitiveValue>>>{
            {start_, progress_value}, {end_, nullptr}});
  }

  DECLARE_NON_INTERPOLABLE_VALUE_TYPE();

 private:
  Member<CSSValue> start_;
  Member<CSSValue> end_;
  const bool is_single_;
};

DEFINE_NON_INTERPOLABLE_VALUE_TYPE(CSSImageNonInterpolableValue);
template <>
struct DowncastTraits<CSSImageNonInterpolableValue> {
  static bool AllowFrom(const NonInterpolableValue* value) {
    return value && AllowFrom(*value);
  }
  static bool AllowFrom(const NonInterpolableValue& value) {
    return value.GetType() == CSSImageNonInterpolableValue::static_type_;
  }
};

CSSImageNonInterpolableValue* CSSImageNonInterpolableValue::Merge(
    const NonInterpolableValue* start,
    const NonInterpolableValue* end) {
  const auto& start_image_pair = To<CSSImageNonInterpolableValue>(*start);
  const auto& end_image_pair = To<CSSImageNonInterpolableValue>(*end);
  DCHECK(start_image_pair.is_single_);
  DCHECK(end_image_pair.is_single_);
  return MakeGarbageCollected<CSSImageNonInterpolableValue>(
      start_image_pair.start_, end_image_pair.end_);
}

InterpolationValue CSSImageInterpolationType::MaybeConvertStyleImage(
    const StyleImage& style_image,
    bool accept_gradients) {
  return MaybeConvertCSSValue(*style_image.CssValue(), accept_gradients);
}

InterpolationValue CSSImageInterpolationType::MaybeConvertCSSValue(
    const CSSValue& value,
    bool accept_gradients) {
  if (value.IsImageValue() || (value.IsGradientValue() && accept_gradients)) {
    CSSValue* refable_css_value = const_cast<CSSValue*>(&value);
    return InterpolationValue(
        MakeGarbageCollected<InterpolableNumber>(1),
        MakeGarbageCollected<CSSImageNonInterpolableValue>(refable_css_value,
                                                           refable_css_value));
  }
  return nullptr;
}

PairwiseInterpolationValue
CSSImageInterpolationType::StaticMergeSingleConversions(
    InterpolationValue&& start,
    InterpolationValue&& end) {
  if (!To<CSSImageNonInterpolableValue>(*start.non_interpolable_value)
           .IsSingle() ||
      !To<CSSImageNonInterpolableValue>(*end.non_interpolable_value)
           .IsSingle()) {
    return nullptr;
  }
  return PairwiseInterpolationValue(
      MakeGarbageCollected<InterpolableNumber>(0),
      MakeGarbageCollected<InterpolableNumber>(1),
      CSSImageNonInterpolableValue::Merge(start.non_interpolable_value,
                                          end.non_interpolable_value));
}

const CSSValue* CSSImageInterpolationType::CreateCSSValue(
    const InterpolableValue& interpolable_value,
    const NonInterpolableValue* non_interpolable_value,
    const StyleResolverState& state) const {
  return StaticCreateCSSValue(interpolable_value, non_interpolable_value,
                              state.CssToLengthConversionData());
}

const CSSValue* CSSImageInterpolationType::StaticCreateCSSValue(
    const InterpolableValue& interpolable_value,
    const NonInterpolableValue* non_interpolable_value,
    const CSSLengthResolver& length_resolver) {
  // TODO(crbug.com/325821290): Avoid InterpolableNumber here.
  return To<CSSImageNonInterpolableValue>(non_interpolable_value)
      ->Crossfade(
          To<InterpolableNumber>(interpolable_value).Value(length_resolver));
}

StyleImage* CSSImageInterpolationType::ResolveStyleImage(
    const CSSProperty& property,
    const InterpolableValue& interpolable_value,
    const NonInterpolableValue* non_interpolable_value,
    StyleResolverState& state) {
  const CSSValue* image =
      StaticCreateCSSValue(interpolable_value, non_interpolable_value,
                           state.CssToLengthConversionData());
  return state.GetStyleImage(property.PropertyID(), *image);
}

bool CSSImageInterpolationType::EqualNonInterpolableValues(
    const NonInterpolableValue* a,
    const NonInterpolableValue* b) {
  return To<CSSImageNonInterpolableValue>(*a).Equals(
      To<CSSImageNonInterpolableValue>(*b));
}

class UnderlyingImageChecker final
    : public CSSInterpolationType::CSSConversionChecker {
 public:
  UnderlyingImageChecker(const InterpolationValue& underlying)
      : underlying_(MakeGarbageCollected<InterpolationValueGCed>(underlying)) {}
  ~UnderlyingImageChecker() final = default;

  void Trace(Visitor* visitor) const final {
    CSSConversionChecker::Trace(visitor);
    visitor->Trace(underlying_);
  }

 private:
  bool IsValid(const StyleResolverState&,
               const InterpolationValue& underlying) const final {
    if (!underlying && !underlying_) {
      return true;
    }
    if (!underlying || !underlying_) {
      return false;
    }
    return underlying_->underlying().interpolable_value->Equals(
               *underlying.interpolable_value) &&
           CSSImageInterpolationType::EqualNonInterpolableValues(
               underlying_->underlying().non_interpolable_value.Get(),
               underlying.non_interpolable_value.Get());
  }

  const Member<InterpolationValueGCed> underlying_;
};

InterpolationValue CSSImageInterpolationType::MaybeConvertNeutral(
    const InterpolationValue& underlying,
    ConversionCheckers& conversion_checkers) const {
  conversion_checkers.push_back(
      MakeGarbageCollected<UnderlyingImageChecker>(underlying));
  return InterpolationValue(underlying.Clone());
}

InterpolationValue CSSImageInterpolationType::MaybeConvertInitial(
    const StyleResolverState&,
    ConversionCheckers& conversion_checkers) const {
  return nullptr;
}

class InheritedImageChecker final
    : public CSSInterpolationType::CSSConversionChecker {
 public:
  InheritedImageChecker(const CSSProperty& property,
                        StyleImage* inherited_image)
      : property_(property), inherited_image_(inherited_image) {}
  ~InheritedImageChecker() final = default;

  void Trace(Visitor* visitor) const final {
    CSSConversionChecker::Trace(visitor);
    visitor->Trace(inherited_image_);
  }

 private:
  bool IsValid(const StyleResolverState& state,
               const InterpolationValue& underlying) const final {
    const StyleImage* inherited_image =
        GetStyleImage(property_, *state.ParentStyle());
    if (!inherited_image_ && !inherited_image)
      return true;
    if (!inherited_image_ || !inherited_image)
      return false;
    return *inherited_image_ == *inherited_image;
  }

  const CSSProperty& property_;
  Member<StyleImage> inherited_image_;
};

InterpolationValue CSSImageInterpolationType::MaybeConvertInherit(
    const StyleResolverState& state,
    ConversionCheckers& conversion_checkers) const {
  if (!state.ParentStyle())
    return nullptr;

  const StyleImage* inherited_image =
      GetStyleImage(CssProperty(), *state.ParentStyle());
  StyleImage* refable_image = const_cast<StyleImage*>(inherited_image);
  conversion_checkers.push_back(MakeGarbageCollected<InheritedImageChecker>(
      CssProperty(), refable_image));
  return MaybeConvertStyleImage(inherited_image, true);
}

InterpolationValue CSSImageInterpolationType::MaybeConvertValue(
    const CSSValue& value,
    const StyleResolverState&,
    ConversionCheckers&) const {
  return MaybeConvertCSSValue(value, true);
}

InterpolationValue
CSSImageInterpolationType::MaybeConvertStandardPropertyUnderlyingValue(
    const ComputedStyle& style) const {
  return MaybeConvertStyleImage(GetStyleImage(CssProperty(), style), true);
}

void CSSImageInterpolationType::Composite(
    UnderlyingValueOwner& underlying_value_owner,
    double underlying_fraction,
    const InterpolationValue& value,
    double interpolation_fraction) const {
  underlying_value_owner.Set(this, value);
}

void CSSImageInterpolationType::ApplyStandardPropertyValue(
    const InterpolableValue& interpolable_value,
    const NonInterpolableValue* non_interpolable_value,
    StyleResolverState& state) const {
  StyleImage* image = ResolveStyleImage(CssProperty(), interpolable_value,
                                        non_interpolable_value, state);
  switch (CssProperty().PropertyID()) {
    case CSSPropertyID::kBorderImageSource:
      state.StyleBuilder().SetBorderImageSource(image);
      break;
    case CSSPropertyID::kListStyleImage:
      state.StyleBuilder().SetListStyleImage(image);
      break;
    case CSSPropertyID::kWebkitMaskBoxImageSource:
      state.StyleBuilder().SetMaskBoxImageSource(image);
      break;
    default:
      NOTREACHED();
  }
}

}  // namespace blink
