// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_SERVICE_CONFIGURE_CONTEXT_H_
#define COMPONENTS_SYNC_SERVICE_CONFIGURE_CONTEXT_H_

#include <string>

#include "base/time/time.h"
#include "components/sync/base/previously_syncing_gaia_id_info_for_metrics.h"
#include "components/sync/base/sync_mode.h"
#include "components/sync/engine/configure_reason.h"
#include "google_apis/gaia/gaia_id.h"

namespace syncer {

// Struct describing in which context sync was enabled, including state that can
// be assumed to be fixed while sync is enabled (or, more precisely, is
// representative of the last (re)configuration request). It's built by
// SyncServiceImpl and plumbed through DataTypeManager until datatype
// controllers, which propagate analogous information to the processor/bridge
// via DataTypeActivationRequest.
struct ConfigureContext {
  ConfigureContext();
  ConfigureContext(const ConfigureContext&);
  ~ConfigureContext();

  GaiaId authenticated_gaia_id;
  std::string cache_guid;
  SyncMode sync_mode = SyncMode::kFull;
  ConfigureReason reason = CONFIGURE_REASON_UNKNOWN;
  base::Time configuration_start_time;
  PreviouslySyncingGaiaIdInfoForMetrics previously_syncing_gaia_id_info =
      PreviouslySyncingGaiaIdInfoForMetrics::kUnspecified;
};

}  // namespace syncer

#endif  // COMPONENTS_SYNC_SERVICE_CONFIGURE_CONTEXT_H_
