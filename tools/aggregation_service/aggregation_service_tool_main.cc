// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

#include "base/command_line.h"
#include "base/containers/contains.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/values.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "tools/aggregation_service/aggregation_service_tool.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_canon.h"

namespace {

// If you change any of the switch strings, update the `kHelpMsg`,
// `kAllowedSwitches` and `kRequiredSwitches` accordingly.
constexpr std::string_view kSwitchHelp = "help";
constexpr std::string_view kSwitchHelpShort = "h";
constexpr std::string_view kSwitchOperation = "operation";
constexpr std::string_view kSwitchBucket = "bucket";
constexpr std::string_view kSwitchValue = "value";
constexpr std::string_view kSwitchReportingOrigin = "reporting-origin";
constexpr std::string_view kSwitchHelperKeyUrl = "helper-key-url";
constexpr std::string_view kSwitchHelperKeyFile = "helper-key-file";
constexpr std::string_view kSwitchOutputFile = "output-file";
constexpr std::string_view kSwitchOutputUrl = "output-url";
constexpr std::string_view kSwitchDisablePayloadEncryption =
    "disable-payload-encryption";
constexpr std::string_view kSwitchAdditionalFields = "additional-fields";
constexpr std::string_view kSwitchAdditionalSharedInfoFields =
    "additional-shared-info-fields";
constexpr std::string_view kSwitchEnableDebugMode = "enable-debug-mode";
constexpr std::string_view kSwitchApiVersion = "api-version";
constexpr std::string_view kSwitchApi = "api";

constexpr std::string_view kHelpMsg = R"(
  aggregation_service_tool [--operation=<operation>] --bucket=<bucket>
  --value=<value>
  --reporting-origin=<reporting_origin>
  --helper-key-url=<helper_key_url> (or --helper-key-file=<helper_key_file>)
  [--output=<output_file>]
  [--output-url=<output_url>] [--disable-payload-encryption]
  [--additional-fields=<additional_fields>]
  [--additional-shared-info-fields=<additional_shared_info_fields]
  [--debug-mode] [--api-version=<api_version>] [--api=<api_identifier>]

  Examples:
  aggregation_service_tool --operation="histogram" --bucket=1234 --value=5
  --reporting-origin="https://example.com"
  --helper-key-url="https://a.com/keys.json"
  --output-file="output.json" --enable-debug-mode --api-version="1.0"
  --api="attribution-reporting" --additional-fields=
  "source_site=https://publisher.example,attribution_destination=https://advertiser.example"
  or
  aggregation_service_tool --bucket=1234 --value=5
  --reporting-origin="https://example.com"
  --helper-key-file="keys.json"
  --output-url="https://c.com/reports"

  aggregation_service_tool is a command-line tool that accepts report contents
  and mapping of origins to public key json files as input and either output an
  aggregatable report to a file on disk or send the aggregatable report to an
  endpoint origin over network. `scheduled_report_time` will be default to 30
  seconds later.

  Switches:
  --operation = Optional switch. Currently only supports "histogram". Default is
                "histogram".
  --bucket = Bucket key of the histogram contribution, must be non-negative
             integer.
  --value = Bucket value of the histogram contribution, must be non-negative
            integer.
  --reporting-origin = The reporting origin endpoint.
  --helper-key-url = Optional switch to specify the URL to fetch the public key
                     json file from. Either this or "--helper-key-file" must be
                     specified.
  --helper-key-file = Optional switch to specify the local public key json file
                      to use. Either this or "--helper-key-url" must be
                      specified.
  --output-file = Optional switch to specify the output file path. Eiter this or
                  "--output-url" must be specified.
  --output-url = Optional switch to specify the output url. Eiter this or
                  "--output-file" must be specified.
  --additional-fields = List of key-value pairs of additional fields to be
                        included in the aggregatable report. Only supports
                        string valued fields.
  --additional-shared-info-fields = List of key-value pairs of additional
                                    fields to be included in the aggregatable
                                    report's shared_info dictionary.
                                    Only supports string valued fields.
  --disable-payload-encryption = Optional switch. If provided, the aggregatable
                                 report's payload(s) will not be encrypted after
                                 serialization.
  --enable-debug-mode = Optional switch. If provided, debug mode is enabled.
                        Otherwise, it is disabled.
  --api-version = Optional switch to specify the API version. Default is "".
  --api = Optional switch to specify the enum string identifying which API
          created the report. Default is "attribution-reporting".
)";

void PrintHelp() {
  LOG(INFO) << kHelpMsg;
}

}  // namespace

int main(int argc, char* argv[]) {
  base::SingleThreadTaskExecutor executor(base::MessagePumpType::IO);
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams(
      "aggregation_service_tool");

  base::CommandLine::Init(argc, argv);
  base::CommandLine& command_line = *base::CommandLine::ForCurrentProcess();

  base::CommandLine::StringVector args = command_line.GetArgs();

  if (args.size() != 0U) {
    LOG(ERROR)
        << "aggregation_service_tool does not expect any additional arguments.";
    PrintHelp();
    return 1;
  }

  const std::vector<std::string_view> kAllowedSwitches = {
      kSwitchHelp,
      kSwitchHelpShort,
      kSwitchOperation,
      kSwitchBucket,
      kSwitchValue,
      kSwitchReportingOrigin,
      kSwitchHelperKeyUrl,
      kSwitchHelperKeyFile,
      kSwitchOutputFile,
      kSwitchOutputUrl,
      kSwitchDisablePayloadEncryption,
      kSwitchAdditionalFields,
      kSwitchAdditionalSharedInfoFields,
      kSwitchEnableDebugMode,
      kSwitchApiVersion,
      kSwitchApi};
  for (const auto& provided_switch : command_line.GetSwitches()) {
    if (!base::Contains(kAllowedSwitches, provided_switch.first)) {
      LOG(ERROR) << "aggregation_service_tool did not expect "
                 << provided_switch.first << " to be specified.";
      PrintHelp();
      return 1;
    }
  }

  if (command_line.GetSwitches().empty() ||
      command_line.HasSwitch(kSwitchHelp) ||
      command_line.HasSwitch(kSwitchHelpShort)) {
    PrintHelp();
    return 1;
  }

  const std::vector<std::string_view> kRequiredSwitches = {
      kSwitchBucket, kSwitchValue, kSwitchReportingOrigin};
  for (std::string_view required_switch : kRequiredSwitches) {
    if (!command_line.HasSwitch(required_switch)) {
      LOG(ERROR) << "aggregation_service_tool expects " << required_switch
                 << " to be specified.";
      PrintHelp();
      return 1;
    }
  }

  // Either output or reporting url should be specified, but not both.
  if (!(command_line.HasSwitch(kSwitchOutputFile) ^
        command_line.HasSwitch(kSwitchOutputUrl))) {
    LOG(ERROR) << "aggregation_service_tool expects either "
               << kSwitchOutputFile << " or " << kSwitchOutputUrl
               << " to be specified, but not both.";
    PrintHelp();
    return 1;
  }

  // Either helper key URL or file should be specified, but not both.
  if (!(command_line.HasSwitch(kSwitchHelperKeyUrl) ^
        command_line.HasSwitch(kSwitchHelperKeyFile))) {
    LOG(ERROR) << "aggregation_service_tool expects either "
               << kSwitchHelperKeyUrl << " or " << kSwitchHelperKeyFile
               << " to be specified, but not both.";
    PrintHelp();
    return 1;
  }

  aggregation_service::AggregationServiceTool tool;

  tool.SetDisablePayloadEncryption(
      /*should_disable=*/command_line.HasSwitch(
          kSwitchDisablePayloadEncryption));

  GURL processing_url;

  if (command_line.HasSwitch(kSwitchHelperKeyUrl)) {
    std::string url_string =
        command_line.GetSwitchValueASCII(kSwitchHelperKeyUrl);
    processing_url = GURL(url_string);
    if (!network::IsUrlPotentiallyTrustworthy(processing_url)) {
      LOG(ERROR) << "Helper key URL " << url_string
                 << " is not potentially trustworthy.";
      return 1;
    }
  } else {
    std::string file_string =
        command_line.GetSwitchValueASCII(kSwitchHelperKeyFile);

    // We need to choose some URL to store the set of public keys under.
    aggregation_service::UrlKeyFile key_file(
        GURL("https://fake.example/keys.json"), file_string);

    if (!tool.SetPublicKeys(key_file)) {
      LOG(ERROR) << "aggregation_service_tool failed to set public keys.";
      return 1;
    }

    processing_url = std::move(key_file.url);
  }

  std::string operation =
      command_line.HasSwitch(kSwitchOperation)
          ? command_line.GetSwitchValueASCII(kSwitchOperation)
          : "histogram";

  url::Origin reporting_origin = url::Origin::Create(
      GURL(command_line.GetSwitchValueASCII(kSwitchReportingOrigin)));

  bool is_debug_mode_enabled = command_line.HasSwitch(kSwitchEnableDebugMode);

  base::Value::Dict additional_shared_info_fields;
  if (command_line.HasSwitch(kSwitchAdditionalSharedInfoFields)) {
    std::string additional_shared_info_fields_str =
        command_line.GetSwitchValueASCII(kSwitchAdditionalSharedInfoFields);
    // `additional_shared_info_fields_str` is formatted like
    // "key1=value1,key2=value2".
    base::StringPairs kv_pairs;
    base::SplitStringIntoKeyValuePairs(
        additional_shared_info_fields_str, /*key_value_delimiter=*/'=',
        /*key_value_pair_delimiter=*/',', &kv_pairs);
    for (std::pair<std::string, std::string>& kv : kv_pairs) {
      additional_shared_info_fields.Set(std::move(kv.first),
                                        std::move(kv.second));
    }
  }

  std::string api_version =
      command_line.HasSwitch(kSwitchApiVersion)
          ? command_line.GetSwitchValueASCII(kSwitchApiVersion)
          : "";

  std::string api_identifier =
      command_line.HasSwitch(kSwitchApi)
          ? command_line.GetSwitchValueASCII(kSwitchApi)
          : "attribution-reporting";

  base::Value::Dict report_dict = tool.AssembleReport(
      std::move(operation), command_line.GetSwitchValueASCII(kSwitchBucket),
      command_line.GetSwitchValueASCII(kSwitchValue),
      std::move(reporting_origin), std::move(processing_url),
      is_debug_mode_enabled, std::move(additional_shared_info_fields),
      std::move(api_version), std::move(api_identifier));
  if (report_dict.empty()) {
    LOG(ERROR)
        << "aggregation_service_tool failed to create the aggregatable report.";
    return 1;
  }

  if (command_line.HasSwitch(kSwitchAdditionalFields)) {
    std::string additional_fields =
        command_line.GetSwitchValueASCII(kSwitchAdditionalFields);
    // `additional_fields` is formatted like "key1=value1,key2=value2".
    base::StringPairs kv_pairs;
    base::SplitStringIntoKeyValuePairs(
        additional_fields, /*key_value_delimiter=*/'=',
        /*key_value_pair_delimiter=*/',', &kv_pairs);
    for (std::pair<std::string, std::string>& kv : kv_pairs) {
      report_dict.Set(std::move(kv.first), std::move(kv.second));
    }
  }

  base::Value report_contents(std::move(report_dict));

  bool succeeded = false;
  if (command_line.HasSwitch(kSwitchOutputFile)) {
    base::FilePath output_file =
        command_line.GetSwitchValuePath(kSwitchOutputFile);
    succeeded = tool.WriteReportToFile(report_contents, output_file);

    if (!succeeded) {
      LOG(ERROR) << "aggregation_service_tool failed to write to "
                 << output_file << ".";
    }
  } else {
    std::string output_url = command_line.GetSwitchValueASCII(kSwitchOutputUrl);
    succeeded = tool.SendReport(report_contents, GURL(output_url));

    if (!succeeded) {
      LOG(ERROR) << "aggregation_service_tool failed to send the report to "
                 << output_url << ".";
    }
  }

  if (!succeeded) {
    return 1;
  }

  return 0;
}
