// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/safe_browsing/download_protection/deep_scanning_request.h"

#include <unordered_map>

#include "base/containers/contains.h"
#include "base/containers/flat_map.h"
#include "base/containers/span.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_forward.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/download/chrome_download_manager_delegate.h"
#include "chrome/browser/download/download_core_service.h"
#include "chrome/browser/download/download_core_service_factory.h"
#include "chrome/browser/download/download_item_warning_data.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/enterprise/connectors/common.h"
#include "chrome/browser/enterprise/connectors/connectors_service.h"
#include "chrome/browser/enterprise/connectors/reporting/realtime_reporting_client_factory.h"
#include "chrome/browser/enterprise/connectors/test/deep_scanning_test_utils.h"
#include "chrome/browser/extensions/api/safe_browsing_private/safe_browsing_private_event_router.h"
#include "chrome/browser/extensions/api/safe_browsing_private/safe_browsing_private_event_router_factory.h"
#include "chrome/browser/policy/dm_token_utils.h"
#include "chrome/browser/safe_browsing/cloud_content_scanning/cloud_binary_upload_service.h"
#include "chrome/browser/safe_browsing/cloud_content_scanning/deep_scanning_utils.h"
#include "chrome/browser/safe_browsing/download_protection/download_item_metadata.h"
#include "chrome/browser/safe_browsing/download_protection/download_protection_service.h"
#include "chrome/browser/safe_browsing/download_protection/download_protection_util.h"
#include "chrome/browser/safe_browsing/download_protection/file_system_access_metadata.h"
#include "chrome/browser/safe_browsing/test_extension_event_observer.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "components/download/public/common/mock_download_item.h"
#include "components/enterprise/common/proto/connectors.pb.h"
#include "components/enterprise/connectors/core/connectors_prefs.h"
#include "components/policy/core/common/cloud/dm_token.h"
#include "components/policy/core/common/cloud/mock_cloud_policy_client.h"
#include "components/policy/core/common/policy_pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/safe_browsing/core/common/features.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "components/signin/public/identity_manager/identity_test_environment.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "crypto/sha2.h"
#include "net/base/mime_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "chromeos/ash/components/system/fake_statistics_provider.h"
#endif

namespace safe_browsing {

using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::ReturnRefOfCopy;

namespace {

constexpr char kUserName[] = "test@chromium.org";

constexpr char kScanForDlpAndMalware[] = R"(
{
  "service_provider": "google",
  "enable": [
    {
      "url_list": ["*"],
      "tags": ["dlp", "malware"]
    }
  ],
  "block_until_verdict": 1
})";

constexpr char kScanForMalware[] = R"(
{
  "service_provider": "google",
  "enable": [
    {
      "url_list": ["*"],
      "tags": ["malware"]
    }
  ],
  "block_until_verdict": 1
})";

constexpr char kScanForDlp[] = R"(
{
  "service_provider": "google",
  "enable": [
    {
      "url_list": ["*"],
      "tags": ["dlp"]
    }
  ],
  "block_until_verdict": 1
})";

constexpr char kNoScan[] = R"({"service_provider": "google"})";

const std::set<std::string>* ExeMimeTypes() {
  static std::set<std::string> set = {"application/x-msdownload",
                                      "application/x-ms-dos-executable",
                                      "application/octet-stream"};
  return &set;
}

const std::set<std::string>* TxtMimeTypes() {
  static std::set<std::string> set = {"text/plain"};
  return &set;
}

constexpr char kScanId[] = "scan_id";

}  // namespace

// Enum to select item type used for deep scanning metadata.
enum class MetadataSourceType { kDownloadItem, kFileSystemAccessWriteItem };

class FakeBinaryUploadService : public BinaryUploadService {
 public:
  void MaybeUploadForDeepScanning(std::unique_ptr<Request> request) override {
    last_request_ = request->content_analysis_request();
    const std::string& filename = request->filename();
    request->FinishRequest(saved_results_[filename],
                           saved_responses_[filename]);

    if (!quit_on_last_request_.is_null()) {
      if (++num_finished_requests_ == saved_responses_.size()) {
        quit_on_last_request_.Run();
      }
    }
  }

  void MaybeAcknowledge(std::unique_ptr<Ack> ack) override {
    EXPECT_EQ(final_action_, ack->ack().final_action());
    ++num_acks_;
    ASSERT_TRUE(base::Contains(requests_tokens_, ack->ack().request_token()));
  }

  void MaybeCancelRequests(std::unique_ptr<CancelRequests> cancel) override {}

  base::WeakPtr<BinaryUploadService> AsWeakPtr() override {
    return weak_ptr_factory_.GetWeakPtr();
  }

  void SetResponse(const base::FilePath& path,
                   BinaryUploadService::Result result,
                   enterprise_connectors::ContentAnalysisResponse response) {
    saved_results_[path.AsUTF8Unsafe()] = result;
    saved_responses_[path.AsUTF8Unsafe()] = response;
    requests_tokens_.push_back(response.request_token());
  }

  const enterprise_connectors::ContentAnalysisRequest& last_request() {
    return last_request_;
  }

  void SetQuitOnLastRequest(base::RepeatingClosure closure) {
    quit_on_last_request_ = std::move(closure);
  }

  void SetExpectedFinalAction(
      enterprise_connectors::ContentAnalysisAcknowledgement::FinalAction
          final_action) {
    final_action_ = final_action;
  }

  size_t num_finished_requests() { return num_finished_requests_; }
  size_t num_acks() { return num_acks_; }

  void Reset() {
    saved_results_.clear();
    saved_responses_.clear();
    requests_tokens_.clear();
    num_finished_requests_ = 0;
    num_acks_ = 0;
  }

 private:
  base::flat_map<std::string, BinaryUploadService::Result> saved_results_;
  base::flat_map<std::string, enterprise_connectors::ContentAnalysisResponse>
      saved_responses_;
  enterprise_connectors::ContentAnalysisRequest last_request_;
  std::vector<std::string> requests_tokens_;
  enterprise_connectors::ContentAnalysisAcknowledgement::FinalAction
      final_action_ = enterprise_connectors::ContentAnalysisAcknowledgement::
          ACTION_UNSPECIFIED;

  base::RepeatingClosure quit_on_last_request_;
  size_t num_finished_requests_ = 0;
  size_t num_acks_ = 0;
  base::WeakPtrFactory<FakeBinaryUploadService> weak_ptr_factory_{this};
};

class FakeDownloadProtectionService : public DownloadProtectionService {
 public:
  FakeDownloadProtectionService() : DownloadProtectionService(nullptr) {}

  void RequestFinished(DeepScanningRequest* request) override {}

  BinaryUploadService* GetBinaryUploadService(
      Profile* profile,
      const enterprise_connectors::AnalysisSettings&) override {
    return &binary_upload_service_;
  }

  FakeBinaryUploadService* GetFakeBinaryUploadService() {
    return &binary_upload_service_;
  }

 private:
  FakeBinaryUploadService binary_upload_service_;
};

class DeepScanningRequestTest : public testing::Test {
 public:
  void SetUp() override {
    SetFeatures(
        /*enabled=*/{safe_browsing::kEnhancedFieldsForSecOps},
        /*disabled=*/{});
    profile_manager_ = std::make_unique<TestingProfileManager>(
        TestingBrowserProcess::GetGlobal());
    EXPECT_TRUE(profile_manager_->SetUp());
    profile_ = profile_manager_->CreateTestingProfile("test-user");

    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    download_path_ = temp_dir_.GetPath().AppendASCII("download.exe");
    for (const char* file_name : {"foo.txt", "bar.txt", "baz.txt"}) {
      base::FilePath final_path = temp_dir_.GetPath().AppendASCII(file_name);
      base::FilePath current_path =
          temp_dir_.GetPath().AppendASCII(base::StrCat({file_name, ".tmp"}));
      base::File file(current_path,
                      base::File::FLAG_CREATE | base::File::FLAG_WRITE);
      file.WriteAtCurrentPos(base::as_byte_span(std::string_view(file_name)));
      secondary_files_.push_back(current_path);
      secondary_files_targets_.push_back(final_path);
    }

    std::string download_contents = "download contents";
    download_hash_ = crypto::SHA256HashString(download_contents);
    tab_url_string_ = "https://example.com/";
    download_url_ = GURL("https://example.com/download.exe");
    tab_url_ = GURL(tab_url_string_);

    base::File download(download_path_,
                        base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    download.WriteAtCurrentPos(base::as_byte_span(download_contents));
    download.Close();

    EXPECT_CALL(item_, GetFullPath()).WillRepeatedly(ReturnRef(download_path_));
    EXPECT_CALL(item_, GetTotalBytes())
        .WillRepeatedly(Return(download_contents.size()));
    EXPECT_CALL(item_, GetURL()).WillRepeatedly(ReturnRef(download_url_));
    EXPECT_CALL(item_, GetTabUrl()).WillRepeatedly(ReturnRef(tab_url_));
    EXPECT_CALL(item_, GetHash()).WillRepeatedly(ReturnRef(download_hash_));
    EXPECT_CALL(item_, GetTargetFilePath())
        .WillRepeatedly(ReturnRef(download_path_));
    EXPECT_CALL(item_, GetMimeType())
        .WillRepeatedly(Return("application/octet-stream"));
    EXPECT_CALL(item_, GetUrlChain())
        .WillRepeatedly(ReturnRefOfCopy(std::vector<GURL>()));
    EXPECT_CALL(item_, GetTabReferrerUrl())
        .WillRepeatedly(ReturnRefOfCopy(GURL()));
    EXPECT_CALL(item_, GetDangerType())
        .WillRepeatedly(Return(download::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS));
    EXPECT_CALL(item_, GetReceivedBytes()).WillRepeatedly(Return(0));
    EXPECT_CALL(item_, HasUserGesture()).WillRepeatedly(Return(false));
    EXPECT_CALL(item_, RequireSafetyChecks()).WillRepeatedly(Return(true));
    content::DownloadItemUtils::AttachInfoForTesting(&item_, profile_, nullptr);

    SetDMTokenForTesting(policy::DMToken::CreateValidToken("dm_token"));

    DownloadCoreServiceFactory::GetForBrowserContext(profile_)
        ->SetDownloadManagerDelegateForTesting(
            std::make_unique<ChromeDownloadManagerDelegate>(profile_));

    // Default source type for `DownloadItem` only tests.
    metadata_source_type_ = MetadataSourceType::kDownloadItem;

    // Create test web contents for setting FSA tab url.
    web_contents_ =
        content::WebContentsTester::CreateTestWebContents(profile_, nullptr);
    content::WebContentsTester::For(web_contents_.get())
        ->SetLastCommittedURL(tab_url_);
  }

  void TearDown() override {
    SetDMTokenForTesting(policy::DMToken::CreateEmptyToken());
    web_contents_.reset();
  }

  void AddUrlToProfilePrefList(const char* pref_name, const GURL& url) {
    ScopedListPrefUpdate(profile_->GetPrefs(), pref_name)->Append(url.host());
  }

  void SetFeatures(const std::vector<base::test::FeatureRef>& enabled,
                   const std::vector<base::test::FeatureRef>& disabled) {
    scoped_feature_list_.Reset();
    scoped_feature_list_.InitWithFeatures(enabled, disabled);
  }

  void ValidateDefaultSettings(
      const std::optional<enterprise_connectors::AnalysisSettings>& settings) {
    ASSERT_TRUE(settings.has_value());

    enterprise_connectors::AnalysisSettings default_settings;
    default_settings.tags = {{"malware", enterprise_connectors::TagSettings()}};
    enterprise_connectors::CloudAnalysisSettings cloud_settings;
    cloud_settings.analysis_url =
        GURL("https://safebrowsing.google.com/safebrowsing/uploads/scan");
    default_settings.cloud_or_local_settings =
        enterprise_connectors::CloudOrLocalAnalysisSettings(
            std::move(cloud_settings));
    default_settings.block_until_verdict =
        enterprise_connectors::BlockUntilVerdict::kBlock;

    for (const auto& tag : settings.value().tags) {
      ASSERT_EQ(tag.second.requires_justification,
                default_settings.tags[tag.first].requires_justification);
      ASSERT_EQ(tag.second.custom_message.message,
                default_settings.tags[tag.first].custom_message.message);
      ASSERT_EQ(tag.second.custom_message.learn_more_url,
                default_settings.tags[tag.first].custom_message.learn_more_url);
    }
    ASSERT_EQ(settings.value().block_large_files,
              default_settings.block_large_files);
    ASSERT_EQ(settings.value().block_password_protected_files,
              default_settings.block_password_protected_files);
    ASSERT_EQ(settings.value().block_until_verdict,
              default_settings.block_until_verdict);
    ASSERT_EQ(settings.value().cloud_or_local_settings.analysis_url(),
              default_settings.cloud_or_local_settings.analysis_url());
  }

  void SetLastResult(DownloadCheckResult result) { last_result_ = result; }

  std::optional<enterprise_connectors::AnalysisSettings> settings() {
    std::unique_ptr<DeepScanningMetadata> metadata = CreateMetadata();
    return DeepScanningRequest::ShouldUploadBinary(*metadata);
  }

  std::unique_ptr<DeepScanningMetadata> CreateMetadata() {
    switch (metadata_source_type_) {
      case MetadataSourceType::kDownloadItem:
        return std::make_unique<DownloadItemMetadata>(&item_);
      case MetadataSourceType::kFileSystemAccessWriteItem:
        auto write_item =
            std::make_unique<content::FileSystemAccessWriteItem>();
        write_item->target_file_path = download_path_;
        write_item->full_path = download_path_;
        write_item->frame_url = download_url_;
        write_item->browser_context = profile_;
        write_item->web_contents = web_contents_->GetWeakPtr();
        write_item->size = item_.GetTotalBytes();
        write_item->sha256_hash = download_hash_;
        write_item->has_user_gesture = false;

        return std::make_unique<FileSystemAccessMetadata>(
            std::move(write_item));
    }
    return nullptr;
  }

  void SetMetadataSourceType(MetadataSourceType type) {
    metadata_source_type_ = type;
  }

  TestingProfile* profile() { return profile_; }

 protected:
  base::test::ScopedFeatureList scoped_feature_list_;
  content::BrowserTaskEnvironment task_environment_;
  std::unique_ptr<TestingProfileManager> profile_manager_;
  raw_ptr<TestingProfile> profile_;

  FakeDownloadProtectionService download_protection_service_;
  download::MockDownloadItem item_;

  base::ScopedTempDir temp_dir_;
  base::FilePath download_path_;
  std::vector<base::FilePath> secondary_files_;
  std::vector<base::FilePath> secondary_files_targets_;
  GURL download_url_;
  GURL tab_url_;
  std::string tab_url_string_;
  std::string download_hash_;

  DownloadCheckResult last_result_;

  MetadataSourceType metadata_source_type_;
  std::unique_ptr<content::WebContents> web_contents_;
  content::RenderViewHostTestEnabler enabler_;
};

class DeepScanningRequestSourceTypeTest
    : public DeepScanningRequestTest,
      public testing::WithParamInterface<MetadataSourceType> {
 public:
  void SetUp() override {
    DeepScanningRequestTest::SetUp();
    SetMetadataSourceType(GetParam());
  }
};

class DeepScanningRequestFeaturesEnabledTest
    : public DeepScanningRequestSourceTypeTest {};

TEST_P(DeepScanningRequestFeaturesEnabledTest, ChecksFeatureFlags) {
  enterprise_connectors::test::SetAnalysisConnector(
      profile_->GetPrefs(), enterprise_connectors::FILE_DOWNLOADED,
      kScanForDlpAndMalware);

  // Try each request with settings indicating both DLP and Malware requests
  // should be sent to show features work correctly.
  auto dlp_and_malware_settings = []() {
    enterprise_connectors::AnalysisSettings settings;
    settings.tags = {{"dlp", enterprise_connectors::TagSettings()},
                     {"malware", enterprise_connectors::TagSettings()}};
    settings.block_until_verdict =
        enterprise_connectors::BlockUntilVerdict::kBlock;
    return settings;
  };

  // A request using the Connector protos doesn't account for the 2 legacy
  // feature flags, so the 2 tags should always stay.
  auto expect_dlp_and_malware_tags = [this]() {
    EXPECT_EQ(2, download_protection_service_.GetFakeBinaryUploadService()
                     ->last_request()
                     .tags_size());

    EXPECT_EQ("dlp", download_protection_service_.GetFakeBinaryUploadService()
                         ->last_request()
                         .tags(0));
    EXPECT_EQ("malware",
              download_protection_service_.GetFakeBinaryUploadService()
                  ->last_request()
                  .tags(1));
    EXPECT_TRUE(download_protection_service_.GetFakeBinaryUploadService()
                    ->last_request()
                    .blocking());
  };

  {
    base::RunLoop run_loop;
    DeepScanningRequest request(
        CreateMetadata(),
        DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
        DownloadCheckResult::SAFE,
        base::BindRepeating(
            [](base::RepeatingClosure closure, DownloadCheckResult result) {
              if (result != DownloadCheckResult::ASYNC_SCANNING) {
                closure.Run();
              }
            },
            run_loop.QuitClosure()),
        &download_protection_service_, dlp_and_malware_settings(),
        /*password=*/std::nullopt);

    request.Start();
    run_loop.Run();
    expect_dlp_and_malware_tags();
  }
}

TEST_P(DeepScanningRequestFeaturesEnabledTest, VerifyBlockingSet) {
  enterprise_connectors::test::SetAnalysisConnector(
      profile_->GetPrefs(), enterprise_connectors::FILE_DOWNLOADED,
      kScanForDlpAndMalware);

  auto no_block_settings = []() {
    enterprise_connectors::AnalysisSettings settings;
    settings.block_until_verdict =
        enterprise_connectors::BlockUntilVerdict::kNoBlock;
    return settings;
  };

  base::RunLoop run_loop;
  DeepScanningRequest request(
      CreateMetadata(),
      DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
      DownloadCheckResult::SAFE,
      base::BindRepeating(
          [](base::RepeatingClosure closure, DownloadCheckResult result) {
            if (result != DownloadCheckResult::ASYNC_SCANNING) {
              closure.Run();
            }
          },
          run_loop.QuitClosure()),
      &download_protection_service_, no_block_settings(),
      /*password=*/std::nullopt);

  request.Start();
  run_loop.Run();
  EXPECT_FALSE(download_protection_service_.GetFakeBinaryUploadService()
                   ->last_request()
                   .blocking());
}

INSTANTIATE_TEST_SUITE_P(
    FeaturesEnabled,
    DeepScanningRequestFeaturesEnabledTest,
    testing::Values(MetadataSourceType::kDownloadItem,
                    MetadataSourceType::kFileSystemAccessWriteItem));

class DeepScanningRequestAllFeaturesEnabledTest
    : public DeepScanningRequestSourceTypeTest {};

TEST_P(DeepScanningRequestAllFeaturesEnabledTest,
       GeneratesCorrectRequestFromPolicy) {
  {
    enterprise_connectors::test::SetAnalysisConnector(
        profile_->GetPrefs(), enterprise_connectors::FILE_DOWNLOADED,
        kScanForDlpAndMalware);
    // Override mime type to platform-independent value for testing.
    net::ScopedOverrideGetMimeTypeForTesting override_mime_type(
        "application/octet-stream");
    base::RunLoop run_loop;
    DeepScanningRequest request(
        CreateMetadata(),
        DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
        DownloadCheckResult::SAFE,
        base::BindRepeating(
            [](base::RepeatingClosure closure, DownloadCheckResult result) {
              if (result != DownloadCheckResult::ASYNC_SCANNING) {
                closure.Run();
              }
            },
            run_loop.QuitClosure()),
        &download_protection_service_, settings().value(),
        /*password=*/std::nullopt);

    request.Start();
    run_loop.Run();
    EXPECT_EQ(2, download_protection_service_.GetFakeBinaryUploadService()
                     ->last_request()
                     .tags_size());
    EXPECT_EQ("dlp", download_protection_service_.GetFakeBinaryUploadService()
                         ->last_request()
                         .tags(0));
    EXPECT_EQ("malware",
              download_protection_service_.GetFakeBinaryUploadService()
                  ->last_request()
                  .tags(1));
    EXPECT_EQ(download_protection_service_.GetFakeBinaryUploadService()
                  ->last_request()
                  .request_data()
                  .url(),
              download_url_.spec());
    EXPECT_EQ(download_protection_service_.GetFakeBinaryUploadService()
                  ->last_request()
                  .request_data()
                  .tab_url(),
              GURL("https://example.com"));
    EXPECT_EQ(download_protection_service_.GetFakeBinaryUploadService()
                  ->last_request()
                  .request_data()
                  .content_type(),
              "application/octet-stream");
    EXPECT_EQ(download_protection_service_.GetFakeBinaryUploadService()
                  ->last_request()
                  .reason(),
              enterprise_connectors::ContentAnalysisRequest::NORMAL_DOWNLOAD);
  }

  {
    base::RunLoop run_loop;
    enterprise_connectors::test::SetAnalysisConnector(
        profile_->GetPrefs(), enterprise_connectors::FILE_DOWNLOADED,
        kScanForMalware);
    DeepScanningRequest request(
        CreateMetadata(),
        DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
        DownloadCheckResult::SAFE,
        base::BindRepeating(
            [](base::RepeatingClosure closure, DownloadCheckResult result) {
              if (result != DownloadCheckResult::ASYNC_SCANNING) {
                closure.Run();
              }
            },
            run_loop.QuitClosure()),
        &download_protection_service_, settings().value(),
        /*password=*/std::nullopt);

    request.Start();
    run_loop.Run();
    EXPECT_EQ(1, download_protection_service_.GetFakeBinaryUploadService()
                     ->last_request()
                     .tags_size());
    EXPECT_EQ("malware",
              download_protection_service_.GetFakeBinaryUploadService()
                  ->last_request()
                  .tags(0));
    EXPECT_EQ(download_protection_service_.GetFakeBinaryUploadService()
                  ->last_request()
                  .reason(),
              enterprise_connectors::ContentAnalysisRequest::NORMAL_DOWNLOAD);
  }

  {
    base::RunLoop run_loop;
    enterprise_connectors::test::SetAnalysisConnector(
        profile_->GetPrefs(), enterprise_connectors::FILE_DOWNLOADED,
        kScanForDlp);
    DeepScanningRequest request(
        CreateMetadata(),
        DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
        DownloadCheckResult::SAFE,
        base::BindRepeating(
            [](base::RepeatingClosure closure, DownloadCheckResult result) {
              if (result != DownloadCheckResult::ASYNC_SCANNING) {
                closure.Run();
              }
            },
            run_loop.QuitClosure()),
        &download_protection_service_, settings().value(),
        /*password=*/std::nullopt);

    request.Start();
    run_loop.Run();
    EXPECT_EQ(1, download_protection_service_.GetFakeBinaryUploadService()
                     ->last_request()
                     .tags_size());
    EXPECT_EQ("dlp", download_protection_service_.GetFakeBinaryUploadService()
                         ->last_request()
                         .tags(0));
    EXPECT_EQ(download_protection_service_.GetFakeBinaryUploadService()
                  ->last_request()
                  .reason(),
              enterprise_connectors::ContentAnalysisRequest::NORMAL_DOWNLOAD);
  }

  {
    base::RunLoop run_loop;
    enterprise_connectors::test::SetAnalysisConnector(
        profile_->GetPrefs(), enterprise_connectors::FILE_DOWNLOADED, kNoScan);
    EXPECT_FALSE(settings().has_value());
    enterprise_connectors::AnalysisSettings analysis_settings;
    analysis_settings.block_until_verdict =
        enterprise_connectors::BlockUntilVerdict::kBlock;
    DeepScanningRequest request(
        CreateMetadata(),
        DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
        DownloadCheckResult::SAFE,
        base::BindRepeating(
            [](base::RepeatingClosure closure, DownloadCheckResult result) {
              if (result != DownloadCheckResult::ASYNC_SCANNING) {
                closure.Run();
              }
            },
            run_loop.QuitClosure()),
        &download_protection_service_, std::move(analysis_settings),
        /*password=*/std::nullopt);

    request.Start();
    run_loop.Run();
    EXPECT_TRUE(download_protection_service_.GetFakeBinaryUploadService()
                    ->last_request()
                    .tags()
                    .empty());
    EXPECT_EQ(download_protection_service_.GetFakeBinaryUploadService()
                  ->last_request()
                  .reason(),
              enterprise_connectors::ContentAnalysisRequest::NORMAL_DOWNLOAD);
  }
}

class DeepScanningAPPRequestTest : public DeepScanningRequestTest {};

TEST_F(DeepScanningAPPRequestTest, GeneratesCorrectRequestForConsumer) {
  enterprise_connectors::AnalysisSettings settings;
  settings.tags = {{"malware", enterprise_connectors::TagSettings()}};
  base::RunLoop run_loop;
  DeepScanningRequest request(
      CreateMetadata(),
      DownloadItemWarningData::DeepScanTrigger::TRIGGER_CONSUMER_PROMPT,
      DownloadCheckResult::SAFE,
      base::BindRepeating(
          [](base::RepeatingClosure closure, DownloadCheckResult result) {
            if (result != DownloadCheckResult::ASYNC_SCANNING) {
              closure.Run();
            }
          },
          run_loop.QuitClosure()),
      &download_protection_service_, std::move(settings),
      /*password=*/std::nullopt);

  request.Start();
  run_loop.Run();

  EXPECT_EQ(1, download_protection_service_.GetFakeBinaryUploadService()
                   ->last_request()
                   .tags()
                   .size());
  EXPECT_EQ("malware",
            download_protection_service_.GetFakeBinaryUploadService()
                ->last_request()
                .tags()[0]);
  EXPECT_FALSE(download_protection_service_.GetFakeBinaryUploadService()
                   ->last_request()
                   .has_device_token());
}

class DeepScanningReportingTest : public DeepScanningRequestTest {
 public:
  void SetUp() override {
    DeepScanningRequestTest::SetUp();

    client_ = std::make_unique<policy::MockCloudPolicyClient>();

    extensions::SafeBrowsingPrivateEventRouterFactory::GetInstance()
        ->SetTestingFactory(
            profile_,
            base::BindRepeating(&BuildSafeBrowsingPrivateEventRouter));
    enterprise_connectors::RealtimeReportingClientFactory::GetInstance()
        ->SetTestingFactory(profile_,
                            base::BindRepeating(&BuildRealtimeReportingClient));

    enterprise_connectors::RealtimeReportingClientFactory::GetForProfile(
        profile_)
        ->SetBrowserCloudPolicyClientForTesting(client_.get());
    identity_test_environment_.MakePrimaryAccountAvailable(
        kUserName, signin::ConsentLevel::kSync);
    enterprise_connectors::RealtimeReportingClientFactory::GetForProfile(
        profile_)
        ->SetIdentityManagerForTesting(
            identity_test_environment_.identity_manager());

    enterprise_connectors::test::SetOnSecurityEventReporting(
        profile_->GetPrefs(), true);

#if BUILDFLAG(IS_CHROMEOS)
    fake_statistics_provider_.SetMachineStatistic(ash::system::kSerialNumberKey,
                                                  "fake_serial_number");
#endif

    enterprise_connectors::test::SetAnalysisConnector(
        profile_->GetPrefs(), enterprise_connectors::FILE_DOWNLOADED,
        kScanForDlpAndMalware);
  }

  void TearDown() override {
    enterprise_connectors::RealtimeReportingClientFactory::GetForProfile(
        profile_)
        ->SetBrowserCloudPolicyClientForTesting(nullptr);
    DeepScanningRequestTest::TearDown();
  }

 protected:
  std::unique_ptr<policy::MockCloudPolicyClient> client_;
  signin::IdentityTestEnvironment identity_test_environment_;
#if BUILDFLAG(IS_CHROMEOS)
  ash::system::ScopedFakeStatisticsProvider fake_statistics_provider_;
#endif
};

class DeepScanningReportingSourceTypeTest
    : public DeepScanningReportingTest,
      public testing::WithParamInterface<MetadataSourceType> {
 public:
  void SetUp() override {
    DeepScanningReportingTest::SetUp();
    SetMetadataSourceType(GetParam());
  }

  MetadataSourceType GetMetadataSourceType() { return GetParam(); }
};

TEST_P(DeepScanningReportingSourceTypeTest, ProcessesResponseCorrectly) {
  {
    base::RunLoop run_loop;
    DeepScanningRequest request(
        CreateMetadata(),
        DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
        DownloadCheckResult::SAFE,
        base::BindRepeating(
            [](DeepScanningRequestTest* test,
               base::RepeatingClosure quit_closure,
               DownloadCheckResult result) {
              test->SetLastResult(result);
              if (result != DownloadCheckResult::ASYNC_SCANNING) {
                quit_closure.Run();
              }
            },
            base::Unretained(this), run_loop.QuitClosure()),
        &download_protection_service_, settings().value(),
        /*password=*/std::nullopt);

    enterprise_connectors::ContentAnalysisResponse response;
    response.set_request_token(kScanId);

    auto* malware_result = response.add_results();
    malware_result->set_tag("malware");
    malware_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);
    auto* malware_rule = malware_result->add_triggered_rules();
    malware_rule->set_action(enterprise_connectors::TriggeredRule::BLOCK);
    malware_rule->set_rule_name("malware");

    auto* dlp_result = response.add_results();
    dlp_result->set_tag("dlp");
    dlp_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);
    auto* dlp_rule = dlp_result->add_triggered_rules();
    dlp_rule->set_action(enterprise_connectors::TriggeredRule::BLOCK);
    dlp_rule->set_rule_name("dlp_rule");
    dlp_rule->set_rule_id("0");

    download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
        download_path_, BinaryUploadService::Result::SUCCESS, response);
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetExpectedFinalAction(
            enterprise_connectors::ContentAnalysisAcknowledgement::WARN);

    enterprise_connectors::test::EventReportValidator validator(client_.get());
    validator.ExpectDangerousDeepScanningResultAndSensitiveDataEvent(
        /*url*/ "https://example.com/download.exe",
        /*tab_url*/ "https://example.com/",
        /*source*/ "",
        /*destination*/ "",
        /*filename*/ download_path_.AsUTF8Unsafe(),
        // printf "download contents" | sha256sum |  tr '[:lower:]'
        // '[:upper:]'
        /*sha256*/
        "76E00EB33811F5778A5EE557512C30D9341D4FEB07646BCE3E4DB13F9428573C",
        /*threat_type*/ "DANGEROUS",
        /*trigger*/
        extensions::SafeBrowsingPrivateEventRouter::kTriggerFileDownload,
        /*dlp_verdict*/ *dlp_result,
        /*mimetypes*/ ExeMimeTypes(),
        /*size*/ std::string("download contents").size(),
        /*result*/
        enterprise_connectors::EventResultToString(
            enterprise_connectors::EventResult::WARNED),
        /*username*/ kUserName,
        /*profile_identifier*/ profile_->GetPath().AsUTF8Unsafe(),
        /*scan_id*/ kScanId,
        /*content_transfer_method*/ std::nullopt);

    request.Start();

    run_loop.Run();

    EXPECT_EQ(DownloadCheckResult::DANGEROUS, last_result_);
  }

  {
    base::RunLoop run_loop;
    DeepScanningRequest request(
        CreateMetadata(),
        DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
        DownloadCheckResult::SAFE,
        base::BindRepeating(
            [](DeepScanningRequestTest* test,
               base::RepeatingClosure quit_closure,
               DownloadCheckResult result) {
              test->SetLastResult(result);
              if (result != DownloadCheckResult::ASYNC_SCANNING) {
                quit_closure.Run();
              }
            },
            base::Unretained(this), run_loop.QuitClosure()),
        &download_protection_service_, settings().value(),
        /*password=*/std::nullopt);

    enterprise_connectors::ContentAnalysisResponse response;
    response.set_request_token(kScanId);

    auto* malware_result = response.add_results();
    malware_result->set_tag("malware");
    malware_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);
    auto* malware_rule = malware_result->add_triggered_rules();
    malware_rule->set_action(enterprise_connectors::TriggeredRule::WARN);
    malware_rule->set_rule_name("uws");

    auto* dlp_result = response.add_results();
    dlp_result->set_tag("dlp");
    dlp_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);
    auto* dlp_rule = dlp_result->add_triggered_rules();
    dlp_rule->set_action(enterprise_connectors::TriggeredRule::WARN);
    dlp_rule->set_rule_name("dlp_rule");
    dlp_rule->set_rule_id("0");

    download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
        download_path_, BinaryUploadService::Result::SUCCESS, response);
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetExpectedFinalAction(
            enterprise_connectors::ContentAnalysisAcknowledgement::WARN);

    enterprise_connectors::test::EventReportValidator validator(client_.get());
    validator.ExpectDangerousDeepScanningResultAndSensitiveDataEvent(
        /*url*/ "https://example.com/download.exe",
        /*tab_url*/ "https://example.com/",
        /*source*/ "",
        /*destination*/ "",
        /*filename*/ download_path_.AsUTF8Unsafe(),
        // printf "download contents" | sha256sum |  tr '[:lower:]'
        // '[:upper:]'
        /*sha256*/
        "76E00EB33811F5778A5EE557512C30D9341D4FEB07646BCE3E4DB13F9428573C",
        /*threat_type*/ "POTENTIALLY_UNWANTED",
        /*trigger*/
        extensions::SafeBrowsingPrivateEventRouter::kTriggerFileDownload,
        /*dlp_verdict*/ *dlp_result,
        /*mimetypes*/ ExeMimeTypes(),
        /*size*/ std::string("download contents").size(),
        /*result*/
        enterprise_connectors::EventResultToString(
            enterprise_connectors::EventResult::WARNED),
        /*username*/ kUserName,
        /*profile_identifier*/ profile_->GetPath().AsUTF8Unsafe(),
        /*scan_id*/ kScanId,
        /*content_transfer_method*/ std::nullopt);

    request.Start();

    run_loop.Run();

    EXPECT_EQ(DownloadCheckResult::POTENTIALLY_UNWANTED, last_result_);
  }

  {
    base::RunLoop run_loop;
    DeepScanningRequest request(
        CreateMetadata(),
        DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
        DownloadCheckResult::SAFE,
        base::BindRepeating(
            [](DeepScanningRequestTest* test,
               base::RepeatingClosure quit_closure,
               DownloadCheckResult result) {
              test->SetLastResult(result);
              if (result != DownloadCheckResult::ASYNC_SCANNING) {
                quit_closure.Run();
              }
            },
            base::Unretained(this), run_loop.QuitClosure()),
        &download_protection_service_, settings().value(),
        /*password=*/std::nullopt);

    enterprise_connectors::ContentAnalysisResponse response;
    response.set_request_token(kScanId);

    auto* dlp_result = response.add_results();
    dlp_result->set_tag("dlp");
    dlp_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);
    auto* dlp_rule = dlp_result->add_triggered_rules();
    dlp_rule->set_action(enterprise_connectors::TriggeredRule::BLOCK);
    dlp_rule->set_rule_name("dlp_rule");
    dlp_rule->set_rule_id("0");

    download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
        download_path_, BinaryUploadService::Result::SUCCESS, response);
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetExpectedFinalAction(
            enterprise_connectors::ContentAnalysisAcknowledgement::BLOCK);

    enterprise_connectors::test::EventReportValidator validator(client_.get());
    validator.ExpectSensitiveDataEvent(
        /*url*/ "https://example.com/download.exe",
        /*tab_url*/ "https://example.com/",
        /*source*/ "",
        /*destination*/ "",
        /*filename*/ download_path_.AsUTF8Unsafe(),
        // printf "download contents" | sha256sum |  tr '[:lower:]' '[:upper:]'
        /*sha256*/
        "76E00EB33811F5778A5EE557512C30D9341D4FEB07646BCE3E4DB13F9428573C",
        /*trigger*/
        extensions::SafeBrowsingPrivateEventRouter::kTriggerFileDownload,
        /*dlp_verdict*/ *dlp_result,
        /*mimetypes*/ ExeMimeTypes(),
        /*size*/ std::string("download contents").size(),
        enterprise_connectors::EventResultToString(
            enterprise_connectors::EventResult::BLOCKED),
        /*username*/ kUserName,
        /*profile_identifier*/ profile_->GetPath().AsUTF8Unsafe(),
        /*scan_id*/ kScanId,
        /*content_transfer_method*/ std::nullopt,
        /*user_justification*/ std::nullopt);

    request.Start();

    run_loop.Run();

    EXPECT_EQ(DownloadCheckResult::SENSITIVE_CONTENT_BLOCK, last_result_);
  }

  {
    base::RunLoop run_loop;
    DeepScanningRequest request(
        CreateMetadata(),
        DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
        DownloadCheckResult::SAFE,
        base::BindRepeating(
            [](DeepScanningRequestTest* test,
               base::RepeatingClosure quit_closure,
               DownloadCheckResult result) {
              test->SetLastResult(result);
              if (result != DownloadCheckResult::ASYNC_SCANNING) {
                quit_closure.Run();
              }
            },
            base::Unretained(this), run_loop.QuitClosure()),
        &download_protection_service_, settings().value(),
        /*password=*/std::nullopt);

    enterprise_connectors::ContentAnalysisResponse response;
    response.set_request_token(kScanId);

    auto* dlp_result = response.add_results();
    dlp_result->set_tag("dlp");
    dlp_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);
    auto* dlp_rule = dlp_result->add_triggered_rules();
    dlp_rule->set_action(enterprise_connectors::TriggeredRule::WARN);
    dlp_rule->set_rule_name("dlp_rule");
    dlp_rule->set_rule_id("0");

    download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
        download_path_, BinaryUploadService::Result::SUCCESS, response);
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetExpectedFinalAction(
            enterprise_connectors::ContentAnalysisAcknowledgement::WARN);

    enterprise_connectors::test::EventReportValidator validator(client_.get());
    validator.ExpectSensitiveDataEvent(
        /*url*/ "https://example.com/download.exe",
        /*tab_url*/ "https://example.com/",
        /*source*/ "",
        /*destination*/ "",
        /*filename*/ download_path_.AsUTF8Unsafe(),
        // printf "download contents" | sha256sum |  tr '[:lower:]' '[:upper:]'
        /*sha256*/
        "76E00EB33811F5778A5EE557512C30D9341D4FEB07646BCE3E4DB13F9428573C",
        /*trigger*/
        extensions::SafeBrowsingPrivateEventRouter::kTriggerFileDownload,
        /*dlp_verdict*/ *dlp_result,
        /*mimetypes*/ ExeMimeTypes(),
        /*size*/ std::string("download contents").size(),
        enterprise_connectors::EventResultToString(
            enterprise_connectors::EventResult::WARNED),
        /*username*/ kUserName,
        /*profile_identifier*/ profile_->GetPath().AsUTF8Unsafe(),
        /*scan_id*/ kScanId,
        /*content_transfer_method*/ std::nullopt,
        /*user_justification*/ std::nullopt);

    request.Start();

    run_loop.Run();

    EXPECT_EQ(DownloadCheckResult::SENSITIVE_CONTENT_WARNING, last_result_);
  }

  {
    base::RunLoop run_loop;
    DeepScanningRequest request(
        CreateMetadata(),
        DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
        DownloadCheckResult::SAFE,
        base::BindRepeating(
            [](DeepScanningRequestTest* test,
               base::RepeatingClosure quit_closure,
               DownloadCheckResult result) {
              test->SetLastResult(result);
              if (result != DownloadCheckResult::ASYNC_SCANNING) {
                quit_closure.Run();
              }
            },
            base::Unretained(this), run_loop.QuitClosure()),
        &download_protection_service_, settings().value(),
        /*password=*/std::nullopt);

    enterprise_connectors::ContentAnalysisResponse response;
    response.set_request_token(kScanId);

    auto* dlp_result = response.add_results();
    dlp_result->set_tag("dlp");
    dlp_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);
    auto* dlp_rule1 = dlp_result->add_triggered_rules();
    dlp_rule1->set_action(enterprise_connectors::TriggeredRule::WARN);
    dlp_rule1->set_rule_name("dlp_rule1");
    dlp_rule1->set_rule_id("0");
    auto* dlp_rule2 = dlp_result->add_triggered_rules();
    dlp_rule2->set_action(enterprise_connectors::TriggeredRule::BLOCK);
    dlp_rule2->set_rule_name("dlp_rule2");
    dlp_rule2->set_rule_id("0");

    download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
        download_path_, BinaryUploadService::Result::SUCCESS, response);
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetExpectedFinalAction(
            enterprise_connectors::ContentAnalysisAcknowledgement::BLOCK);

    enterprise_connectors::test::EventReportValidator validator(client_.get());
    validator.ExpectSensitiveDataEvent(
        /*url*/ "https://example.com/download.exe",
        /*tab_url*/ "https://example.com/",
        /*source*/ "",
        /*destination*/ "",
        /*filename*/ download_path_.AsUTF8Unsafe(),
        // printf "download contents" | sha256sum |  tr '[:lower:]' '[:upper:]'
        /*sha256*/
        "76E00EB33811F5778A5EE557512C30D9341D4FEB07646BCE3E4DB13F9428573C",
        /*trigger*/
        extensions::SafeBrowsingPrivateEventRouter::kTriggerFileDownload,
        /*dlp_verdict*/ *dlp_result,
        /*mimetypes*/ ExeMimeTypes(),
        /*size*/ std::string("download contents").size(),
        enterprise_connectors::EventResultToString(
            enterprise_connectors::EventResult::BLOCKED),
        /*username*/ kUserName,
        /*profile_identifier*/ profile_->GetPath().AsUTF8Unsafe(),
        /*scan_id*/ kScanId,
        /*content_transfer_method*/ std::nullopt,
        /*user_justification*/ std::nullopt);

    request.Start();

    run_loop.Run();

    EXPECT_EQ(DownloadCheckResult::SENSITIVE_CONTENT_BLOCK, last_result_);
  }

  {
    base::RunLoop run_loop;
    DeepScanningRequest request(
        CreateMetadata(),
        DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
        DownloadCheckResult::SAFE,
        base::BindRepeating(
            [](DeepScanningRequestTest* test,
               base::RepeatingClosure quit_closure,
               DownloadCheckResult result) {
              test->SetLastResult(result);
              if (result != DownloadCheckResult::ASYNC_SCANNING) {
                quit_closure.Run();
              }
            },
            base::Unretained(this), run_loop.QuitClosure()),
        &download_protection_service_, settings().value(),
        /*password=*/std::nullopt);

    enterprise_connectors::ContentAnalysisResponse response;

    auto* malware_result = response.add_results();
    malware_result->set_tag("dlp");
    malware_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::FAILURE);

    download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
        download_path_, BinaryUploadService::Result::SUCCESS, response);
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetExpectedFinalAction(
            enterprise_connectors::ContentAnalysisAcknowledgement::ALLOW);

    enterprise_connectors::test::EventReportValidator validator(client_.get());
    validator.ExpectUnscannedFileEvent(
        /*url*/ "https://example.com/download.exe",
        /*tab_url*/ "https://example.com/",
        /*source*/ "",
        /*destination*/ "",
        /*filename*/ download_path_.AsUTF8Unsafe(),
        // printf "download contents" | sha256sum |  tr '[:lower:]' '[:upper:]'
        /*sha256*/
        "76E00EB33811F5778A5EE557512C30D9341D4FEB07646BCE3E4DB13F9428573C",
        /*trigger*/
        extensions::SafeBrowsingPrivateEventRouter::kTriggerFileDownload,
        /*reason*/ "DLP_SCAN_FAILED",
        /*mimetypes*/ ExeMimeTypes(),
        /*size*/ std::string("download contents").size(),
        /*result*/
        enterprise_connectors::EventResultToString(
            enterprise_connectors::EventResult::ALLOWED),
        /*username*/ kUserName,
        /*profile_identifier*/ profile_->GetPath().AsUTF8Unsafe(),
        /*content_transfer_method*/ std::nullopt);

    request.Start();

    run_loop.Run();

    EXPECT_EQ(DownloadCheckResult::SAFE, last_result_);
  }

  {
    base::RunLoop run_loop;
    DeepScanningRequest request(
        CreateMetadata(),
        DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
        DownloadCheckResult::SAFE,
        base::BindRepeating(
            [](DeepScanningRequestTest* test,
               base::RepeatingClosure quit_closure,
               DownloadCheckResult result) {
              test->SetLastResult(result);
              if (result != DownloadCheckResult::ASYNC_SCANNING) {
                quit_closure.Run();
              }
            },
            base::Unretained(this), run_loop.QuitClosure()),
        &download_protection_service_, settings().value(),
        /*password=*/std::nullopt);

    enterprise_connectors::ContentAnalysisResponse response;

    auto* malware_result = response.add_results();
    malware_result->set_tag("malware");
    malware_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::FAILURE);

    download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
        download_path_, BinaryUploadService::Result::SUCCESS, response);
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetExpectedFinalAction(
            enterprise_connectors::ContentAnalysisAcknowledgement::ALLOW);

    enterprise_connectors::test::EventReportValidator validator(client_.get());
    validator.ExpectUnscannedFileEvent(
        /*url*/ "https://example.com/download.exe",
        /*tab_url*/ "https://example.com/",
        /*source*/ "",
        /*destination*/ "",
        /*filename*/ download_path_.AsUTF8Unsafe(),
        // printf "download contents" | sha256sum |  tr '[:lower:]' '[:upper:]'
        /*sha256*/
        "76E00EB33811F5778A5EE557512C30D9341D4FEB07646BCE3E4DB13F9428573C",
        /*trigger*/
        extensions::SafeBrowsingPrivateEventRouter::kTriggerFileDownload,
        /*reason*/ "MALWARE_SCAN_FAILED",
        /*mimetypes*/ ExeMimeTypes(),
        /*size*/ std::string("download contents").size(),
        /*result*/
        enterprise_connectors::EventResultToString(
            enterprise_connectors::EventResult::ALLOWED),
        /*username*/ kUserName,
        /*profile_identifier*/ profile_->GetPath().AsUTF8Unsafe(),
        /*content_transfer_method*/ std::nullopt);

    request.Start();

    run_loop.Run();

    EXPECT_EQ(DownloadCheckResult::SAFE, last_result_);
  }

  {
    base::RunLoop run_loop;
    // The DownloadCheckResult passed below should be used if scanning fails.
    DeepScanningRequest request(
        CreateMetadata(),
        DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
        DownloadCheckResult::DANGEROUS,
        base::BindRepeating(
            [](DeepScanningRequestTest* test,
               base::RepeatingClosure quit_closure,
               DownloadCheckResult result) {
              test->SetLastResult(result);
              if (result != DownloadCheckResult::ASYNC_SCANNING) {
                quit_closure.Run();
              }
            },
            base::Unretained(this), run_loop.QuitClosure()),
        &download_protection_service_, settings().value(),
        /*password=*/std::nullopt);

    EXPECT_CALL(item_, GetDangerType())
        .WillRepeatedly(Return(download::DownloadDangerType::
                                   DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT));

    enterprise_connectors::ContentAnalysisResponse response;

    auto* malware_result = response.add_results();
    malware_result->set_tag("malware");
    malware_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::FAILURE);

    download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
        download_path_, BinaryUploadService::Result::SUCCESS, response);

    download_protection_service_.GetFakeBinaryUploadService()
        ->SetExpectedFinalAction(
            GetMetadataSourceType() == MetadataSourceType::kDownloadItem
                ? enterprise_connectors::ContentAnalysisAcknowledgement::WARN
                : enterprise_connectors::ContentAnalysisAcknowledgement::
                      ALLOW);  // for fsa item scans, the pre-scan result is
                               // always ALLOW

    enterprise_connectors::test::EventReportValidator validator(client_.get());
    validator.ExpectUnscannedFileEvent(
        /*url*/ "https://example.com/download.exe",
        /*tab_url*/ "https://example.com/",
        /*source*/ "",
        /*destination*/ "",
        /*filename*/ download_path_.AsUTF8Unsafe(),
        // printf "download contents" | sha256sum |  tr '[:lower:]' '[:upper:]'
        /*sha256*/
        "76E00EB33811F5778A5EE557512C30D9341D4FEB07646BCE3E4DB13F9428573C",
        /*trigger*/
        extensions::SafeBrowsingPrivateEventRouter::kTriggerFileDownload,
        /*reason*/ "MALWARE_SCAN_FAILED",
        /*mimetypes*/ ExeMimeTypes(),
        /*size*/ std::string("download contents").size(),
        /*result*/
        enterprise_connectors::EventResultToString(
            GetMetadataSourceType() == MetadataSourceType::kDownloadItem
                ? enterprise_connectors::EventResult::WARNED
                : enterprise_connectors::EventResult::
                      ALLOWED),  // for fsa item scans, the pre-scan result is
                                 // always ALLOW
        /*username*/ kUserName,
        /*profile_identifier*/ profile_->GetPath().AsUTF8Unsafe(),
        /*content_transfer_reason*/ std::nullopt);

    request.Start();

    run_loop.Run();

    EXPECT_EQ(DownloadCheckResult::DANGEROUS, last_result_);
  }
}

TEST_F(DeepScanningReportingTest, ConsumerEncryptedArchiveSuccess) {
  base::RunLoop run_loop;
  DeepScanningRequest request(
      CreateMetadata(),
      DownloadItemWarningData::DeepScanTrigger::TRIGGER_CONSUMER_PROMPT,
      DownloadCheckResult::SAFE,
      base::BindRepeating(
          [](DeepScanningRequestTest* test, base::RepeatingClosure quit_closure,
             DownloadCheckResult result) {
            test->SetLastResult(result);
            if (result != DownloadCheckResult::ASYNC_SCANNING) {
              quit_closure.Run();
            }
          },
          base::Unretained(this), run_loop.QuitClosure()),
      &download_protection_service_, settings().value(),
      /*password=*/std::nullopt);

  enterprise_connectors::ContentAnalysisResponse response;
  response.set_request_token(kScanId);

  auto* malware_result = response.add_results();
  malware_result->set_tag("malware");
  malware_result->set_status(
      enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);

  download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
      download_path_, BinaryUploadService::Result::SUCCESS, response);
  download_protection_service_.GetFakeBinaryUploadService()
      ->SetExpectedFinalAction(
          enterprise_connectors::ContentAnalysisAcknowledgement::ALLOW);

  DownloadItemWarningData::SetIsTopLevelEncryptedArchive(&item_, true);
  EXPECT_FALSE(DownloadItemWarningData::HasIncorrectPassword(&item_));

  request.Start();

  run_loop.Run();

  EXPECT_FALSE(DownloadItemWarningData::HasIncorrectPassword(&item_));
}

TEST_F(DeepScanningReportingTest, ConsumerEncryptedArchiveFailed) {
  base::RunLoop run_loop;
  DeepScanningRequest request(
      CreateMetadata(),
      DownloadItemWarningData::DeepScanTrigger::TRIGGER_CONSUMER_PROMPT,
      DownloadCheckResult::SAFE,
      base::BindRepeating(
          [](DeepScanningRequestTest* test, base::RepeatingClosure quit_closure,
             DownloadCheckResult result) {
            test->SetLastResult(result);
            if (result != DownloadCheckResult::ASYNC_SCANNING) {
              quit_closure.Run();
            }
          },
          base::Unretained(this), run_loop.QuitClosure()),
      &download_protection_service_, settings().value(),
      /*password=*/std::nullopt);

  enterprise_connectors::ContentAnalysisResponse response;
  response.set_request_token(kScanId);

  auto* malware_result = response.add_results();
  malware_result->set_tag("malware");
  malware_result->set_status(
      enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);
  malware_result->set_status_error_message(
      enterprise_connectors::ContentAnalysisResponse::Result::
          DECRYPTION_FAILED);

  download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
      download_path_, BinaryUploadService::Result::SUCCESS, response);
  download_protection_service_.GetFakeBinaryUploadService()
      ->SetExpectedFinalAction(
          enterprise_connectors::ContentAnalysisAcknowledgement::ALLOW);

  DownloadItemWarningData::SetIsTopLevelEncryptedArchive(&item_, true);
  EXPECT_FALSE(DownloadItemWarningData::HasIncorrectPassword(&item_));

  request.Start();

  run_loop.Run();

  EXPECT_TRUE(DownloadItemWarningData::HasIncorrectPassword(&item_));
}

TEST_F(DeepScanningReportingTest, ConsumerUnencryptedArchive) {
  base::RunLoop run_loop;
  DeepScanningRequest request(
      CreateMetadata(),
      DownloadItemWarningData::DeepScanTrigger::TRIGGER_CONSUMER_PROMPT,
      DownloadCheckResult::SAFE,
      base::BindRepeating(
          [](DeepScanningRequestTest* test, base::RepeatingClosure quit_closure,
             DownloadCheckResult result) {
            test->SetLastResult(result);
            if (result != DownloadCheckResult::ASYNC_SCANNING) {
              quit_closure.Run();
            }
          },
          base::Unretained(this), run_loop.QuitClosure()),
      &download_protection_service_, settings().value(),
      /*password=*/std::nullopt);

  enterprise_connectors::ContentAnalysisResponse response;
  response.set_request_token(kScanId);

  auto* malware_result = response.add_results();
  malware_result->set_tag("malware");
  malware_result->set_status(
      enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);

  download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
      download_path_, BinaryUploadService::Result::SUCCESS, response);
  download_protection_service_.GetFakeBinaryUploadService()
      ->SetExpectedFinalAction(
          enterprise_connectors::ContentAnalysisAcknowledgement::ALLOW);

  DownloadItemWarningData::SetIsTopLevelEncryptedArchive(&item_, false);
  EXPECT_FALSE(DownloadItemWarningData::HasIncorrectPassword(&item_));

  request.Start();

  run_loop.Run();

  EXPECT_FALSE(DownloadItemWarningData::HasIncorrectPassword(&item_));
}

TEST_P(DeepScanningReportingSourceTypeTest, MultipleFiles) {
  {
    enterprise_connectors::ContentAnalysisResponse response;
    response.set_request_token(kScanId);

    auto* malware_result = response.add_results();
    malware_result->set_tag("malware");
    malware_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);

    auto* dlp_result = response.add_results();
    dlp_result->set_tag("dlp");
    dlp_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);
    base::flat_map<base::FilePath, base::FilePath> current_paths_to_final_paths;

    auto metadata = CreateMetadata();
    current_paths_to_final_paths[metadata->GetFullPath()] =
        metadata->GetTargetFilePath();
    download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
        metadata->GetTargetFilePath(), BinaryUploadService::Result::SUCCESS,
        response);
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetExpectedFinalAction(
            enterprise_connectors::ContentAnalysisAcknowledgement::ALLOW);
    for (size_t i = 0; i < secondary_files_.size(); ++i) {
      current_paths_to_final_paths[secondary_files_[i]] =
          secondary_files_targets_[i];

      enterprise_connectors::ContentAnalysisResponse response_copy = response;
      response.set_request_token(
          base::StrCat({kScanId, base::NumberToString(i)}));

      download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
          secondary_files_targets_[i], BinaryUploadService::Result::SUCCESS,
          response_copy);
    }

    DeepScanningRequest request(
        std::move(metadata), DownloadCheckResult::SAFE,
        base::BindRepeating(&DeepScanningRequestTest::SetLastResult,
                            base::Unretained(this)),
        &download_protection_service_, settings().value(),
        current_paths_to_final_paths);

    base::RunLoop run_loop;
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetQuitOnLastRequest(run_loop.QuitClosure());
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetExpectedFinalAction(
            enterprise_connectors::ContentAnalysisAcknowledgement::ALLOW);

    enterprise_connectors::test::EventReportValidator validator(client_.get());
    validator.ExpectNoReport();

    request.Start();
    run_loop.Run();

    EXPECT_EQ(DownloadCheckResult::DEEP_SCANNED_SAFE, last_result_);
    EXPECT_EQ(4u, download_protection_service_.GetFakeBinaryUploadService()
                      ->num_finished_requests());
    EXPECT_EQ(
        4u,
        download_protection_service_.GetFakeBinaryUploadService()->num_acks());
    download_protection_service_.GetFakeBinaryUploadService()->Reset();
  }

  {
    enterprise_connectors::ContentAnalysisResponse response;
    response.set_request_token(kScanId);

    auto* malware_result = response.add_results();
    malware_result->set_tag("malware");
    malware_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);

    auto* dlp_result = response.add_results();
    dlp_result->set_tag("dlp");
    dlp_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);
    base::flat_map<base::FilePath, base::FilePath> current_paths_to_final_paths;

    auto metadata = CreateMetadata();
    current_paths_to_final_paths[metadata->GetFullPath()] =
        metadata->GetTargetFilePath();
    download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
        metadata->GetTargetFilePath(), BinaryUploadService::Result::SUCCESS,
        response);
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetExpectedFinalAction(
            enterprise_connectors::ContentAnalysisAcknowledgement::ALLOW);
    for (size_t i = 0; i < secondary_files_.size(); ++i) {
      current_paths_to_final_paths[secondary_files_[i]] =
          secondary_files_targets_[i];

      enterprise_connectors::ContentAnalysisResponse response_copy = response;
      response_copy.set_request_token(
          base::StrCat({kScanId, base::NumberToString(i)}));

      if (i == 0) {
        response_copy.mutable_results(0)->set_status(
            enterprise_connectors::ContentAnalysisResponse::Result::FAILURE);
      }

      download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
          secondary_files_targets_[i], BinaryUploadService::Result::SUCCESS,
          response_copy);
    }

    DeepScanningRequest request(
        std::move(metadata), DownloadCheckResult::SAFE,
        base::BindRepeating(&DeepScanningRequestTest::SetLastResult,
                            base::Unretained(this)),
        &download_protection_service_, settings().value(),
        current_paths_to_final_paths);

    base::RunLoop run_loop;
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetQuitOnLastRequest(run_loop.QuitClosure());
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetExpectedFinalAction(
            enterprise_connectors::ContentAnalysisAcknowledgement::ALLOW);

    enterprise_connectors::test::EventReportValidator validator(client_.get());
    validator.ExpectUnscannedFileEvent(
        /*url*/ "https://example.com/download.exe",
        /*tab_url*/ "https://example.com/",
        /*source*/ "",
        /*destination*/ "",
        /*filename*/ secondary_files_targets_[0].AsUTF8Unsafe(),
        // printf "foo.txt" | sha256sum |  tr '[:lower:]' '[:upper:]'
        /*sha256*/
        "DDAB29FF2C393EE52855D21A240EB05F775DF88E3CE347DF759F0C4B80356C35",
        /*trigger*/
        extensions::SafeBrowsingPrivateEventRouter::kTriggerFileDownload,
        /*reason*/ "MALWARE_SCAN_FAILED",
        /*mimetypes*/ TxtMimeTypes(),
        /*size*/ std::string("foo.exe").size(),
        /*result*/
        enterprise_connectors::EventResultToString(
            enterprise_connectors::EventResult::ALLOWED),
        /*username*/ kUserName,
        /*profile_identifier*/ profile_->GetPath().AsUTF8Unsafe(),
        /*content_transfer_reason*/ std::nullopt);

    request.Start();
    run_loop.Run();

    EXPECT_EQ(DownloadCheckResult::SAFE, last_result_);
    EXPECT_EQ(4u, download_protection_service_.GetFakeBinaryUploadService()
                      ->num_finished_requests());
    EXPECT_EQ(
        4u,
        download_protection_service_.GetFakeBinaryUploadService()->num_acks());
    download_protection_service_.GetFakeBinaryUploadService()->Reset();
  }

  {
    enterprise_connectors::ContentAnalysisResponse response;
    response.set_request_token(kScanId);

    auto* malware_result = response.add_results();
    malware_result->set_tag("malware");
    malware_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);

    auto* dlp_result = response.add_results();
    dlp_result->set_tag("dlp");
    dlp_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);
    base::flat_map<base::FilePath, base::FilePath> current_paths_to_final_paths;

    auto metadata = CreateMetadata();
    current_paths_to_final_paths[metadata->GetFullPath()] =
        metadata->GetTargetFilePath();
    download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
        metadata->GetTargetFilePath(), BinaryUploadService::Result::SUCCESS,
        response);
    std::vector<enterprise_connectors::ContentAnalysisResponse::Result>
        expected_dlp_verdicts;
    for (size_t i = 0; i < secondary_files_.size(); ++i) {
      current_paths_to_final_paths[secondary_files_[i]] =
          secondary_files_targets_[i];

      enterprise_connectors::ContentAnalysisResponse response_copy = response;
      response_copy.set_request_token(
          base::StrCat({kScanId, base::NumberToString(i)}));

      if (i == 0) {
        auto* dlp_rule =
            response_copy.mutable_results(1)->add_triggered_rules();
        dlp_rule->set_action(enterprise_connectors::TriggeredRule::WARN);
        dlp_rule->set_rule_name("warn_dlp_rule");
        dlp_rule->set_rule_id("0");
        expected_dlp_verdicts.push_back(response_copy.results(1));
      } else if (i == 1) {
        auto* dlp_rule =
            response_copy.mutable_results(1)->add_triggered_rules();
        dlp_rule->set_action(enterprise_connectors::TriggeredRule::BLOCK);
        dlp_rule->set_rule_name("block_dlp_rule");
        dlp_rule->set_rule_id("1");
        expected_dlp_verdicts.push_back(response_copy.results(1));
      }

      download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
          secondary_files_targets_[i], BinaryUploadService::Result::SUCCESS,
          response_copy);
    }

    DeepScanningRequest request(
        std::move(metadata), DownloadCheckResult::SAFE,
        base::BindRepeating(&DeepScanningRequestTest::SetLastResult,
                            base::Unretained(this)),
        &download_protection_service_, settings().value(),
        current_paths_to_final_paths);

    base::RunLoop run_loop;
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetQuitOnLastRequest(run_loop.QuitClosure());
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetExpectedFinalAction(
            enterprise_connectors::ContentAnalysisAcknowledgement::BLOCK);

    enterprise_connectors::test::EventReportValidator validator(client_.get());
    validator.ExpectSensitiveDataEvents(
        /*url*/ "https://example.com/download.exe",
        /*tab_url*/ "https://example.com/",
        /*source*/ "",
        /*destination*/ "",
        {
            secondary_files_targets_[0].AsUTF8Unsafe(),
            secondary_files_targets_[1].AsUTF8Unsafe(),
        },
        // printf "foo.txt" | sha256sum |  tr '[:lower:]' '[:upper:]'
        // printf "bar.txt" | sha256sum |  tr '[:lower:]' '[:upper:]'
        {
            "DDAB29FF2C393EE52855D21A240EB05F775DF88E3CE347DF759F0C4B80356C35",
            "08BD2D247CC7AA38B8C4B7FD20EE7EDAD0B593C3DEBCE92F595C9D016DA40BAE",
        },
        /*trigger*/
        extensions::SafeBrowsingPrivateEventRouter::kTriggerFileDownload,
        expected_dlp_verdicts,
        /*mimetypes*/ TxtMimeTypes(),
        /*size*/ std::string("foo.exe").size(),
        // Both results are BLOCKED since the highest precedence result will
        // determine Chrome's UX for a given download.
        /*results*/
        {enterprise_connectors::EventResultToString(
             enterprise_connectors::EventResult::BLOCKED),
         enterprise_connectors::EventResultToString(
             enterprise_connectors::EventResult::BLOCKED)},
        /*username*/ kUserName,
        /*profile_identifier*/ profile_->GetPath().AsUTF8Unsafe(),
        /*scan IDs*/
        {
            kScanId + std::string("0"),
            kScanId + std::string("1"),
        },
        /*content_transfer_reason*/ std::nullopt,
        /*user_justification*/ std::nullopt);

    request.Start();
    run_loop.Run();

    EXPECT_EQ(DownloadCheckResult::SENSITIVE_CONTENT_BLOCK, last_result_);
    EXPECT_EQ(4u, download_protection_service_.GetFakeBinaryUploadService()
                      ->num_finished_requests());
    EXPECT_EQ(
        4u,
        download_protection_service_.GetFakeBinaryUploadService()->num_acks());
    download_protection_service_.GetFakeBinaryUploadService()->Reset();
  }
}

TEST_P(DeepScanningReportingSourceTypeTest, Timeout) {
  base::RunLoop run_loop;
  DeepScanningRequest request(
      CreateMetadata(),
      DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
      DownloadCheckResult::SAFE,
      base::BindRepeating(
          [](DeepScanningRequestTest* test, base::RepeatingClosure quit_closure,
             DownloadCheckResult result) {
            test->SetLastResult(result);
            if (result != DownloadCheckResult::ASYNC_SCANNING) {
              quit_closure.Run();
            }
          },
          base::Unretained(this), run_loop.QuitClosure()),
      &download_protection_service_, settings().value(),
      /*password=*/std::nullopt);

  download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
      download_path_, BinaryUploadService::Result::TIMEOUT,
      enterprise_connectors::ContentAnalysisResponse());

  enterprise_connectors::test::EventReportValidator validator(client_.get());
  validator.ExpectUnscannedFileEvent(
      /*url*/ "https://example.com/download.exe",
      /*tab_url*/ "https://example.com/",
      /*source*/ "",
      /*destination*/ "",
      /*filename*/ download_path_.AsUTF8Unsafe(),
      // printf "download contents" | sha256sum |  tr '[:lower:]' '[:upper:]'
      /*sha256*/
      "76E00EB33811F5778A5EE557512C30D9341D4FEB07646BCE3E4DB13F9428573C",
      /*trigger*/
      extensions::SafeBrowsingPrivateEventRouter::kTriggerFileDownload,
      /*reason*/ "TIMEOUT",
      /*mimetypes*/ ExeMimeTypes(),
      /*size*/ std::string("download contents").size(),
      /*result*/
      enterprise_connectors::EventResultToString(
          enterprise_connectors::EventResult::ALLOWED),
      /*username*/ kUserName,
      /*profile_identifier*/ profile_->GetPath().AsUTF8Unsafe(),
      /*content_transfer_reason*/ std::nullopt);

  request.Start();

  run_loop.Run();

  EXPECT_EQ(DownloadCheckResult::SAFE, last_result_);
}

INSTANTIATE_TEST_SUITE_P(
    Reporting,
    DeepScanningReportingSourceTypeTest,
    testing::Values(MetadataSourceType::kDownloadItem,
                    MetadataSourceType::kFileSystemAccessWriteItem));

class DeepScanningDownloadFailClosedTest
    : public DeepScanningRequestTest,
      public testing::WithParamInterface<
          std::tuple<safe_browsing::BinaryUploadService::Result, bool>> {
 public:
  safe_browsing::BinaryUploadService::Result upload_result() const {
    return std::get<0>(GetParam());
  }

  bool should_fail_closed() const { return std::get<1>(GetParam()); }

  // Use a string since the setting value is inserted into a JSON policy.
  const char* default_action_setting_value() const {
    return should_fail_closed() ? "block" : "allow";
  }
};

INSTANTIATE_TEST_SUITE_P(
    ,
    DeepScanningDownloadFailClosedTest,
    testing::Combine(
        testing::Values(
            safe_browsing::BinaryUploadService::Result::UPLOAD_FAILURE,
            safe_browsing::BinaryUploadService::Result::TIMEOUT,
            safe_browsing::BinaryUploadService::Result::FAILED_TO_GET_TOKEN,
            safe_browsing::BinaryUploadService::Result::TOO_MANY_REQUESTS,
            safe_browsing::BinaryUploadService::Result::UNKNOWN),
        testing::Bool()));

TEST_P(DeepScanningDownloadFailClosedTest, HandlesDefaultActionCorrectly) {
  constexpr char kDefaultActionPref[] = R"({
    "service_provider": "google",
    "enable": [
      {
        "url_list": ["*"],
        "tags": ["dlp"]
      }
    ],
    "block_until_verdict": 1,
    "default_action": "%s"
    })";

  enterprise_connectors::test::SetAnalysisConnector(
      profile_->GetPrefs(), enterprise_connectors::FILE_DOWNLOADED,
      base::StringPrintf(kDefaultActionPref, default_action_setting_value()));

  base::RunLoop run_loop;
  DeepScanningRequest request(
      CreateMetadata(),
      DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
      DownloadCheckResult::SAFE,
      base::BindLambdaForTesting([this, quit_closure = run_loop.QuitClosure()](
                                     DownloadCheckResult result) {
        SetLastResult(result);
        if (result != DownloadCheckResult::ASYNC_SCANNING) {
          quit_closure.Run();
        }
      }),
      &download_protection_service_, settings().value(),
      /*password=*/std::nullopt);

  download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
      download_path_, upload_result(),
      enterprise_connectors::ContentAnalysisResponse());

  request.Start();

  run_loop.Run();

  if (should_fail_closed()) {
    EXPECT_EQ(DownloadCheckResult::BLOCKED_SCAN_FAILED, last_result_);
  } else {
    EXPECT_EQ(DownloadCheckResult::SAFE, last_result_);
  }
}

class DeepScanningDownloadRestrictionsTest
    : public DeepScanningReportingTest,
      public testing::WithParamInterface<policy::DownloadRestriction> {
 public:
  void SetUp() override {
    DeepScanningReportingTest::SetUp();
    profile_->GetPrefs()->SetInteger(
        policy::policy_prefs::kDownloadRestrictions,
        static_cast<int>(download_restriction()));
  }

  policy::DownloadRestriction download_restriction() const {
    return testing::WithParamInterface<policy::DownloadRestriction>::GetParam();
  }

  enterprise_connectors::EventResult expected_event_result_for_malware() const {
    switch (download_restriction()) {
      case policy::DownloadRestriction::NONE:
        return enterprise_connectors::EventResult::WARNED;
      case policy::DownloadRestriction::DANGEROUS_FILES:
      case policy::DownloadRestriction::MALICIOUS_FILES:
      case policy::DownloadRestriction::POTENTIALLY_DANGEROUS_FILES:
      case policy::DownloadRestriction::ALL_FILES:
        return enterprise_connectors::EventResult::BLOCKED;
    }
  }

  enterprise_connectors::EventResult expected_event_result_for_safe_large_file()
      const {
    switch (download_restriction()) {
      case policy::DownloadRestriction::NONE:
      case policy::DownloadRestriction::DANGEROUS_FILES:
      case policy::DownloadRestriction::MALICIOUS_FILES:
      case policy::DownloadRestriction::POTENTIALLY_DANGEROUS_FILES:
        return enterprise_connectors::EventResult::ALLOWED;
      case policy::DownloadRestriction::ALL_FILES:
        return enterprise_connectors::EventResult::BLOCKED;
    }
  }

  enterprise_connectors::ContentAnalysisAcknowledgement::FinalAction
  expected_final_action() const {
    switch (download_restriction()) {
      case policy::DownloadRestriction::NONE:
        return enterprise_connectors::ContentAnalysisAcknowledgement::WARN;
      case policy::DownloadRestriction::DANGEROUS_FILES:
      case policy::DownloadRestriction::MALICIOUS_FILES:
      case policy::DownloadRestriction::POTENTIALLY_DANGEROUS_FILES:
      case policy::DownloadRestriction::ALL_FILES:
        return enterprise_connectors::ContentAnalysisAcknowledgement::BLOCK;
    }
  }
};

INSTANTIATE_TEST_SUITE_P(
    ,
    DeepScanningDownloadRestrictionsTest,
    testing::Values(policy::DownloadRestriction::NONE,
                    policy::DownloadRestriction::DANGEROUS_FILES,
                    policy::DownloadRestriction::POTENTIALLY_DANGEROUS_FILES,
                    policy::DownloadRestriction::ALL_FILES,
                    policy::DownloadRestriction::MALICIOUS_FILES));

TEST_P(DeepScanningDownloadRestrictionsTest, GeneratesCorrectReport) {
  enterprise_connectors::test::SetAnalysisConnector(
      profile_->GetPrefs(), enterprise_connectors::FILE_DOWNLOADED,
      kScanForMalware);
  {
    base::RunLoop run_loop;

    DeepScanningRequest request(
        CreateMetadata(),
        DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
        DownloadCheckResult::SAFE,
        base::BindRepeating(
            [](DeepScanningRequestTest* test,
               base::RepeatingClosure quit_closure,
               DownloadCheckResult result) {
              test->SetLastResult(result);
              if (result != DownloadCheckResult::ASYNC_SCANNING) {
                quit_closure.Run();
              }
            },
            base::Unretained(this), run_loop.QuitClosure()),
        &download_protection_service_, settings().value(),
        /*password=*/std::nullopt);

    enterprise_connectors::ContentAnalysisResponse response;
    response.set_request_token(kScanId);

    auto* malware_result = response.add_results();
    malware_result->set_tag("malware");
    malware_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);
    auto* malware_rule = malware_result->add_triggered_rules();
    malware_rule->set_action(enterprise_connectors::TriggeredRule::BLOCK);
    malware_rule->set_rule_name("malware");

    download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
        download_path_, BinaryUploadService::Result::SUCCESS, response);
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetExpectedFinalAction(expected_final_action());

    enterprise_connectors::test::EventReportValidator validator(client_.get());
    validator.ExpectDangerousDeepScanningResult(
        /*url*/ "https://example.com/download.exe",
        /*tab_url*/ "https://example.com/",
        /*source*/ "",
        /*destination*/ "",
        /*filename*/ download_path_.AsUTF8Unsafe(),
        // printf "download contents" | sha256sum |  tr '[:lower:]'
        // '[:upper:]'
        /*sha256*/
        "76E00EB33811F5778A5EE557512C30D9341D4FEB07646BCE3E4DB13F9428573C",
        /*threat_type*/ "DANGEROUS",
        /*trigger*/
        extensions::SafeBrowsingPrivateEventRouter::kTriggerFileDownload,
        /*mimetypes*/ ExeMimeTypes(),
        /*size*/ std::string("download contents").size(),
        /*result*/
        enterprise_connectors::EventResultToString(
            expected_event_result_for_malware()),
        /*username*/ kUserName,
        /*profile_identifier*/ profile_->GetPath().AsUTF8Unsafe(),
        /*scan_id*/ kScanId);

    request.Start();

    run_loop.Run();

    EXPECT_EQ(DownloadCheckResult::DANGEROUS, last_result_);
  }
  {
    base::RunLoop run_loop;

    DeepScanningRequest request(
        CreateMetadata(),
        DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
        DownloadCheckResult::SAFE,
        base::BindRepeating(
            [](DeepScanningRequestTest* test,
               base::RepeatingClosure quit_closure,
               DownloadCheckResult result) {
              test->SetLastResult(result);
              if (result != DownloadCheckResult::ASYNC_SCANNING) {
                quit_closure.Run();
              }
            },
            base::Unretained(this), run_loop.QuitClosure()),
        &download_protection_service_, settings().value(),
        /*password=*/std::nullopt);

    enterprise_connectors::ContentAnalysisResponse response;
    response.set_request_token(kScanId);

    auto* malware_result = response.add_results();
    malware_result->set_tag("malware");
    malware_result->set_status(
        enterprise_connectors::ContentAnalysisResponse::Result::SUCCESS);
    auto* malware_rule = malware_result->add_triggered_rules();
    malware_rule->set_action(enterprise_connectors::TriggeredRule::WARN);
    malware_rule->set_rule_name("uws");

    download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
        download_path_, BinaryUploadService::Result::SUCCESS, response);
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetExpectedFinalAction(
            enterprise_connectors::ContentAnalysisAcknowledgement::WARN);

    enterprise_connectors::test::EventReportValidator validator(client_.get());
    validator.ExpectDangerousDeepScanningResult(
        /*url*/ "https://example.com/download.exe",
        /*tab_url*/ "https://example.com/",
        /*source*/ "",
        /*destination*/ "",
        /*filename*/ download_path_.AsUTF8Unsafe(),
        // printf "download contents" | sha256sum |  tr '[:lower:]'
        // '[:upper:]'
        /*sha256*/
        "76E00EB33811F5778A5EE557512C30D9341D4FEB07646BCE3E4DB13F9428573C",
        /*threat_type*/ "POTENTIALLY_UNWANTED",
        /*trigger*/
        extensions::SafeBrowsingPrivateEventRouter::kTriggerFileDownload,
        /*mimetypes*/ ExeMimeTypes(),
        /*size*/ std::string("download contents").size(),
        /*result*/
        enterprise_connectors::EventResultToString(
            enterprise_connectors::EventResult::WARNED),
        /*username*/ kUserName,
        /*profile_identifier*/ profile_->GetPath().AsUTF8Unsafe(),
        /*scan_id*/ kScanId);

    request.Start();

    run_loop.Run();

    EXPECT_EQ(DownloadCheckResult::POTENTIALLY_UNWANTED, last_result_);
  }

  {
    base::RunLoop run_loop;
    DeepScanningRequest request(
        CreateMetadata(),
        DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
        DownloadCheckResult::SAFE,
        base::BindRepeating(
            [](DeepScanningRequestTest* test,
               base::RepeatingClosure quit_closure,
               DownloadCheckResult result) {
              test->SetLastResult(result);
              if (result != DownloadCheckResult::ASYNC_SCANNING) {
                quit_closure.Run();
              }
            },
            base::Unretained(this), run_loop.QuitClosure()),
        &download_protection_service_, settings().value(),
        /*password=*/std::nullopt);

    enterprise_connectors::ContentAnalysisResponse response;
    response.set_request_token(kScanId);

    download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
        download_path_, BinaryUploadService::Result::FILE_TOO_LARGE, response);
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetExpectedFinalAction(
            enterprise_connectors::ContentAnalysisAcknowledgement::BLOCK);

    enterprise_connectors::test::EventReportValidator validator(client_.get());
    validator.ExpectUnscannedFileEvent(
        /*url*/ "https://example.com/download.exe",
        /*tab_url*/ "https://example.com/",
        /*source*/ "",
        /*destination*/ "",
        /*filename*/ download_path_.AsUTF8Unsafe(),
        // printf "download contents" | sha256sum |  tr '[:lower:]' '[:upper:]'
        /*sha256*/
        "76E00EB33811F5778A5EE557512C30D9341D4FEB07646BCE3E4DB13F9428573C",
        /*trigger*/
        extensions::SafeBrowsingPrivateEventRouter::kTriggerFileDownload,
        /*reason*/ "FILE_TOO_LARGE",
        /*mimetypes*/ ExeMimeTypes(),
        /*size*/ std::string("download contents").size(),
        /*result*/
        enterprise_connectors::EventResultToString(
            expected_event_result_for_safe_large_file()),
        /*username*/ kUserName,
        /*profile_identifier*/ profile_->GetPath().AsUTF8Unsafe(),
        /*content_transfer_reason*/ std::nullopt);

    request.Start();

    run_loop.Run();

    EXPECT_EQ(DownloadCheckResult::SAFE, last_result_);
  }

  {
    base::RunLoop run_loop;
    // If `item_` has a dangerous DownloadDangerType before a deep scan and that
    // deep scan fails, the corresponding unscanned file event should match the
    // EventResult imposed by DownloadRestrictions.
    EXPECT_CALL(item_, GetDangerType())
        .WillRepeatedly(Return(download::DownloadDangerType::
                                   DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT));
    DeepScanningRequest request(
        CreateMetadata(),
        DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
        DownloadCheckResult::DANGEROUS,
        base::BindRepeating(
            [](DeepScanningRequestTest* test,
               base::RepeatingClosure quit_closure,
               DownloadCheckResult result) {
              test->SetLastResult(result);
              if (result != DownloadCheckResult::ASYNC_SCANNING) {
                quit_closure.Run();
              }
            },
            base::Unretained(this), run_loop.QuitClosure()),
        &download_protection_service_, settings().value(),
        /*password=*/std::nullopt);

    enterprise_connectors::ContentAnalysisResponse response;
    response.set_request_token(kScanId);

    download_protection_service_.GetFakeBinaryUploadService()->SetResponse(
        download_path_, BinaryUploadService::Result::FILE_TOO_LARGE, response);
    download_protection_service_.GetFakeBinaryUploadService()
        ->SetExpectedFinalAction(
            enterprise_connectors::ContentAnalysisAcknowledgement::BLOCK);

    enterprise_connectors::test::EventReportValidator validator(client_.get());
    validator.ExpectUnscannedFileEvent(
        /*url*/ "https://example.com/download.exe",
        /*tab_url*/ "https://example.com/",
        /*source*/ "",
        /*destination*/ "",
        /*filename*/ download_path_.AsUTF8Unsafe(),
        // printf "download contents" | sha256sum |  tr '[:lower:]' '[:upper:]'
        /*sha256*/
        "76E00EB33811F5778A5EE557512C30D9341D4FEB07646BCE3E4DB13F9428573C",
        /*trigger*/
        extensions::SafeBrowsingPrivateEventRouter::kTriggerFileDownload,
        /*reason*/ "FILE_TOO_LARGE",
        /*mimetypes*/ ExeMimeTypes(),
        /*size*/ std::string("download contents").size(),
        /*result*/
        enterprise_connectors::EventResultToString(
            expected_event_result_for_malware()),
        /*username*/ kUserName,
        /*profile_identifier*/ profile_->GetPath().AsUTF8Unsafe(),
        /*content_transfer_reason*/ std::nullopt);

    request.Start();

    run_loop.Run();

    EXPECT_EQ(DownloadCheckResult::DANGEROUS, last_result_);
  }
}

class DeepScanningRequestConnectorsFeatureTest
    : public DeepScanningRequestTest {};

TEST_F(DeepScanningRequestConnectorsFeatureTest,
       ShouldUploadBinary_MalwareListPolicy) {
  enterprise_connectors::test::SetAnalysisConnector(
      profile_->GetPrefs(), enterprise_connectors::FILE_DOWNLOADED,
      kScanForMalware);

  content::DownloadItemUtils::AttachInfoForTesting(&item_, profile_, nullptr);
  EXPECT_CALL(item_, GetURL()).WillRepeatedly(ReturnRef(download_url_));

  // Without the malware policy list set, the item should be uploaded.
  ValidateDefaultSettings(settings());

  // With the old malware policy list set, the item should be uploaded since
  // DeepScanningRequest ignores that policy.
  AddUrlToProfilePrefList(prefs::kSafeBrowsingAllowlistDomains, download_url_);
  ValidateDefaultSettings(settings());

  // With the new malware policy list set, the item should not be uploaded since
  // DeepScanningRequest honours that policy.
  enterprise_connectors::test::SetAnalysisConnector(
      profile_->GetPrefs(), enterprise_connectors::FILE_DOWNLOADED,
      base::StringPrintf(
          R"({
                            "service_provider": "google",
                            "enable": [
                              {"url_list": ["*"], "tags": ["malware"]}
                            ],
                            "disable": [
                              {"url_list": ["%s"], "tags": ["malware"]}
                            ],
                            "block_until_verdict": 1
                          })",
          download_url_.host().c_str()));
  EXPECT_FALSE(settings().has_value());
}

TEST_F(DeepScanningRequestConnectorsFeatureTest, ShouldUploadBinary_FileURLs) {
  enterprise_connectors::test::SetAnalysisConnector(
      profile_->GetPrefs(), enterprise_connectors::FILE_DOWNLOADED,
      kScanForDlpAndMalware);

  content::DownloadItemUtils::AttachInfoForTesting(&item_, profile_, nullptr);

  // Even if the policy indicates scanning should occur, file:/// URLs should
  // never return settings.
  GURL url_1("file:///a/path/to/a/file");
  EXPECT_CALL(item_, GetURL()).WillRepeatedly(ReturnRef(url_1));
  EXPECT_FALSE(settings().has_value());

  GURL url_2("file:///file.txt");
  EXPECT_CALL(item_, GetURL()).WillRepeatedly(ReturnRef(url_2));
  EXPECT_FALSE(settings().has_value());

  GURL url_3("file:///C:\\a\\path\\to\\a\\file");
  EXPECT_CALL(item_, GetURL()).WillRepeatedly(ReturnRef(url_3));
  EXPECT_FALSE(settings().has_value());

  GURL url_4("file:///C:\\file.txt");
  EXPECT_CALL(item_, GetURL()).WillRepeatedly(ReturnRef(url_4));
  EXPECT_FALSE(settings().has_value());
}

TEST_P(DeepScanningRequestAllFeaturesEnabledTest, PopulatesRequest) {
  enterprise_connectors::test::SetAnalysisConnector(
      profile_->GetPrefs(), enterprise_connectors::FILE_DOWNLOADED,
      kScanForDlpAndMalware);

  // Override mime type to platform-independent value for testing.
  net::ScopedOverrideGetMimeTypeForTesting override_mime_type(
      "application/octet-stream");

  base::RunLoop run_loop;
  DeepScanningRequest request(
      CreateMetadata(),
      DownloadItemWarningData::DeepScanTrigger::TRIGGER_POLICY,
      DownloadCheckResult::SAFE,
      base::BindRepeating(
          [](base::RepeatingClosure closure, DownloadCheckResult result) {
            if (result != DownloadCheckResult::ASYNC_SCANNING) {
              closure.Run();
            }
          },
          run_loop.QuitClosure()),
      &download_protection_service_, settings().value(),
      /*password=*/std::nullopt);

  request.Start();
  run_loop.Run();
  EXPECT_EQ(download_protection_service_.GetFakeBinaryUploadService()
                ->last_request()
                .request_data()
                .filename(),
            download_path_.AsUTF8Unsafe());
  EXPECT_EQ(download_protection_service_.GetFakeBinaryUploadService()
                ->last_request()
                .request_data()
                .digest(),
            // Hex-encoding of 'hash'
            "76E00EB33811F5778A5EE557512C30D9341D4FEB07646BCE3E4DB13F9428573C");
  EXPECT_EQ(download_protection_service_.GetFakeBinaryUploadService()
                ->last_request()
                .request_data()
                .content_type(),
            "application/octet-stream");
  EXPECT_EQ(download_protection_service_.GetFakeBinaryUploadService()
                ->last_request()
                .request_data()
                .tab_url(),
            GURL("https://example.com"));
}

INSTANTIATE_TEST_SUITE_P(
    AllFeaturesEnabled,
    DeepScanningRequestAllFeaturesEnabledTest,
    testing::Values(MetadataSourceType::kDownloadItem,
                    MetadataSourceType::kFileSystemAccessWriteItem));

}  // namespace safe_browsing
