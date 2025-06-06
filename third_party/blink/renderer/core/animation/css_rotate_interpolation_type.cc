// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/animation/css_rotate_interpolation_type.h"

#include <memory>

#include "base/memory/ptr_util.h"
#include "third_party/blink/renderer/core/animation/length_units_checker.h"
#include "third_party/blink/renderer/core/animation/tree_counting_checker.h"
#include "third_party/blink/renderer/core/animation/underlying_value_owner.h"
#include "third_party/blink/renderer/core/css/css_axis_value.h"
#include "third_party/blink/renderer/core/css/css_primitive_value.h"
#include "third_party/blink/renderer/core/css/resolver/style_builder_converter.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/platform/transforms/rotate_transform_operation.h"
#include "third_party/blink/renderer/platform/transforms/rotation.h"

namespace blink {

class OptionalRotation {
 public:
  OptionalRotation() : is_none_(true) {}

  explicit OptionalRotation(Rotation rotation)
      : rotation_(rotation), is_none_(false) {}

  bool IsNone() const { return is_none_; }
  const Rotation& GetRotation() const {
    DCHECK(!is_none_);
    return rotation_;
  }

  static OptionalRotation Add(const OptionalRotation& a,
                              const OptionalRotation& b) {
    if (a.IsNone())
      return b;
    if (b.IsNone())
      return a;
    return OptionalRotation(Rotation::Add(a.GetRotation(), b.GetRotation()));
  }
  static OptionalRotation Slerp(const OptionalRotation& from,
                                const OptionalRotation& to,
                                double progress) {
    if (from.IsNone() && to.IsNone())
      return OptionalRotation();

    return OptionalRotation(
        Rotation::Slerp(from.IsNone() ? Rotation() : from.GetRotation(),
                        to.IsNone() ? Rotation() : to.GetRotation(), progress));
  }

 private:
  Rotation rotation_;
  bool is_none_;
};

class CSSRotateNonInterpolableValue : public NonInterpolableValue {
 public:
  CSSRotateNonInterpolableValue(bool is_single,
                                const OptionalRotation& start,
                                const OptionalRotation& end,
                                bool is_start_additive,
                                bool is_end_additive)
      : is_single_(is_single),
        start_(start),
        end_(end),
        is_start_additive_(is_start_additive),
        is_end_additive_(is_end_additive) {}

  static CSSRotateNonInterpolableValue* Create(
      const OptionalRotation& rotation) {
    return MakeGarbageCollected<CSSRotateNonInterpolableValue>(
        true, rotation, OptionalRotation(), false, false);
  }

  static CSSRotateNonInterpolableValue* Create(
      const CSSRotateNonInterpolableValue& start,
      const CSSRotateNonInterpolableValue& end) {
    return MakeGarbageCollected<CSSRotateNonInterpolableValue>(
        false, start.GetOptionalRotation(), end.GetOptionalRotation(),
        start.IsAdditive(), end.IsAdditive());
  }

  static CSSRotateNonInterpolableValue* CreateAdditive(
      const CSSRotateNonInterpolableValue& other) {
    DCHECK(other.is_single_);
    const bool is_single = true;
    const bool is_additive = true;
    return MakeGarbageCollected<CSSRotateNonInterpolableValue>(
        is_single, other.start_, other.end_, is_additive, is_additive);
  }

  CSSRotateNonInterpolableValue* Composite(
      const CSSRotateNonInterpolableValue& other,
      double other_progress) const {
    DCHECK(is_single_ && !is_start_additive_);
    if (other.is_single_) {
      DCHECK_EQ(other_progress, 0);
      DCHECK(other.IsAdditive());
      return Create(OptionalRotation::Add(GetOptionalRotation(),
                                          other.GetOptionalRotation()));
    }

    DCHECK(other.is_start_additive_ || other.is_end_additive_);
    OptionalRotation start =
        other.is_start_additive_
            ? OptionalRotation::Add(GetOptionalRotation(), other.start_)
            : other.start_;
    OptionalRotation end =
        other.is_end_additive_
            ? OptionalRotation::Add(GetOptionalRotation(), other.end_)
            : other.end_;
    return Create(OptionalRotation::Slerp(start, end, other_progress));
  }

  OptionalRotation SlerpedRotation(double progress) const {
    DCHECK(!is_start_additive_ && !is_end_additive_);
    DCHECK(!is_single_ || progress == 0);
    return OptionalRotation::Slerp(start_, end_, progress);
  }

  DECLARE_NON_INTERPOLABLE_VALUE_TYPE();

 private:
  const OptionalRotation& GetOptionalRotation() const {
    DCHECK(is_single_);
    return start_;
  }
  bool IsAdditive() const {
    DCHECK(is_single_);
    return is_start_additive_;
  }

  bool is_single_;
  OptionalRotation start_;
  OptionalRotation end_;
  bool is_start_additive_;
  bool is_end_additive_;
};

DEFINE_NON_INTERPOLABLE_VALUE_TYPE(CSSRotateNonInterpolableValue);
template <>
struct DowncastTraits<CSSRotateNonInterpolableValue> {
  static bool AllowFrom(const NonInterpolableValue* value) {
    return value && AllowFrom(*value);
  }
  static bool AllowFrom(const NonInterpolableValue& value) {
    return value.GetType() == CSSRotateNonInterpolableValue::static_type_;
  }
};

namespace {

OptionalRotation GetRotation(const ComputedStyle& style) {
  if (!style.Rotate())
    return OptionalRotation();
  return OptionalRotation(
      Rotation(style.Rotate()->Axis(), style.Rotate()->Angle()));
}

InterpolationValue ConvertRotation(const OptionalRotation& rotation) {
  return InterpolationValue(MakeGarbageCollected<InterpolableNumber>(0),
                            CSSRotateNonInterpolableValue::Create(rotation));
}

class InheritedRotationChecker
    : public CSSInterpolationType::CSSConversionChecker {
 public:
  explicit InheritedRotationChecker(const OptionalRotation& inherited_rotation)
      : inherited_rotation_(inherited_rotation) {}

  bool IsValid(const StyleResolverState& state,
               const InterpolationValue& underlying) const final {
    OptionalRotation inherited_rotation = GetRotation(*state.ParentStyle());
    if (inherited_rotation_.IsNone() || inherited_rotation.IsNone())
      return inherited_rotation_.IsNone() == inherited_rotation.IsNone();
    return inherited_rotation_.GetRotation().axis ==
               inherited_rotation.GetRotation().axis &&
           inherited_rotation_.GetRotation().angle ==
               inherited_rotation.GetRotation().angle;
  }

 private:
  const OptionalRotation inherited_rotation_;
};

}  // namespace

InterpolationValue CSSRotateInterpolationType::MaybeConvertNeutral(
    const InterpolationValue& underlying,
    ConversionCheckers&) const {
  return ConvertRotation(OptionalRotation(Rotation()));
}

InterpolationValue CSSRotateInterpolationType::MaybeConvertInitial(
    const StyleResolverState&,
    ConversionCheckers&) const {
  return ConvertRotation(OptionalRotation());
}

InterpolationValue CSSRotateInterpolationType::MaybeConvertInherit(
    const StyleResolverState& state,
    ConversionCheckers& conversion_checkers) const {
  OptionalRotation inherited_rotation = GetRotation(*state.ParentStyle());
  conversion_checkers.push_back(
      MakeGarbageCollected<InheritedRotationChecker>(inherited_rotation));
  return ConvertRotation(inherited_rotation);
}

InterpolationValue CSSRotateInterpolationType::MaybeConvertValue(
    const CSSValue& value,
    const StyleResolverState& state,
    ConversionCheckers& conversion_checkers) const {
  if (value.IsIdentifierValue()) {
    // 'none'
    return ConvertRotation(OptionalRotation());
  }

  const CSSLengthResolver& length_resolver = state.CssToLengthConversionData();
  const CSSValueList& list = To<CSSValueList>(value);
  bool needs_tree_counting_checker = false;
  CSSPrimitiveValue::LengthTypeFlags types;
  if (list.length() == 2) {
    for (const CSSValue* axis_component :
         To<cssvalue::CSSAxisValue>(list.Item(0))) {
      const auto* primitive_value = To<CSSPrimitiveValue>(axis_component);
      primitive_value->AccumulateLengthUnitTypes(types);
      if (primitive_value->IsElementDependent()) {
        needs_tree_counting_checker = true;
        break;
      }
    }
  }
  const auto& primitive_value =
      To<CSSPrimitiveValue>(list.Item(list.length() - 1));
  primitive_value.AccumulateLengthUnitTypes(types);
  if (needs_tree_counting_checker || primitive_value.IsElementDependent()) {
    conversion_checkers.push_back(TreeCountingChecker::Create(length_resolver));
  }
  if (InterpolationType::ConversionChecker* length_units_checker =
          LengthUnitsChecker::MaybeCreate(types, state)) {
    conversion_checkers.push_back(length_units_checker);
  }

  return ConvertRotation(OptionalRotation(
      StyleBuilderConverter::ConvertRotation(length_resolver, value)));
}

InterpolationValue
CSSRotateInterpolationType::PreInterpolationCompositeIfNeeded(
    InterpolationValue value,
    const InterpolationValue& underlying,
    EffectModel::CompositeOperation,
    ConversionCheckers&) const {
  value.non_interpolable_value = CSSRotateNonInterpolableValue::CreateAdditive(
      To<CSSRotateNonInterpolableValue>(*value.non_interpolable_value));
  return value;
}

PairwiseInterpolationValue CSSRotateInterpolationType::MaybeMergeSingles(
    InterpolationValue&& start,
    InterpolationValue&& end) const {
  return PairwiseInterpolationValue(
      MakeGarbageCollected<InterpolableNumber>(0),
      MakeGarbageCollected<InterpolableNumber>(1),
      CSSRotateNonInterpolableValue::Create(
          To<CSSRotateNonInterpolableValue>(*start.non_interpolable_value),
          To<CSSRotateNonInterpolableValue>(*end.non_interpolable_value)));
}

InterpolationValue
CSSRotateInterpolationType::MaybeConvertStandardPropertyUnderlyingValue(
    const ComputedStyle& style) const {
  return ConvertRotation(GetRotation(style));
}

void CSSRotateInterpolationType::Composite(
    UnderlyingValueOwner& underlying_value_owner,
    double underlying_fraction,
    const InterpolationValue& value,
    double interpolation_fraction) const {
  const auto& underlying_non_interpolable_value =
      To<CSSRotateNonInterpolableValue>(
          *underlying_value_owner.Value().non_interpolable_value);
  const auto& non_interpolable_value =
      To<CSSRotateNonInterpolableValue>(*value.non_interpolable_value);
  double progress = To<InterpolableNumber>(*value.interpolable_value).Value();
  underlying_value_owner.MutableValue().non_interpolable_value =
      underlying_non_interpolable_value.Composite(non_interpolable_value,
                                                  progress);
}

void CSSRotateInterpolationType::ApplyStandardPropertyValue(
    const InterpolableValue& interpolable_value,
    const NonInterpolableValue* untyped_non_interpolable_value,
    StyleResolverState& state) const {
  double progress = To<InterpolableNumber>(interpolable_value).Value();
  const auto& non_interpolable_value =
      To<CSSRotateNonInterpolableValue>(*untyped_non_interpolable_value);
  OptionalRotation rotation = non_interpolable_value.SlerpedRotation(progress);
  if (rotation.IsNone()) {
    state.StyleBuilder().SetRotate(nullptr);
    return;
  }
  state.StyleBuilder().SetRotate(MakeGarbageCollected<RotateTransformOperation>(
      rotation.GetRotation(), TransformOperation::kRotate3D));
}

}  // namespace blink
