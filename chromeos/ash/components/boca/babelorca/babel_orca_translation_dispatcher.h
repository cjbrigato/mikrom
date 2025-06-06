// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_ASH_COMPONENTS_BOCA_BABELORCA_BABEL_ORCA_TRANSLATION_DISPATCHER_H_
#define CHROMEOS_ASH_COMPONENTS_BOCA_BABELORCA_BABEL_ORCA_TRANSLATION_DISPATCHER_H_

#include <optional>
#include <string>

#include "components/live_caption/translation_util.h"

namespace ash::babelorca {

class BabelOrcaTranslationDipsatcher {
 public:
  virtual ~BabelOrcaTranslationDipsatcher() = default;

  virtual void GetTranslation(const std::string& result,
                              const std::string& source_language,
                              const std::string& target_language,
                              captions::TranslateEventCallback callback) = 0;
};

}  // namespace ash::babelorca

#endif  // CHROMEOS_ASH_COMPONENTS_BOCA_BABELORCA_BABEL_ORCA_TRANSLATION_DISPATCHER_H_
