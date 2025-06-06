// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_GEOMETRY_LINEAR_GRADIENT_H_
#define UI_GFX_GEOMETRY_LINEAR_GRADIENT_H_

#include <stdint.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "base/component_export.h"

namespace gfx {

class AxisTransform2d;
class Transform;

// A class that defines a linear gradient mask.
// Up to 6 steps are supported.
//
// ex. Horizontal linear gradient that starts in the middle.
// LinearGradient gradient(0);
// gradient.AddStep(20, 0);
// gradient.AddStep(30, 255);
// gradient.AddStep(70, 255);
// gradient.AddStep(80, 0);
class COMPONENT_EXPORT(GEOMETRY_SKIA) LinearGradient {
 public:
  struct Step {
    // Fraction that defines a position in diagonal, from 0 to 1.
    float fraction = 0;
    // Alpha, from 0 to 255.
    uint8_t alpha = 0;

    friend bool operator==(const Step&, const Step&) = default;
  };
  static LinearGradient& GetEmpty();

  static constexpr size_t kMaxStepSize = 8;
  using StepArray = std::array<Step, kMaxStepSize>;

  LinearGradient();
  explicit LinearGradient(int16_t angle);
  LinearGradient(const LinearGradient& copy);
  LinearGradient& operator=(const LinearGradient& gradient_mask) = default;

  bool IsEmpty() const { return !step_count_; }

  // Add a new step. Adding more than 6 results in DCHECK or ignored.
  void AddStep(float fraction, uint8_t alpha);

  // Get step information.
  const StepArray& steps() const { return steps_; }
  StepArray& steps() { return steps_; }
  size_t step_count() const { return step_count_; }

  // Gets/Sets an angle (in degrees).
  int16_t angle() const { return angle_; }
  void set_angle(int16_t degree) { angle_ = degree; }

  // Reverse the steps.
  void ReverseSteps();

  // Transform the angle.
  void ApplyTransform(const Transform& transform);
  void ApplyTransform(const AxisTransform2d& transform);

  std::string ToString() const;

  friend bool operator==(const LinearGradient&,
                         const LinearGradient&) = default;

 private:
  // angle in degrees.
  int16_t angle_ = 0;
  size_t step_count_ = 0;
  StepArray steps_;
};

}  // namespace gfx

#endif  // UI_GFX_GEOMETRY_LINEAR_GRADIENT_H_
