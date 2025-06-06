// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/json_file_sanitizer.h"

#include <memory>
#include <optional>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/gmock_expected_support.h"
#include "base/types/expected.h"
#include "content/public/test/browser_task_environment.h"
#include "extensions/browser/extension_file_task_runner.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::base::test::ErrorIs;
using ::base::test::HasValue;

namespace extensions {

namespace {

class JsonFileSanitizerTest : public testing::Test {
 public:
  JsonFileSanitizerTest() {}

  JsonFileSanitizerTest(const JsonFileSanitizerTest&) = delete;
  JsonFileSanitizerTest& operator=(const JsonFileSanitizerTest&) = delete;

 protected:
  base::FilePath CreateFilePath(const base::FilePath::StringType& file_name) {
    return temp_dir_.GetPath().Append(file_name);
  }

  void CreateValidJsonFile(const base::FilePath& path) {
    std::string kJson = "{\"hello\":\"bonjour\"}";
    ASSERT_TRUE(base::WriteFile(path, kJson));
  }

  void CreateInvalidJsonFile(const base::FilePath& path) {
    std::string kJson = "sjkdsk;'<?js";
    ASSERT_TRUE(base::WriteFile(path, kJson));
  }

  const base::FilePath& GetJsonFilePath() const { return temp_dir_.GetPath(); }

  void WaitForSanitizationDone() {
    ASSERT_FALSE(done_callback_);
    base::RunLoop run_loop;
    done_callback_ = run_loop.QuitClosure();
    run_loop.Run();
  }

  void CreateAndStartSanitizer(const std::set<base::FilePath>& file_paths) {
    sanitizer_ = JsonFileSanitizer::CreateAndStart(
        file_paths,
        base::BindOnce(&JsonFileSanitizerTest::SanitizationDone,
                       base::Unretained(this)),
        GetExtensionFileTaskRunner());
  }

  base::expected<void, JsonFileSanitizer::Error> last_reported_status() const {
    return last_status_;
  }

 private:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  void SanitizationDone(base::expected<void, JsonFileSanitizer::Error> status) {
    last_status_ = status;
    if (done_callback_) {
      std::move(done_callback_).Run();
    }
  }

  content::BrowserTaskEnvironment task_environment_;
  base::expected<void, JsonFileSanitizer::Error> last_status_;
  base::OnceClosure done_callback_;
  std::unique_ptr<JsonFileSanitizer> sanitizer_;
  base::ScopedTempDir temp_dir_;
};

}  // namespace

TEST_F(JsonFileSanitizerTest, NoFilesProvided) {
  CreateAndStartSanitizer(std::set<base::FilePath>());
  WaitForSanitizationDone();
  EXPECT_THAT(last_reported_status(), HasValue());
}

TEST_F(JsonFileSanitizerTest, ValidCase) {
  constexpr std::array<const base::FilePath::CharType* const, 10> kFileNames{
      {FILE_PATH_LITERAL("test0"), FILE_PATH_LITERAL("test1"),
       FILE_PATH_LITERAL("test2"), FILE_PATH_LITERAL("test3"),
       FILE_PATH_LITERAL("test4"), FILE_PATH_LITERAL("test5"),
       FILE_PATH_LITERAL("test6"), FILE_PATH_LITERAL("test7"),
       FILE_PATH_LITERAL("test8"), FILE_PATH_LITERAL("test9")}};
  std::set<base::FilePath> paths;
  for (const base::FilePath::CharType* file_name : kFileNames) {
    base::FilePath path = CreateFilePath(file_name);
    CreateValidJsonFile(path);
    paths.insert(path);
  }
  CreateAndStartSanitizer(paths);
  WaitForSanitizationDone();
  EXPECT_THAT(last_reported_status(), HasValue());
  // Make sure the JSON files are there and non empty.
  for (const auto& path : paths) {
    std::optional<int64_t> file_size = base::GetFileSize(path);
    ASSERT_TRUE(file_size.has_value());
    EXPECT_GT(file_size.value(), 0);
  }
}

TEST_F(JsonFileSanitizerTest, MissingJsonFile) {
  constexpr base::FilePath::CharType kGoodName[] =
      FILE_PATH_LITERAL("i_exists");
  constexpr base::FilePath::CharType kNonExistingName[] =
      FILE_PATH_LITERAL("i_don_t_exist");
  base::FilePath good_path = CreateFilePath(kGoodName);
  CreateValidJsonFile(good_path);
  base::FilePath invalid_path = CreateFilePath(kNonExistingName);
  CreateAndStartSanitizer({good_path, invalid_path});
  WaitForSanitizationDone();
  EXPECT_THAT(last_reported_status(),
              ErrorIs(JsonFileSanitizer::Error::kFileReadError));
}

TEST_F(JsonFileSanitizerTest, InvalidJson) {
  constexpr base::FilePath::CharType kGoodJsonFileName[] =
      FILE_PATH_LITERAL("good.json");
  constexpr base::FilePath::CharType kBadJsonFileName[] =
      FILE_PATH_LITERAL("bad.json");
  base::FilePath good_path = CreateFilePath(kGoodJsonFileName);
  CreateValidJsonFile(good_path);
  base::FilePath badd_path = CreateFilePath(kBadJsonFileName);
  CreateInvalidJsonFile(badd_path);
  CreateAndStartSanitizer({good_path, badd_path});
  WaitForSanitizationDone();
  EXPECT_THAT(last_reported_status(),
              ErrorIs(JsonFileSanitizer::Error::kDecodingError));
}

}  // namespace extensions
