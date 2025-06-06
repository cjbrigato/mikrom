// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/lobster/lobster_candidate_resizer.h"

#include <string>

#include "ash/public/cpp/lobster/lobster_enums.h"
#include "ash/public/cpp/lobster/lobster_image_candidate.h"
#include "ash/strings/grit/ash_strings.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "chrome/browser/ash/lobster/lobster_image_fetcher.h"
#include "ui/base/l10n/l10n_util.h"

LobsterCandidateResizer::LobsterCandidateResizer(LobsterImageFetcher* fetcher)
    : image_fetcher_(fetcher) {}

LobsterCandidateResizer::~LobsterCandidateResizer() = default;

void LobsterCandidateResizer::InflateImage(
    uint32_t seed,
    const std::string& query,
    ash::InflateCandidateCallback callback) {
  if (image_fetcher_ == nullptr) {
    LOG(ERROR) << "No image fetcher found";
    std::move(callback).Run(base::unexpected(ash::LobsterError(
        /*status_code=*/ash::LobsterErrorCode::kUnknown,
        /*message=*/l10n_util::GetStringUTF8(
            IDS_LOBSTER_NO_SERVER_RESPONSE_ERROR_MESSAGE))));
    return;
  }

  image_fetcher_->RequestFullSizeCandidate(
      query, seed,
      base::BindOnce(
          [](ash::InflateCandidateCallback callback,
             const ash::LobsterResult& result) {
            if (!result.has_value()) {
              LOG(ERROR) << "No inflated image found. Error: "
                         << result.error().message;
              std::move(callback).Run(base::unexpected(result.error()));
              return;
            }

            if (result->size() == 0) {
              LOG(ERROR) << "No inflated image found.";
              std::move(callback).Run(base::unexpected(ash::LobsterError(
                  /*status_code=*/ash::LobsterErrorCode::kUnknown,
                  /*message=*/l10n_util::GetStringUTF8(
                      IDS_LOBSTER_NO_SERVER_RESPONSE_ERROR_MESSAGE))));
              return;
            }

            if (result->size() > 1) {
              LOG(WARNING) << "Receive more than one candidate";
            }

            std::vector<ash::LobsterImageCandidate> inflated_candidates = {
                (*result)[0]};
            std::move(callback).Run(std::move(inflated_candidates));
          },
          std::move(callback)));
}
