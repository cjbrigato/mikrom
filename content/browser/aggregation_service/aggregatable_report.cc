// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/aggregation_service/aggregatable_report.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/check.h"
#include "base/check_op.h"
#include "base/containers/adapters.h"
#include "base/containers/flat_map.h"
#include "base/containers/span.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/notreached.h"
#include "base/numerics/byte_conversions.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "base/values.h"
#include "components/aggregation_service/aggregation_coordinator_utils.h"
#include "components/aggregation_service/parsing_utils.h"
#include "components/cbor/values.h"
#include "components/cbor/writer.h"
#include "content/browser/aggregation_service/proto/aggregatable_report.pb.h"
#include "content/browser/aggregation_service/public_key.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "third_party/abseil-cpp/absl/numeric/int128.h"
#include "third_party/blink/public/mojom/aggregation_service/aggregatable_report.mojom.h"
#include "third_party/boringssl/src/include/openssl/hpke.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace content {

namespace {

constexpr size_t kBitsPerByte = 8;

// Payload contents:
constexpr std::string_view kHistogramValue = "histogram";
constexpr std::string_view kOperationKey = "operation";

std::optional<GURL> GetProcessingUrl(
    const std::optional<url::Origin>& aggregation_coordinator_origin) {
  if (!aggregation_coordinator_origin.has_value()) {
    return GetAggregationServiceProcessingUrl(
        ::aggregation_service::GetDefaultAggregationCoordinatorOrigin());
  }
  if (!::aggregation_service::IsAggregationCoordinatorOriginAllowed(
          *aggregation_coordinator_origin)) {
    return std::nullopt;
  }
  return GetAggregationServiceProcessingUrl(*aggregation_coordinator_origin);
}

// TODO(crbug.com/40215445): Replace with `base/numerics/byte_conversions.h` if
// available.
std::array<uint8_t, 16u> U128ToBigEndian(absl::uint128 integer) {
  std::array<uint8_t, 16u> byte_string;

  // Construct the vector in reverse to ensure network byte (big-endian) order.
  for (auto& byte : base::Reversed(byte_string)) {
    byte = static_cast<uint8_t>(integer & 0xFF);
    integer >>= 8;
  }
  return byte_string;
}

void AppendEncodedContributionToCborArray(
    cbor::Value::ArrayValue& array,
    const blink::mojom::AggregatableReportHistogramContribution& contribution,
    size_t filtering_id_max_bytes) {
  cbor::Value::MapValue map;
  map.emplace("bucket", U128ToBigEndian(contribution.bucket));
  map.emplace("value", base::U32ToBigEndian(contribution.value));

  uint64_t filtering_id = contribution.filtering_id.value_or(0);
  CHECK_LE(static_cast<size_t>(std::bit_width(filtering_id)),
           kBitsPerByte * filtering_id_max_bytes);

  static_assert(
      AggregationServicePayloadContents::kMaximumFilteringIdMaxBytes == 8);
  std::array<uint8_t, 8u> encoded_id;
  encoded_id.fill(0);
  base::span(encoded_id).copy_from(base::U64ToBigEndian(filtering_id));

  // Note that the payload will have a length dependent on the choice of
  // `filtering_id_max_bytes` here. APIs using this field should ensure that
  // this value is not dependent on cross-site data (or only allow it to vary in
  // debug mode).
  map.emplace("id", base::span(encoded_id).last(filtering_id_max_bytes));

  array.emplace_back(std::move(map));
}

// Returns a serialized CBOR map. See the `AggregatableReport` documentation for
// more detail on the expected format. Returns `std::nullopt` if serialization
// fails.
std::optional<std::vector<uint8_t>> ConstructUnencryptedPayload(
    const AggregationServicePayloadContents& payload_contents) {
  cbor::Value::MapValue value;
  value.emplace(kOperationKey, kHistogramValue);

  cbor::Value::ArrayValue data;
  for (const blink::mojom::AggregatableReportHistogramContribution&
           contribution : payload_contents.contributions) {
    AppendEncodedContributionToCborArray(
        data, contribution, payload_contents.filtering_id_max_bytes);
  }

  // This property is enforced by `AggregatableReportRequest::Create()`.
  CHECK_GE(payload_contents.max_contributions_allowed,
           payload_contents.contributions.size());
  const size_t number_of_null_contributions_to_add =
      payload_contents.max_contributions_allowed -
      payload_contents.contributions.size();
  for (size_t i = 0; i < number_of_null_contributions_to_add; ++i) {
    AppendEncodedContributionToCborArray(
        data,
        blink::mojom::AggregatableReportHistogramContribution(
            /*bucket=*/0, /*value=*/0, /*filtering_id=*/std::nullopt),
        payload_contents.filtering_id_max_bytes);
  }

  value.emplace("data", std::move(data));

  return cbor::Writer::Write(cbor::Value(std::move(value)));
}

constexpr std::optional<size_t> ComputeCborArrayOverheadLen(
    size_t num_elements) {
  if (num_elements <= 0x17) {
    return 1;  // The length fits in the array's "initial byte".
  } else if (num_elements <= std::numeric_limits<uint8_t>::max()) {
    return 1 + sizeof(uint8_t);
  } else if (num_elements <= std::numeric_limits<uint16_t>::max()) {
    return 1 + sizeof(uint16_t);
  } else if (num_elements <= std::numeric_limits<uint32_t>::max()) {
    return 1 + sizeof(uint32_t);
  }
  // We will never generate reports with 2**32 or more contributions, so
  // there's no reason to model CBOR's behavior on such large arrays.
  return std::nullopt;
}

// Computes the length in bytes of a payload's plaintext CBOR serialization.
// Returns `std::nullopt` if the computation would overflow or if
// `num_contributions` exceeds the maximum value of `uint32_t`. See
// `AggregatableReport::AggregationServicePayload` for the format's definition.
constexpr std::optional<size_t> ComputePayloadLengthInBytes(
    size_t num_contributions,
    size_t filtering_id_max_bytes) {
  constexpr base::CheckedNumeric<size_t> kPayloadLenBeforeArray{
      1                                           // map(2)
      + 1 + std::string_view("operation").size()  // text(9)
      + 1 + std::string_view("histogram").size()  // text(9)
      + 1 + std::string_view("data").size()       // text(4)
  };
  static_assert(kPayloadLenBeforeArray.ValueOrDie() == 26);

  const std::optional<size_t> array_overhead_len =
      ComputeCborArrayOverheadLen(num_contributions);
  if (!array_overhead_len.has_value()) {
    return std::nullopt;
  }

  constexpr base::CheckedNumeric<size_t> kContributionLenWithoutId{
      1                                        // map(_)
      + 1 + std::string_view("bucket").size()  // text(6)
      + 1 + 16                                 // bytes(16)
      + 1 + std::string_view("value").size()   // text(5)
      + 1 + 4                                  // bytes(4)
  };
  static_assert(kContributionLenWithoutId.ValueOrDie() == 36);

  const base::CheckedNumeric<size_t> filtering_id_len =
      1 + std::string_view("id").size()      // text(2)
      + 1 + size_t{filtering_id_max_bytes};  // bytes(_)

  const base::CheckedNumeric<size_t> payload_len =
      kPayloadLenBeforeArray + *array_overhead_len +
      (num_contributions * (kContributionLenWithoutId + filtering_id_len));
  if (!payload_len.IsValid()) {
    return std::nullopt;
  }
  return payload_len.ValueOrDie();
}

std::optional<AggregationServicePayloadContents>
ConvertPayloadContentsFromProto(
    const proto::AggregationServicePayloadContents& proto) {
  if (proto.operation() !=
      proto::AggregationServicePayloadContents_Operation_HISTOGRAM) {
    return std::nullopt;
  }
  AggregationServicePayloadContents::Operation operation(
      AggregationServicePayloadContents::Operation::kHistogram);

  std::vector<blink::mojom::AggregatableReportHistogramContribution>
      contributions;
  for (const proto::AggregatableReportHistogramContribution&
           contribution_proto : proto.contributions()) {
    std::optional<uint64_t> filtering_id;
    if (contribution_proto.has_filtering_id()) {
      filtering_id = contribution_proto.filtering_id();
    }
    contributions.emplace_back(
        /*bucket=*/absl::MakeUint128(contribution_proto.bucket_high(),
                                     contribution_proto.bucket_low()),
        /*value=*/contribution_proto.value(), filtering_id);
  }

  std::optional<url::Origin> aggregation_coordinator_origin;
  if (proto.has_aggregation_coordinator_origin()) {
    aggregation_coordinator_origin =
        url::Origin::Create(GURL(proto.aggregation_coordinator_origin()));
  }

  if (proto.max_contributions_allowed() < 0) {
    return std::nullopt;
  }
  // Don't pad reports stored before padding was implemented.
  const size_t max_contributions_allowed =
      proto.max_contributions_allowed() == 0
          ? contributions.size()
          : base::saturated_cast<size_t>(proto.max_contributions_allowed());

  // Default used for reports saved without this feature.
  size_t filtering_id_max_bytes = 1u;
  if (proto.has_filtering_id_max_bytes()) {
    filtering_id_max_bytes = proto.filtering_id_max_bytes();
  }

  return AggregationServicePayloadContents(
      operation, std::move(contributions),
      std::move(aggregation_coordinator_origin), max_contributions_allowed,
      filtering_id_max_bytes);
}

std::optional<AggregatableReportSharedInfo> ConvertSharedInfoFromProto(
    const proto::AggregatableReportSharedInfo& proto) {
  base::Time scheduled_report_time = base::Time::FromDeltaSinceWindowsEpoch(
      base::Microseconds(proto.scheduled_report_time()));
  base::Uuid report_id = base::Uuid::ParseLowercase(proto.report_id());
  url::Origin reporting_origin =
      url::Origin::Create(GURL(proto.reporting_origin()));

  AggregatableReportSharedInfo::DebugMode debug_mode =
      AggregatableReportSharedInfo::DebugMode::kDisabled;
  switch (proto.debug_mode()) {
    case proto::AggregatableReportSharedInfo_DebugMode_DISABLED:
      break;
    case proto::AggregatableReportSharedInfo_DebugMode_ENABLED:
      debug_mode = AggregatableReportSharedInfo::DebugMode::kEnabled;
      break;
    default:
      return std::nullopt;
  }

  std::string api_version = proto.api_version();
  std::string api_identifier = proto.api_identifier();

  return AggregatableReportSharedInfo(
      scheduled_report_time, std::move(report_id), std::move(reporting_origin),
      debug_mode,
      // TODO(alexmt): Persist additional_fields when it becomes necessary.
      /*additional_fields=*/base::Value::Dict(),
      // TODO(crbug.com/40230303): Add mechanism to upgrade stored requests from
      // older to newer versions.
      std::move(api_version), std::move(api_identifier));
}

std::optional<AggregatableReportRequest> ConvertReportRequestFromProto(
    proto::AggregatableReportRequest request_proto) {
  std::optional<AggregationServicePayloadContents> payload_contents(
      ConvertPayloadContentsFromProto(request_proto.payload_contents()));
  if (!payload_contents.has_value()) {
    return std::nullopt;
  }

  std::optional<AggregatableReportRequest::DelayType> delay_type;
  if (request_proto.has_delay_type()) {
    // Protos read from disk may be corrupted and proto3 enums are permitted to
    // contain unrecognized values.
    if (!proto::AggregatableReportRequest::DelayType_IsValid(
            request_proto.delay_type())) {
      return std::nullopt;
    }
    delay_type = static_cast<AggregatableReportRequest::DelayType>(
        request_proto.delay_type());
    // The `Unscheduled` enumerator is not defined in the proto's version of
    // the enum, and we already know that the value was valid.
    CHECK_NE(*delay_type, AggregatableReportRequest::DelayType::Unscheduled);
  }

  std::optional<AggregatableReportSharedInfo> shared_info(
      ConvertSharedInfoFromProto(request_proto.shared_info()));
  if (!shared_info.has_value()) {
    return std::nullopt;
  }

  std::optional<uint64_t> debug_key;
  if (request_proto.has_debug_key()) {
    debug_key = request_proto.debug_key();
  }

  base::flat_map<std::string, std::string> additional_fields;
  for (auto& elem : request_proto.additional_fields()) {
    additional_fields.emplace(std::move(elem));
  }

  return AggregatableReportRequest::Create(
      std::move(payload_contents.value()), std::move(shared_info.value()),
      delay_type, std::move(*request_proto.mutable_reporting_path()), debug_key,
      std::move(additional_fields), request_proto.failed_send_attempts());
}

void ConvertPayloadContentsToProto(
    const AggregationServicePayloadContents& payload_contents,
    proto::AggregationServicePayloadContents* out) {
  switch (payload_contents.operation) {
    case AggregationServicePayloadContents::Operation::kHistogram:
      out->set_operation(
          proto::AggregationServicePayloadContents_Operation_HISTOGRAM);
  }

  for (const blink::mojom::AggregatableReportHistogramContribution&
           contribution : payload_contents.contributions) {
    proto::AggregatableReportHistogramContribution* contribution_proto =
        out->add_contributions();
    contribution_proto->set_bucket_high(
        absl::Uint128High64(contribution.bucket));
    contribution_proto->set_bucket_low(absl::Uint128Low64(contribution.bucket));
    contribution_proto->set_value(contribution.value);
    if (contribution.filtering_id.has_value()) {
      contribution_proto->set_filtering_id(contribution.filtering_id.value());
    }
  }

  if (payload_contents.aggregation_coordinator_origin.has_value()) {
    out->set_aggregation_coordinator_origin(
        payload_contents.aggregation_coordinator_origin->Serialize());
  }

  out->set_max_contributions_allowed(
      payload_contents.max_contributions_allowed);

  out->set_filtering_id_max_bytes(payload_contents.filtering_id_max_bytes);
}

void ConvertSharedInfoToProto(const AggregatableReportSharedInfo& shared_info,
                              proto::AggregatableReportSharedInfo* out) {
  out->set_scheduled_report_time(
      shared_info.scheduled_report_time.ToDeltaSinceWindowsEpoch()
          .InMicroseconds());
  out->set_report_id(shared_info.report_id.AsLowercaseString());
  out->set_reporting_origin(shared_info.reporting_origin.Serialize());

  switch (shared_info.debug_mode) {
    case AggregatableReportSharedInfo::DebugMode::kDisabled:
      out->set_debug_mode(
          proto::AggregatableReportSharedInfo_DebugMode_DISABLED);
      break;
    case AggregatableReportSharedInfo::DebugMode::kEnabled:
      out->set_debug_mode(
          proto::AggregatableReportSharedInfo_DebugMode_ENABLED);
      break;
  }

  CHECK(shared_info.additional_fields.empty());

  out->set_api_version(shared_info.api_version);
  out->set_api_identifier(shared_info.api_identifier);
}

proto::AggregatableReportRequest ConvertReportRequestToProto(
    const AggregatableReportRequest& request) {
  proto::AggregatableReportRequest request_proto;
  ConvertPayloadContentsToProto(
      request.payload_contents(),
      /*out=*/request_proto.mutable_payload_contents());
  ConvertSharedInfoToProto(request.shared_info(),
                           /*out=*/request_proto.mutable_shared_info());

  CHECK(request.delay_type().has_value());
  CHECK(proto::AggregatableReportRequest::DelayType_IsValid(
      static_cast<int>(*request.delay_type())));
  request_proto.set_delay_type(
      static_cast<proto::AggregatableReportRequest::DelayType>(
          *request.delay_type()));

  *request_proto.mutable_reporting_path() = request.reporting_path();
  if (request.debug_key().has_value()) {
    request_proto.set_debug_key(request.debug_key().value());
  }
  request_proto.set_failed_send_attempts(request.failed_send_attempts());

  for (auto& elem : request.additional_fields()) {
    (*request_proto.mutable_additional_fields())[elem.first] = elem.second;
  }

  return request_proto;
}

// Note that null filtering IDs are considered to 'fit in' to all max bytes.
bool FilteringIdsFitInMaxBytes(
    std::vector<blink::mojom::AggregatableReportHistogramContribution>
        contributions,
    size_t filtering_id_max_bytes) {
  return std::ranges::none_of(
      contributions,
      [&](const blink::mojom::AggregatableReportHistogramContribution&
              contribution) {
        return static_cast<size_t>(
                   std::bit_width(contribution.filtering_id.value_or(0))) >
               kBitsPerByte * filtering_id_max_bytes;
      });
}

}  // namespace

GURL GetAggregationServiceProcessingUrl(const url::Origin& origin) {
  GURL::Replacements replacements;
  static constexpr std::string_view kEndpointPath =
      ".well-known/aggregation-service/v1/public-keys";
  replacements.SetPathStr(kEndpointPath);
  return origin.GetURL().ReplaceComponents(replacements);
}

AggregationServicePayloadContents::AggregationServicePayloadContents(
    Operation operation,
    std::vector<blink::mojom::AggregatableReportHistogramContribution>
        contributions,
    std::optional<url::Origin> aggregation_coordinator_origin,
    base::StrictNumeric<size_t> max_contributions_allowed,
    size_t filtering_id_max_bytes)
    : operation(operation),
      contributions(std::move(contributions)),
      aggregation_coordinator_origin(std::move(aggregation_coordinator_origin)),
      max_contributions_allowed(max_contributions_allowed),
      filtering_id_max_bytes(filtering_id_max_bytes) {}

AggregationServicePayloadContents::AggregationServicePayloadContents(
    const AggregationServicePayloadContents& other) = default;
AggregationServicePayloadContents& AggregationServicePayloadContents::operator=(
    const AggregationServicePayloadContents& other) = default;
AggregationServicePayloadContents::AggregationServicePayloadContents(
    AggregationServicePayloadContents&& other) = default;
AggregationServicePayloadContents& AggregationServicePayloadContents::operator=(
    AggregationServicePayloadContents&& other) = default;

AggregationServicePayloadContents::~AggregationServicePayloadContents() =
    default;

AggregatableReportSharedInfo::AggregatableReportSharedInfo(
    base::Time scheduled_report_time,
    base::Uuid report_id,
    url::Origin reporting_origin,
    DebugMode debug_mode,
    base::Value::Dict additional_fields,
    std::string api_version,
    std::string api_identifier)
    : scheduled_report_time(scheduled_report_time),
      report_id(std::move(report_id)),
      reporting_origin(std::move(reporting_origin)),
      debug_mode(debug_mode),
      additional_fields(std::move(additional_fields)),
      api_version(std::move(api_version)),
      api_identifier(std::move(api_identifier)) {}

AggregatableReportSharedInfo::AggregatableReportSharedInfo(
    AggregatableReportSharedInfo&& other) = default;
AggregatableReportSharedInfo& AggregatableReportSharedInfo::operator=(
    AggregatableReportSharedInfo&& other) = default;
AggregatableReportSharedInfo::~AggregatableReportSharedInfo() = default;

AggregatableReportSharedInfo AggregatableReportSharedInfo::Clone() const {
  return AggregatableReportSharedInfo(
      scheduled_report_time, report_id, reporting_origin, debug_mode,
      additional_fields.Clone(), api_version, api_identifier);
}

std::string AggregatableReportSharedInfo::SerializeAsJson() const {
  base::Value::Dict value;

  CHECK(report_id.is_valid());
  value.Set("report_id", report_id.AsLowercaseString());

  value.Set("reporting_origin", reporting_origin.Serialize());

  // Encoded as the number of seconds since the Unix epoch, ignoring leap
  // seconds and rounded down.
  CHECK(!scheduled_report_time.is_null());
  CHECK(!scheduled_report_time.is_inf());
  value.Set("scheduled_report_time",
            base::NumberToString(
                scheduled_report_time.InMillisecondsSinceUnixEpoch() /
                base::Time::kMillisecondsPerSecond));

  value.Set("version", api_version);

  value.Set("api", api_identifier);

  // Only include the field if enabled.
  if (debug_mode == DebugMode::kEnabled) {
    value.Set("debug_mode", "enabled");
  }

  CHECK(std::ranges::none_of(additional_fields, [&value](const auto& e) {
    return value.contains(e.first);
  })) << "Additional fields in shared_info cannot duplicate existing fields";

  value.Merge(additional_fields.Clone());

  std::string serialized_value;
  bool succeeded = base::JSONWriter::Write(value, &serialized_value);
  CHECK(succeeded);

  return serialized_value;
}

// static
std::optional<AggregatableReportRequest> AggregatableReportRequest::Create(
    AggregationServicePayloadContents payload_contents,
    AggregatableReportSharedInfo shared_info,
    std::optional<AggregatableReportRequest::DelayType> delay_type,
    std::string reporting_path,
    std::optional<uint64_t> debug_key,
    base::flat_map<std::string, std::string> additional_fields,
    int failed_send_attempts) {
  if (std::optional<GURL> processing_url =
          GetProcessingUrl(payload_contents.aggregation_coordinator_origin);
      processing_url.has_value()) {
    return CreateInternal(*std::move(processing_url),
                          std::move(payload_contents), std::move(shared_info),
                          delay_type, std::move(reporting_path), debug_key,
                          std::move(additional_fields), failed_send_attempts);
  }

  return std::nullopt;
}

// static
std::optional<AggregatableReportRequest>
AggregatableReportRequest::CreateForTesting(
    GURL processing_url,
    AggregationServicePayloadContents payload_contents,
    AggregatableReportSharedInfo shared_info,
    std::optional<AggregatableReportRequest::DelayType> delay_type,
    std::string reporting_path,
    std::optional<uint64_t> debug_key,
    base::flat_map<std::string, std::string> additional_fields,
    int failed_send_attempts) {
  return CreateInternal(std::move(processing_url), std::move(payload_contents),
                        std::move(shared_info), delay_type,
                        std::move(reporting_path), debug_key,
                        std::move(additional_fields), failed_send_attempts);
}

// static
std::optional<AggregatableReportRequest>
AggregatableReportRequest::CreateInternal(
    GURL processing_url,
    AggregationServicePayloadContents payload_contents,
    AggregatableReportSharedInfo shared_info,
    std::optional<AggregatableReportRequest::DelayType> delay_type,
    std::string reporting_path,
    std::optional<uint64_t> debug_key,
    base::flat_map<std::string, std::string> additional_fields,
    int failed_send_attempts) {
  if (!network::IsUrlPotentiallyTrustworthy(processing_url)) {
    DVLOG(1) << "Processing URL is not potentially trustworthy";
    return std::nullopt;
  }

  if (std::ranges::any_of(
          payload_contents.contributions,
          [](const blink::mojom::AggregatableReportHistogramContribution&
                 contribution) { return contribution.value < 0; })) {
    DVLOG(1) << "At least one contribution was less than zero";
    return std::nullopt;
  }

  if (!shared_info.report_id.is_valid()) {
    DVLOG(1) << "Invalid report ID";
    return std::nullopt;
  }

  if (debug_key.has_value() &&
      shared_info.debug_mode ==
          AggregatableReportSharedInfo::DebugMode::kDisabled) {
    DVLOG(1) << "Debug key exists, but debug mode is disabled";
    return std::nullopt;
  }

  if (failed_send_attempts < 0) {
    DVLOG(1) << "Failed send attempts are negative";
    return std::nullopt;
  }

  if (payload_contents.max_contributions_allowed <
      payload_contents.contributions.size()) {
    DVLOG(1) << "Max contributions allowed is smaller than the number of "
                "contributions";
    return std::nullopt;
  }

  if (payload_contents.filtering_id_max_bytes <= 0 ||
      payload_contents.filtering_id_max_bytes >
          AggregationServicePayloadContents::kMaximumFilteringIdMaxBytes) {
    DVLOG(1) << "Value of filtering_id_max_bytes is out of range";
    return std::nullopt;
  }

  if (!FilteringIdsFitInMaxBytes(payload_contents.contributions,
                                 payload_contents.filtering_id_max_bytes)) {
    DVLOG(1) << "Filtering ID does not fit in filtering_id_max_bytes";
    return std::nullopt;
  }

  return AggregatableReportRequest(
      std::move(processing_url), std::move(payload_contents),
      std::move(shared_info), delay_type, std::move(reporting_path), debug_key,
      std::move(additional_fields), failed_send_attempts);
}

AggregatableReportRequest::AggregatableReportRequest(
    GURL processing_url,
    AggregationServicePayloadContents payload_contents,
    AggregatableReportSharedInfo shared_info,
    std::optional<AggregatableReportRequest::DelayType> delay_type,
    std::string reporting_path,
    std::optional<uint64_t> debug_key,
    base::flat_map<std::string, std::string> additional_fields,
    int failed_send_attempts)
    : processing_url_(std::move(processing_url)),
      payload_contents_(std::move(payload_contents)),
      shared_info_(std::move(shared_info)),
      reporting_path_(std::move(reporting_path)),
      debug_key_(debug_key),
      additional_fields_(std::move(additional_fields)),
      failed_send_attempts_(failed_send_attempts),
      delay_type_(delay_type) {}

AggregatableReportRequest::AggregatableReportRequest(
    AggregatableReportRequest&& other) = default;

AggregatableReportRequest& AggregatableReportRequest::operator=(
    AggregatableReportRequest&& other) = default;

AggregatableReportRequest::~AggregatableReportRequest() = default;

GURL AggregatableReportRequest::GetReportingUrl() const {
  if (reporting_path_.empty()) {
    return GURL();
  }
  return shared_info().reporting_origin.GetURL().Resolve(reporting_path_);
}

std::optional<AggregatableReportRequest> AggregatableReportRequest::Deserialize(
    base::span<const uint8_t> serialized_proto) {
  proto::AggregatableReportRequest request_proto;
  if (!request_proto.ParseFromArray(serialized_proto.data(),
                                    serialized_proto.size())) {
    return std::nullopt;
  }

  return ConvertReportRequestFromProto(std::move(request_proto));
}

std::vector<uint8_t> AggregatableReportRequest::Serialize() const {
  proto::AggregatableReportRequest request_proto =
      ConvertReportRequestToProto(*this);

  size_t size = request_proto.ByteSizeLong();
  std::vector<uint8_t> serialized_proto(size);
  if (!request_proto.SerializeToArray(serialized_proto.data(), size)) {
    return {};
  }

  return serialized_proto;
}

AggregatableReport::AggregationServicePayload::AggregationServicePayload(
    std::vector<uint8_t> payload,
    std::string key_id,
    std::optional<std::vector<uint8_t>> debug_cleartext_payload)
    : payload(std::move(payload)),
      key_id(std::move(key_id)),
      debug_cleartext_payload(std::move(debug_cleartext_payload)) {}

AggregatableReport::AggregationServicePayload::AggregationServicePayload(
    const AggregatableReport::AggregationServicePayload& other) = default;
AggregatableReport::AggregationServicePayload&
AggregatableReport::AggregationServicePayload::operator=(
    const AggregatableReport::AggregationServicePayload& other) = default;
AggregatableReport::AggregationServicePayload::AggregationServicePayload(
    AggregatableReport::AggregationServicePayload&& other) = default;
AggregatableReport::AggregationServicePayload&
AggregatableReport::AggregationServicePayload::operator=(
    AggregatableReport::AggregationServicePayload&& other) = default;
AggregatableReport::AggregationServicePayload::~AggregationServicePayload() =
    default;

AggregatableReport::AggregatableReport(
    std::optional<AggregationServicePayload> payload,
    std::string shared_info,
    std::optional<uint64_t> debug_key,
    base::flat_map<std::string, std::string> additional_fields,
    std::optional<url::Origin> aggregation_coordinator_origin)
    : payload_(std::move(payload)),
      shared_info_(std::move(shared_info)),
      debug_key_(debug_key),
      additional_fields_(std::move(additional_fields)),
      aggregation_coordinator_origin_(
          std::move(aggregation_coordinator_origin)) {}

AggregatableReport::AggregatableReport(const AggregatableReport& other) =
    default;

AggregatableReport& AggregatableReport::operator=(
    const AggregatableReport& other) = default;

AggregatableReport::AggregatableReport(AggregatableReport&& other) = default;

AggregatableReport& AggregatableReport::operator=(AggregatableReport&& other) =
    default;

AggregatableReport::~AggregatableReport() = default;

// static
bool AggregatableReport::Provider::g_disable_encryption_for_testing_tool_ =
    false;

// static
void AggregatableReport::Provider::SetDisableEncryptionForTestingTool(
    bool should_disable) {
  g_disable_encryption_for_testing_tool_ = should_disable;
}

AggregatableReport::Provider::~Provider() = default;

std::optional<AggregatableReport>
AggregatableReport::Provider::CreateFromRequestAndPublicKey(
    const AggregatableReportRequest& report_request,
    PublicKey public_key) const {
  std::optional<std::vector<uint8_t>> unencrypted_payload =
      ConstructUnencryptedPayload(report_request.payload_contents());
  if (!unencrypted_payload.has_value()) {
    return std::nullopt;
  }

  // Validate that the payload length is a deterministic function of
  // `max_contributions_allowed` and `filtering_id_max_bytes`.
  const size_t expected_payload_length =
      ComputePayloadLengthInBytes(
          report_request.payload_contents().max_contributions_allowed,
          report_request.payload_contents().filtering_id_max_bytes)
          .value();
  CHECK_EQ(unencrypted_payload->size(), expected_payload_length);

  std::string encoded_shared_info =
      report_request.shared_info().SerializeAsJson();

  std::string authenticated_info_str =
      base::StrCat({kDomainSeparationPrefix, encoded_shared_info});
  base::span<const uint8_t> authenticated_info =
      base::as_byte_span(authenticated_info_str);

  std::vector<uint8_t> encrypted_payload =
      g_disable_encryption_for_testing_tool_
          ? *unencrypted_payload
          : EncryptAggregatableReportPayloadWithHpke(
                /*report_payload_plaintext=*/*unencrypted_payload,
                /*public_key=*/public_key.key,
                /*report_authenticated_info=*/authenticated_info);

  if (encrypted_payload.empty()) {
    return std::nullopt;
  }

  std::optional<std::vector<uint8_t>> debug_cleartext_payload;
  if (report_request.shared_info().debug_mode ==
      AggregatableReportSharedInfo::DebugMode::kEnabled) {
    debug_cleartext_payload = *std::move(unencrypted_payload);
  }

  return AggregatableReport(
      AggregationServicePayload(std::move(encrypted_payload),
                                std::move(public_key).id,
                                std::move(debug_cleartext_payload)),
      std::move(encoded_shared_info), report_request.debug_key(),
      report_request.additional_fields(),
      report_request.payload_contents().aggregation_coordinator_origin);
}

base::Value::Dict AggregatableReport::GetAsJson() const {
  base::Value::Dict value;

  value.Set("shared_info", shared_info_);

  // When invoked for reports being shown in the WebUI, `payload_` may be
  // `std::nullopt` prior to assembly or if assembly failed.
  if (payload_.has_value()) {
    base::Value::Dict payload_dict_value;
    payload_dict_value.Set("payload", base::Base64Encode(payload_->payload));
    payload_dict_value.Set("key_id", payload_->key_id);
    if (payload_->debug_cleartext_payload.has_value()) {
      payload_dict_value.Set(
          "debug_cleartext_payload",
          base::Base64Encode(payload_->debug_cleartext_payload.value()));
    }

    value.Set("aggregation_service_payloads",
              base::Value::List().Append(std::move(payload_dict_value)));
  }

  if (debug_key_.has_value()) {
    value.Set("debug_key", base::NumberToString(debug_key_.value()));
  }

  value.Set(
      "aggregation_coordinator_origin",
      aggregation_coordinator_origin_
          .value_or(
              ::aggregation_service::GetDefaultAggregationCoordinatorOrigin())
          .Serialize());

  for (const auto& item : additional_fields_) {
    CHECK(!value.contains(item.first))
        << "Additional field duplicates existing field: " << item.first;
    value.Set(item.first, item.second);
  }

  return value;
}

// static
std::optional<std::vector<uint8_t>>
AggregatableReport::SerializePayloadForTesting(
    const AggregationServicePayloadContents& payload_contents) {
  return ConstructUnencryptedPayload(payload_contents);
}

// static
std::optional<size_t> AggregatableReport::ComputePayloadLengthInBytesForTesting(
    size_t num_contributions,
    size_t filtering_id_max_bytes) {
  return ComputePayloadLengthInBytes(num_contributions, filtering_id_max_bytes);
}

std::vector<uint8_t> EncryptAggregatableReportPayloadWithHpke(
    base::span<const uint8_t> report_payload_plaintext,
    base::span<const uint8_t> public_key,
    base::span<const uint8_t> report_authenticated_info) {
  bssl::ScopedEVP_HPKE_CTX sender_context;

  // This vector will hold the encapsulated shared secret "enc" followed by the
  // symmetrically encrypted ciphertext "ct".
  std::vector<uint8_t> payload(EVP_HPKE_MAX_ENC_LENGTH);
  size_t encapsulated_shared_secret_len;

  CHECK_EQ(public_key.size(), PublicKey::kKeyByteLength);

  if (!EVP_HPKE_CTX_setup_sender(
          /*ctx=*/sender_context.get(),
          /*out_enc=*/payload.data(),
          /*out_enc_len=*/&encapsulated_shared_secret_len,
          /*max_enc=*/payload.size(),
          /*kem=*/EVP_hpke_x25519_hkdf_sha256(), /*kdf=*/EVP_hpke_hkdf_sha256(),
          /*aead=*/EVP_hpke_chacha20_poly1305(),
          /*peer_public_key=*/public_key.data(),
          /*peer_public_key_len=*/public_key.size(),
          /*info=*/report_authenticated_info.data(),
          /*info_len=*/report_authenticated_info.size())) {
    return {};
  }

  payload.resize(encapsulated_shared_secret_len +
                 report_payload_plaintext.size() +
                 EVP_HPKE_CTX_max_overhead(sender_context.get()));

  base::span<uint8_t> ciphertext =
      base::span(payload).subspan(encapsulated_shared_secret_len);
  size_t ciphertext_len;

  if (!EVP_HPKE_CTX_seal(
          /*ctx=*/sender_context.get(), /*out=*/ciphertext.data(),
          /*out_len=*/&ciphertext_len,
          /*max_out_len=*/ciphertext.size(),
          /*in=*/report_payload_plaintext.data(),
          /*in_len*/ report_payload_plaintext.size(),
          /*ad=*/nullptr,
          /*ad_len=*/0)) {
    return {};
  }
  payload.resize(encapsulated_shared_secret_len + ciphertext_len);

  return payload;
}

}  // namespace content
