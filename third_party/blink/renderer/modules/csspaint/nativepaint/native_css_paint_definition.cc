// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/csspaint/nativepaint/native_css_paint_definition.h"

#include "third_party/blink/renderer/core/animation/animation_time_delta.h"
#include "third_party/blink/renderer/core/animation/element_animations.h"
#include "third_party/blink/renderer/core/animation/interpolable_value.h"
#include "third_party/blink/renderer/core/css/css_value.h"
#include "third_party/blink/renderer/core/css/properties/css_property.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"

namespace blink {

NativeCssPaintDefinition::NativeCssPaintDefinition(
    LocalFrame* local_root,
    PaintWorkletInput::PaintWorkletInputType type)
    : NativePaintDefinition(local_root, type) {}

bool NativeCssPaintDefinition::CanGetValueFromKeyframe(
    const Element* element,
    const PropertySpecificKeyframe* frame,
    const KeyframeEffectModelBase* model,
    ValueFilter filter) {
  if (model->IsStringKeyframeEffectModel()) {
    DCHECK(frame->IsCSSPropertySpecificKeyframe());
    const CSSValue* value = To<CSSPropertySpecificKeyframe>(frame)->Value();
    return filter(element, value, nullptr);
  } else {
    DCHECK(frame->IsTransitionPropertySpecificKeyframe());
    const TransitionKeyframe::PropertySpecificKeyframe* keyframe =
        To<TransitionKeyframe::PropertySpecificKeyframe>(frame);
    InterpolableValue* value =
        keyframe->GetValue()->Value().interpolable_value.Get();
    return filter(element, nullptr, value);
  }
}

Animation* NativeCssPaintDefinition::GetAnimationForProperty(
    const Element* element,
    const CSSProperty& property,
    ValueFilter filter) {
  if (!element->GetElementAnimations()) {
    return nullptr;
  }
  Animation* compositable_animation = nullptr;

  // We'd composite only if it is the only animation of its type on
  // this element.
  unsigned count = 0;
  for (const auto& animation : element->GetElementAnimations()->Animations()) {
    if (animation.key->CalculateAnimationPlayState() ==
            V8AnimationPlayState::Enum::kIdle ||
        !animation.key->Affects(*element, property)) {
      continue;
    }
    count++;
    compositable_animation = animation.key;
  }
  if (!compositable_animation || count > 1) {
    return nullptr;
  }

  if (!AnimationIsValidForPaintWorklets(compositable_animation, element,
                                        property, filter)) {
    return nullptr;
  }

  return compositable_animation;
}

bool NativeCssPaintDefinition::AnimationIsValidForPaintWorklets(
    Animation* compositable_animation,
    const Element* element,
    const CSSProperty& property,
    ValueFilter filter) {
  const AnimationEffect* effect = compositable_animation->effect();

  // TODO(crbug.com/1429770): Implement positive delay fix for bgcolor.
  if (effect->SpecifiedTiming().start_delay.AsTimeValue().InSecondsF() > 0.f) {
    if (property.PropertyID() != CSSPropertyID::kClipPath) {
      return false;
    }
  }

  DCHECK(effect->IsKeyframeEffect());
  const KeyframeEffectModelBase* model =
      static_cast<const KeyframeEffect*>(effect)->Model();
  if (model->AffectedByUnderlyingAnimations()) {
    return false;
  }
  const PropertySpecificKeyframeVector* frames =
      model->GetPropertySpecificKeyframes(PropertyHandle(property));
  DCHECK_GE(frames->size(), 2u);
  for (const auto& frame : *frames) {
    if (!CanGetValueFromKeyframe(element, frame, model, filter)) {
      return false;
    }
  }
  return true;
}

bool NativeCssPaintDefinition::DefaultValueFilter(
    const Element* element,
    const CSSValue* value,
    const InterpolableValue* interpolable_value) {
  return value || interpolable_value;
}

std::optional<double> NativeCssPaintDefinition::Progress(
    const std::optional<double>& main_thread_progress,
    const CompositorPaintWorkletJob::AnimatedPropertyValues&
        animated_property_values) {
  std::optional<double> progress = main_thread_progress;

  // Override the progress from the main thread if the animation has been
  // started on the compositor.
  if (!animated_property_values.empty()) {
    DCHECK_EQ(animated_property_values.size(), 1u);
    const auto& entry = animated_property_values.begin();
    progress = entry->second.float_value.value();
  }

  return progress;
}

}  // namespace blink
