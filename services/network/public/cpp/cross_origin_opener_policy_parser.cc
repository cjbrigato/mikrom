// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/public/cpp/cross_origin_opener_policy_parser.h"

#include <algorithm>
#include <string_view>

#include "base/strings/string_util.h"
#include "net/http/http_response_headers.h"
#include "net/http/structured_headers.h"
#include "services/network/public/cpp/cross_origin_opener_policy.h"
#include "services/network/public/cpp/features.h"

namespace network {

namespace {

// Const definition of the strings involved in the header parsing.
constexpr char kCrossOriginOpenerPolicyHeader[] = "Cross-Origin-Opener-Policy";
constexpr char kCrossOriginOpenerPolicyHeaderReportOnly[] =
    "Cross-Origin-Opener-Policy-Report-Only";
constexpr char kSameOrigin[] = "same-origin";
constexpr char kSameOriginAllowPopups[] = "same-origin-allow-popups";
constexpr char kUnsafeNone[] = "unsafe-none";
constexpr char kReportTo[] = "report-to";
constexpr char kNoopenerAllowPopups[] = "noopener-allow-popups";

// Fills |value|, |endpoint| and an optional |soap_by_default_value| with
// parsed values from |header|.
// Note: if |header| is invalid, |value|, |soap_by_default_value| and
// |endpoint| will not be modified.
void ParseHeader(std::string_view header_value,
                 mojom::CrossOriginOpenerPolicyValue* value,
                 mojom::CrossOriginOpenerPolicyValue* soap_by_default_value,
                 std::optional<std::string>* endpoint) {
  DCHECK(value);
  DCHECK(endpoint);
  using Item = net::structured_headers::Item;
  const auto item = net::structured_headers::ParseItem(header_value);
  if (item && item->item.is_token()) {
    const auto& policy_item = item->item.GetString();
    if (policy_item == kSameOrigin) {
      *value = mojom::CrossOriginOpenerPolicyValue::kSameOrigin;
      if (soap_by_default_value) {
        *soap_by_default_value =
            mojom::CrossOriginOpenerPolicyValue::kSameOrigin;
      }
    }
    if (policy_item == kSameOriginAllowPopups) {
      *value = mojom::CrossOriginOpenerPolicyValue::kSameOriginAllowPopups;
      if (soap_by_default_value) {
        *soap_by_default_value =
            mojom::CrossOriginOpenerPolicyValue::kSameOriginAllowPopups;
      }
    }
    if (policy_item == kUnsafeNone) {
      *value = mojom::CrossOriginOpenerPolicyValue::kUnsafeNone;
      if (soap_by_default_value) {
        *soap_by_default_value =
            mojom::CrossOriginOpenerPolicyValue::kUnsafeNone;
      }
    }
    if ((policy_item == kNoopenerAllowPopups) &&
        base::FeatureList::IsEnabled(features::kCoopNoopenerAllowPopups)) {
      *value = mojom::CrossOriginOpenerPolicyValue::kNoopenerAllowPopups;
      if (soap_by_default_value) {
        *soap_by_default_value =
            mojom::CrossOriginOpenerPolicyValue::kNoopenerAllowPopups;
      }
    }
    auto it = std::ranges::find(item->params, kReportTo,
                                &std::pair<std::string, Item>::first);
    if (it != item->params.end() && it->second.is_string()) {
      *endpoint = it->second.GetString();
    }
  }
}

}  // namespace

CrossOriginOpenerPolicy ParseCrossOriginOpenerPolicy(
    const net::HttpResponseHeaders& headers) {
  CrossOriginOpenerPolicy coop;

  // This is the single line of code disabling COOP globally.
  if (!base::FeatureList::IsEnabled(features::kCrossOriginOpenerPolicy))
    return coop;

  coop.soap_by_default_value =
      mojom::CrossOriginOpenerPolicyValue::kSameOriginAllowPopups;

  // Parse Cross-Origin-Opener-Policy:
  if (std::optional<std::string> header_value =
          headers.GetNormalizedHeader(kCrossOriginOpenerPolicyHeader)) {
    ParseHeader(*header_value, &coop.value, &coop.soap_by_default_value,
                &coop.reporting_endpoint);
  }

  if (base::FeatureList::IsEnabled(
          features::kCrossOriginOpenerPolicyByDefault)) {
    coop.value = coop.soap_by_default_value;
  }

  // Parse Cross-Origin-Opener-Policy-Report-Only:
  if (std::optional<std::string> header_value = headers.GetNormalizedHeader(
          kCrossOriginOpenerPolicyHeaderReportOnly)) {
    ParseHeader(*header_value, &coop.report_only_value, nullptr,
                &coop.report_only_reporting_endpoint);
  }

  return coop;
}

}  // namespace network
