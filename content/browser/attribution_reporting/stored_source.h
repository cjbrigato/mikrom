// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ATTRIBUTION_REPORTING_STORED_SOURCE_H_
#define CONTENT_BROWSER_ATTRIBUTION_REPORTING_STORED_SOURCE_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/time/time.h"
#include "base/types/strong_alias.h"
#include "components/attribution_reporting/aggregation_keys.h"
#include "components/attribution_reporting/attribution_scopes_data.h"
#include "components/attribution_reporting/destination_set.h"
#include "components/attribution_reporting/event_level_epsilon.h"
#include "components/attribution_reporting/event_report_windows.h"
#include "components/attribution_reporting/filters.h"
#include "components/attribution_reporting/max_event_level_reports.h"
#include "components/attribution_reporting/trigger_config.h"
#include "components/attribution_reporting/trigger_data_matching.mojom-forward.h"
#include "content/browser/attribution_reporting/common_source_info.h"
#include "content/common/content_export.h"
#include "third_party/abseil-cpp/absl/numeric/int128.h"

namespace content {

class AggregatableNamedBudgetPair;

// Contains attributes specific to a stored source.
class CONTENT_EXPORT StoredSource {
 public:
  using Id = base::StrongAlias<StoredSource, int64_t>;
  using AggregatableNamedBudgets =
      base::flat_map<std::string, AggregatableNamedBudgetPair>;

  // Note that aggregatable reports are not subject to the attribution logic.
  enum class AttributionLogic {
    // Never send a report for this source even if it gets attributed.
    kNever = 0,
    // Attribute the source truthfully.
    kTruthfully = 1,
    // The browser generates fake reports for the source.
    kFalsely = 2,
    kMaxValue = kFalsely,
  };

  enum class ActiveState {
    kActive = 0,
    kInactive = 1,
    kReachedEventLevelAttributionLimit = 2,
    kMaxValue = kReachedEventLevelAttributionLimit,
  };

  static std::optional<StoredSource> Create(
      CommonSourceInfo common_info,
      uint64_t source_event_id,
      attribution_reporting::DestinationSet,
      base::Time source_time,
      base::Time expiry_time,
      attribution_reporting::TriggerDataSet,
      attribution_reporting::EventReportWindows,
      attribution_reporting::MaxEventLevelReports,
      base::Time aggregatable_report_window_time,
      int64_t priority,
      attribution_reporting::FilterData,
      std::optional<uint64_t> debug_key,
      attribution_reporting::AggregationKeys,
      AttributionLogic,
      ActiveState,
      Id source_id,
      int remaining_aggregatable_attribution_budget,
      double randomized_response_rate,
      attribution_reporting::mojom::TriggerDataMatching,
      attribution_reporting::EventLevelEpsilon,
      absl::uint128 aggregatable_debug_key_piece,
      int remaining_aggregatable_debug_budget,
      std::optional<attribution_reporting::AttributionScopesData>,
      AggregatableNamedBudgets);

  ~StoredSource();

  StoredSource(const StoredSource&);
  StoredSource(StoredSource&&);

  StoredSource& operator=(const StoredSource&);
  StoredSource& operator=(StoredSource&&);

  const CommonSourceInfo& common_info() const { return common_info_; }

  uint64_t source_event_id() const { return source_event_id_; }

  const attribution_reporting::DestinationSet& destination_sites() const {
    return destination_sites_;
  }

  base::Time source_time() const { return source_time_; }

  base::Time expiry_time() const { return expiry_time_; }

  base::Time aggregatable_report_window_time() const {
    return aggregatable_report_window_time_;
  }

  const attribution_reporting::TriggerDataSet& trigger_data() const {
    return trigger_data_;
  }

  const attribution_reporting::EventReportWindows& event_report_windows()
      const {
    return event_report_windows_;
  }

  attribution_reporting::MaxEventLevelReports max_event_level_reports() const {
    return max_event_level_reports_;
  }

  int64_t priority() const { return priority_; }

  const attribution_reporting::FilterData& filter_data() const {
    return filter_data_;
  }

  std::optional<uint64_t> debug_key() const { return debug_key_; }

  const attribution_reporting::AggregationKeys& aggregation_keys() const {
    return aggregation_keys_;
  }

  AttributionLogic attribution_logic() const { return attribution_logic_; }

  ActiveState active_state() const { return active_state_; }

  Id source_id() const { return source_id_; }

  int remaining_aggregatable_attribution_budget() const {
    return remaining_aggregatable_attribution_budget_;
  }

  const std::vector<uint64_t>& dedup_keys() const { return dedup_keys_; }

  std::vector<uint64_t>& dedup_keys() { return dedup_keys_; }

  const std::vector<uint64_t>& aggregatable_dedup_keys() const {
    return aggregatable_dedup_keys_;
  }

  std::vector<uint64_t>& aggregatable_dedup_keys() {
    return aggregatable_dedup_keys_;
  }

  double randomized_response_rate() const { return randomized_response_rate_; }

  attribution_reporting::mojom::TriggerDataMatching trigger_data_matching()
      const {
    return trigger_data_matching_;
  }

  attribution_reporting::EventLevelEpsilon event_level_epsilon() const {
    return event_level_epsilon_;
  }

  absl::uint128 aggregatable_debug_key_piece() const {
    return aggregatable_debug_key_piece_;
  }

  int remaining_aggregatable_debug_budget() const {
    return remaining_aggregatable_debug_budget_;
  }

  const std::optional<attribution_reporting::AttributionScopesData>&
  attribution_scopes_data() const {
    return attribution_scopes_data_;
  }

  const AggregatableNamedBudgets& aggregatable_named_budgets() const {
    return aggregatable_named_budgets_;
  }

 private:
  StoredSource(CommonSourceInfo common_info,
               uint64_t source_event_id,
               attribution_reporting::DestinationSet,
               base::Time source_time,
               base::Time expiry_time,
               attribution_reporting::TriggerDataSet,
               attribution_reporting::EventReportWindows,
               attribution_reporting::MaxEventLevelReports,
               base::Time aggregatable_report_window_time,
               int64_t priority,
               attribution_reporting::FilterData,
               std::optional<uint64_t> debug_key,
               attribution_reporting::AggregationKeys,
               AttributionLogic,
               ActiveState,
               Id source_id,
               int remaining_aggregatable_attribution_budget,
               double randomized_response_rate,
               attribution_reporting::mojom::TriggerDataMatching,
               attribution_reporting::EventLevelEpsilon,
               absl::uint128 aggregatable_debug_key_piece,
               int remaining_aggregatable_debug_budget,
               std::optional<attribution_reporting::AttributionScopesData>,
               AggregatableNamedBudgets);

  CommonSourceInfo common_info_;

  uint64_t source_event_id_;
  attribution_reporting::DestinationSet destination_sites_;
  base::Time source_time_;
  base::Time expiry_time_;
  attribution_reporting::TriggerDataSet trigger_data_;
  attribution_reporting::EventReportWindows event_report_windows_;
  attribution_reporting::MaxEventLevelReports max_event_level_reports_;
  base::Time aggregatable_report_window_time_;
  int64_t priority_;
  attribution_reporting::FilterData filter_data_;
  std::optional<uint64_t> debug_key_;
  attribution_reporting::AggregationKeys aggregation_keys_;

  AttributionLogic attribution_logic_;

  ActiveState active_state_;

  Id source_id_;

  int remaining_aggregatable_attribution_budget_;

  std::vector<uint64_t> dedup_keys_;

  std::vector<uint64_t> aggregatable_dedup_keys_;

  double randomized_response_rate_;

  attribution_reporting::mojom::TriggerDataMatching trigger_data_matching_;

  attribution_reporting::EventLevelEpsilon event_level_epsilon_;

  absl::uint128 aggregatable_debug_key_piece_;

  int remaining_aggregatable_debug_budget_;

  std::optional<attribution_reporting::AttributionScopesData>
      attribution_scopes_data_;

  AggregatableNamedBudgets aggregatable_named_budgets_;

  // When adding new members, the corresponding `operator==()` definition in
  // `attribution_test_utils.h` should also be updated.
};

}  // namespace content

#endif  // CONTENT_BROWSER_ATTRIBUTION_REPORTING_STORED_SOURCE_H_
