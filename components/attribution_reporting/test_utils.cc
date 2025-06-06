// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/attribution_reporting/test_utils.h"

#include <algorithm>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"
#include "components/attribution_reporting/aggregatable_debug_reporting_config.h"
#include "components/attribution_reporting/aggregatable_dedup_key.h"
#include "components/attribution_reporting/aggregatable_named_budget_candidate.h"
#include "components/attribution_reporting/aggregatable_named_budget_defs.h"
#include "components/attribution_reporting/aggregatable_trigger_config.h"
#include "components/attribution_reporting/aggregatable_trigger_data.h"
#include "components/attribution_reporting/aggregatable_values.h"
#include "components/attribution_reporting/aggregation_keys.h"
#include "components/attribution_reporting/attribution_scopes_data.h"
#include "components/attribution_reporting/attribution_scopes_set.h"
#include "components/attribution_reporting/destination_set.h"
#include "components/attribution_reporting/event_report_windows.h"
#include "components/attribution_reporting/event_trigger_data.h"
#include "components/attribution_reporting/filters.h"
#include "components/attribution_reporting/os_registration.h"
#include "components/attribution_reporting/parsing_utils.h"
#include "components/attribution_reporting/privacy_math.h"
#include "components/attribution_reporting/source_registration.h"
#include "components/attribution_reporting/source_type.h"
#include "components/attribution_reporting/source_type.mojom-forward.h"
#include "components/attribution_reporting/suitable_origin.h"
#include "components/attribution_reporting/trigger_config.h"
#include "components/attribution_reporting/trigger_registration.h"
#include "net/base/schemeful_site.h"
#include "url/origin.h"

namespace attribution_reporting {

FiltersDisjunction FiltersForSourceType(
    mojom::SourceType source_type,
    std::optional<base::TimeDelta> lookback_window) {
  return {*FilterConfig::Create(
      {
          {
              {FilterData::kSourceTypeFilterKey, {SourceTypeName(source_type)}},
          },
      },
      lookback_window)};
}

EventReportWindows EventReportWindowsWithCount(int num_report_windows) {
  std::vector<base::TimeDelta> deltas;
  deltas.reserve(num_report_windows);
  for (int i = 0; i < num_report_windows; i++) {
    deltas.emplace_back(base::Days(1) + base::Days(i));
  }
  return *EventReportWindows::Create(base::Days(0), std::move(deltas));
}

TriggerDataSet TriggerDataSetWithCardinality(int cardinality) {
  TriggerDataSet::TriggerData trigger_data;
  for (int i = 0; i < cardinality; ++i) {
    trigger_data.insert(i);
  }

  return *TriggerDataSet::Create(std::move(trigger_data));
}

std::ostream& operator<<(std::ostream& out,
                         const AggregationKeys& aggregation_keys) {
  return out << aggregation_keys.ToJson();
}

std::ostream& operator<<(std::ostream& out, const FilterData& filter_data) {
  return out << filter_data.ToJson();
}

std::ostream& operator<<(std::ostream& out, const FilterPair& filters) {
  base::Value::Dict dict;
  filters.SerializeIfNotEmpty(dict);
  return out << dict;
}

std::ostream& operator<<(std::ostream& out,
                         const DestinationSet& destination_set) {
  return out << destination_set.ToJson();
}

std::ostream& operator<<(std::ostream& out,
                         const EventReportWindows& event_report_windows) {
  base::Value::Dict dict;
  event_report_windows.Serialize(dict);
  return out << dict;
}

std::ostream& operator<<(std::ostream& out,
                         const AttributionScopesSet& attribution_scopes_set) {
  base::Value::Dict dict;
  attribution_scopes_set.SerializeForTrigger(dict);
  return out << dict;
}

std::ostream& operator<<(std::ostream& out,
                         const AttributionScopesData& attribution_scopes_data) {
  return out << attribution_scopes_data.ToJson();
}

std::ostream& operator<<(std::ostream& out,
                         const AggregatableNamedBudgetDefs& budgets) {
  base::Value::Dict dict;
  budgets.Serialize(dict);
  return out << dict;
}

std::ostream& operator<<(std::ostream& out, const SourceRegistration& s) {
  return out << s.ToJson();
}

std::ostream& operator<<(std::ostream& out, const AggregatableValues& values) {
  return out << values.ToJson();
}

std::ostream& operator<<(std::ostream& out,
                         const AggregatableTriggerData& trigger_data) {
  return out << trigger_data.ToJson();
}

std::ostream& operator<<(std::ostream& out,
                         const EventTriggerData& event_trigger) {
  return out << event_trigger.ToJson();
}

std::ostream& operator<<(std::ostream& out,
                         const AggregatableNamedBudgetCandidate& budget) {
  return out << budget.ToJson();
}

std::ostream& operator<<(std::ostream& out, const TriggerRegistration& reg) {
  return out << reg.ToJson();
}

std::ostream& operator<<(std::ostream& out, const SuitableOrigin& origin) {
  return out << *origin;
}

std::ostream& operator<<(std::ostream& out,
                         const AggregatableDedupKey& aggregatable_dedup_key) {
  return out << aggregatable_dedup_key.ToJson();
}

std::ostream& operator<<(std::ostream& out, const OsRegistrationItem& item) {
  return out << "{url=" << item.url
             << ", debug_reporting=" << item.debug_reporting << "}";
}

std::ostream& operator<<(std::ostream& out, const TriggerDataSet& set) {
  return out << set.ToJson();
}

std::ostream& operator<<(
    std::ostream& out,
    const AggregatableTriggerConfig& aggregatable_trigger_config) {
  base::Value::Dict dict;
  aggregatable_trigger_config.Serialize(dict);
  return out << dict;
}

std::ostream& operator<<(std::ostream& out, const ParseError&) {
  return out << "ParseError";
}

std::ostream& operator<<(std::ostream& out, const FakeEventLevelReport& r) {
  return out << "{trigger_data=" << r.trigger_data
             << ",window_index=" << r.window_index << "}";
}

std::ostream& operator<<(std::ostream& out, const RandomizedResponseData& r) {
  out << "{rate=" << r.rate() << ",response=";

  if (r.response().has_value()) {
    out << "[";

    for (const char* separator = ""; const auto& fake_report : *r.response()) {
      out << separator << fake_report;
      separator = ", ";
    }

    out << "]";
  } else {
    out << "null";
  }

  return out << "}";
}

std::ostream& operator<<(std::ostream& out,
                         const AggregatableDebugReportingConfig& v) {
  base::Value::Dict dict;
  v.Serialize(dict);
  return out << dict;
}

std::ostream& operator<<(std::ostream& out,
                         const SourceAggregatableDebugReportingConfig& v) {
  base::Value::Dict dict;
  v.Serialize(dict);
  return out << dict;
}

std::ostream& operator<<(std::ostream& out,
                         const AggregatableDebugReportingContribution& v) {
  return out << "{key_piece=" << HexEncodeAggregationKey(v.key_piece())
             << ",value=" << v.value() << "}";
}

}  // namespace attribution_reporting
