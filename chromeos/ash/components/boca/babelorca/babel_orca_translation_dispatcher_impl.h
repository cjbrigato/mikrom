// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_ASH_COMPONENTS_BOCA_BABELORCA_BABEL_ORCA_TRANSLATION_DISPATCHER_IMPL_H_
#define CHROMEOS_ASH_COMPONENTS_BOCA_BABELORCA_BABEL_ORCA_TRANSLATION_DISPATCHER_IMPL_H_

#include <memory>
#include <string>

#include "chromeos/ash/components/boca/babelorca/babel_orca_translation_dispatcher.h"
#include "components/live_caption/translation_dispatcher.h"
#include "components/live_caption/translation_util.h"

namespace ash {

// TODO(377691979): Move this to chromeos/ash/components
class BabelOrcaTranslationDispatcherImpl
    : public babelorca::BabelOrcaTranslationDipsatcher {
 public:
  explicit BabelOrcaTranslationDispatcherImpl(
      std::unique_ptr<::captions::TranslationDispatcher>
          translation_dispatcher);
  BabelOrcaTranslationDispatcherImpl(BabelOrcaTranslationDispatcherImpl&) =
      delete;
  BabelOrcaTranslationDispatcherImpl operator=(
      BabelOrcaTranslationDispatcherImpl&) = delete;
  ~BabelOrcaTranslationDispatcherImpl() override;

  // BabelOrcaTranslationDipsatcher
  void GetTranslation(const std::string& result,
                      const std::string& source_language,
                      const std::string& target_language,
                      ::captions::TranslateEventCallback callback) override;

 private:
  const std::unique_ptr<::captions::TranslationDispatcher>
      translation_dispatcher_;
};

}  // namespace ash

#endif  // CHROMEOS_ASH_COMPONENTS_BOCA_BABELORCA_BABEL_ORCA_TRANSLATION_DISPATCHER_IMPL_H_
