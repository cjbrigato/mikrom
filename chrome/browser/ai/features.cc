// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ai/features.h"

namespace features {

BASE_FEATURE(kAILanguageModelOverrideConfiguration,
             "kAILanguageModelOverrideConfiguration",
             base::FEATURE_ENABLED_BY_DEFAULT);

const base::FeatureParam<int> kAILanguageModelOverrideConfigurationMaxTopK{
    &features::kAILanguageModelOverrideConfiguration, "max_top_k", 8};

const base::FeatureParam<double>
    kAILanguageModelOverrideConfigurationMaxTemperature{
        &features::kAILanguageModelOverrideConfiguration, "max_temperature",
        2.0f};

// The number of tokens to use as a buffer for generating output. At least this
// many tokens will be available between the language model token limit and the
// max model tokens.
const base::FeatureParam<int> kAILanguageModelOverrideConfigurationOutputBuffer{
    &features::kAILanguageModelOverrideConfiguration,
    "ai_language_model_output_buffer", 1024};

}  // namespace features
