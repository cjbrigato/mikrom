// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/computed_hashes.h"

#include <array>

#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "crypto/sha2.h"
#include "extensions/browser/content_verifier/content_verifier_utils.h"
#include "extensions/common/constants.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

constexpr bool kIsFileAccessCaseInsensitive =
    !extensions::content_verifier_utils::IsFileAccessCaseSensitive();

struct HashInfo {
  base::FilePath path;
  int block_size;
  std::vector<std::string> hashes;
};

testing::AssertionResult WriteThenReadComputedHashes(
    const std::vector<HashInfo>& hash_infos,
    extensions::ComputedHashes* result) {
  base::ScopedTempDir scoped_dir;
  if (!scoped_dir.CreateUniqueTempDir()) {
    return testing::AssertionFailure() << "Failed to create temp dir.";
  }

  base::FilePath computed_hashes_path =
      scoped_dir.GetPath().AppendASCII("computed_hashes.json");
  extensions::ComputedHashes::Data computed_hashes_data;
  for (const auto& info : hash_infos)
    computed_hashes_data.Add(info.path, info.block_size, info.hashes);

  if (!extensions::ComputedHashes(std::move(computed_hashes_data))
           .WriteToFile(computed_hashes_path)) {
    return testing::AssertionFailure()
           << "Failed to write computed_hashes.json";
  }
  extensions::ComputedHashes::Status computed_hashes_status;
  std::optional<extensions::ComputedHashes> computed_hashes =
      extensions::ComputedHashes::CreateFromFile(computed_hashes_path,
                                                 &computed_hashes_status);
  if (!computed_hashes) {
    return testing::AssertionFailure()
           << "Failed to read computed_hashes.json (status: "
           << static_cast<int>(computed_hashes_status) << ")";
  }
  *result = std::move(computed_hashes.value());

  return testing::AssertionSuccess();
}

}  // namespace

namespace extensions {

TEST(ComputedHashesTest, ComputedHashes) {
  // We'll add hashes for 2 files, one of which uses a subdirectory
  // path. The first file will have a list of 1 block hash, and the
  // second file will have 2 block hashes.
  base::FilePath path1(FILE_PATH_LITERAL("foo.txt"));
  base::FilePath path2 =
      base::FilePath(FILE_PATH_LITERAL("foo")).AppendASCII("bar.txt");
  std::vector<std::string> hashes1 = {crypto::SHA256HashString("first")};
  std::vector<std::string> hashes2 = {crypto::SHA256HashString("second"),
                                      crypto::SHA256HashString("third")};
  const int kBlockSize1 = 4096;
  const int kBlockSize2 = 2048;

  ComputedHashes computed_hashes{ComputedHashes::Data()};
  ASSERT_TRUE(WriteThenReadComputedHashes(
      {{path1, kBlockSize1, hashes1}, {path2, kBlockSize2, hashes2}},
      &computed_hashes));

  // After reading hashes back assert that we got what we wrote.
  std::vector<std::string> read_hashes1;
  std::vector<std::string> read_hashes2;

  int block_size = 0;
  EXPECT_TRUE(computed_hashes.GetHashes(path1, &block_size, &read_hashes1));
  EXPECT_EQ(block_size, 4096);
  block_size = 0;
  EXPECT_TRUE(computed_hashes.GetHashes(path2, &block_size, &read_hashes2));
  EXPECT_EQ(block_size, 2048);

  EXPECT_EQ(hashes1, read_hashes1);
  EXPECT_EQ(hashes2, read_hashes2);

  // Make sure we can lookup hashes for a file using incorrect case
  base::FilePath path1_badcase(FILE_PATH_LITERAL("FoO.txt"));
  std::vector<std::string> read_hashes1_badcase;
  EXPECT_EQ(kIsFileAccessCaseInsensitive,
            computed_hashes.GetHashes(path1_badcase, &block_size,
                                      &read_hashes1_badcase));
  if (kIsFileAccessCaseInsensitive) {
    EXPECT_EQ(4096, block_size);
    EXPECT_EQ(hashes1, read_hashes1_badcase);
  }

  // Finally make sure that we can retrieve the hashes for the subdir
  // path even when that path contains forward slashes (on windows).
  base::FilePath path2_fwd_slashes =
      base::FilePath::FromUTF8Unsafe("foo/bar.txt");
  block_size = 0;
  EXPECT_TRUE(
      computed_hashes.GetHashes(path2_fwd_slashes, &block_size, &read_hashes2));
  EXPECT_EQ(hashes2, read_hashes2);
}

// Note: the expected hashes used in this test were generated using linux
// command line tools. E.g., from a bash prompt:
//  $ printf "hello world" | openssl dgst -sha256 -binary | base64
//
// The file with multiple-blocks expectations were generated by doing:
// $ for i in `seq 500 ; do printf "hello world" ; done > hello.txt
// $ dd if=hello.txt bs=4096 count=1 | openssl dgst -sha256 -binary | base64
// $ dd if=hello.txt skip=1 bs=4096 count=1 |
//   openssl dgst -sha256 -binary | base64
TEST(ComputedHashesTest, GetHashesForContent) {
  const int block_size = 4096;

  // Simple short input.
  std::string content1 = "hello world";
  std::string content1_expected_hash =
      "uU0nuZNNPgilLlLX2n2r+sSE7+N6U4DukIj3rOLvzek=";
  std::vector<std::string> hashes1 =
      ComputedHashes::GetHashesForContent(content1, block_size);
  ASSERT_EQ(1u, hashes1.size());
  EXPECT_EQ(content1_expected_hash, base::Base64Encode(hashes1[0]));

  // Multiple blocks input.
  std::string content2;
  for (int i = 0; i < 500; i++)
    content2 += "hello world";
  auto content2_expected_hashes = std::to_array<const char*>(
      {"bvtt5hXo8xvHrlzGAhhoqPL/r+4zJXHx+6wAvkv15V8=",
       "lTD45F7P6I/HOdi8u7FLRA4qzAYL+7xSNVeusG6MJI0="});
  std::vector<std::string> hashes2 =
      ComputedHashes::GetHashesForContent(content2, block_size);
  ASSERT_EQ(2u, hashes2.size());
  EXPECT_EQ(content2_expected_hashes[0], base::Base64Encode(hashes2[0]));
  EXPECT_EQ(content2_expected_hashes[1], base::Base64Encode(hashes2[1]));

  // Now an empty input.
  std::string content3;
  std::vector<std::string> hashes3 =
      ComputedHashes::GetHashesForContent(content3, block_size);
  ASSERT_EQ(1u, hashes3.size());
  ASSERT_EQ(std::string("47DEQpj8HBSa+/TImW+5JCeuQeRkm5NMpJWZG3hSuFU="),
            base::Base64Encode(hashes3[0]));
}

}  // namespace extensions
