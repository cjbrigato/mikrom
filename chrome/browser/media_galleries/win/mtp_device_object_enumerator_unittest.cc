// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// MTPDeviceObjectEnumerator unit tests.

#include "chrome/browser/media_galleries/win/mtp_device_object_enumerator.h"

#include <stddef.h>
#include <stdint.h>

#include <ctime>
#include <string>

#include "base/time/time.h"
#include "chrome/browser/media_galleries/win/mtp_device_object_entry.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

struct MTPDeviceObjectEntryData {
  // Friendly name of the object, e.g. "IMG_9911.jpeg".
  std::u16string name;

  // The object identifier, e.g. "o299".
  std::wstring object_id;

  // True if the current object is a directory/folder/album content type.
  bool is_directory;

  // The object file size in bytes, e.g. "882992".
  int64_t size;

  // Last modified time of the object.
  time_t last_modified_time;
};

const MTPDeviceObjectEntryData kTestCases[] = {
    {u"File_1", L"o100", false, 10023, 1121},
    {u"Directory_1", L"o52", true, 99833, 2231},
    {u"File_2", L"o230", false, 8733, 7372},
};

void TestEnumeratorIsEmpty(MTPDeviceObjectEnumerator* enumerator) {
  EXPECT_EQ(std::wstring(), enumerator->GetObjectId());
  EXPECT_EQ(0, enumerator->Size());
  EXPECT_FALSE(enumerator->IsDirectory());
  EXPECT_TRUE(enumerator->LastModifiedTime().is_null());
}

void TestNextEntryIsEmpty(MTPDeviceObjectEnumerator* enumerator) {
  EXPECT_TRUE(enumerator->Next().empty());
}

typedef testing::Test MTPDeviceObjectEnumeratorWinTest;

TEST_F(MTPDeviceObjectEnumeratorWinTest, Empty) {
  MTPDeviceObjectEntries entries;
  MTPDeviceObjectEnumerator enumerator(entries);
  TestEnumeratorIsEmpty(&enumerator);
  TestNextEntryIsEmpty(&enumerator);
  TestNextEntryIsEmpty(&enumerator);
  TestEnumeratorIsEmpty(&enumerator);
}

TEST_F(MTPDeviceObjectEnumeratorWinTest, Traversal) {
  MTPDeviceObjectEntries entries;
  for (const auto& test_case : kTestCases) {
    entries.push_back(MTPDeviceObjectEntry(
        test_case.object_id, test_case.name, test_case.is_directory,
        test_case.size, base::Time::FromTimeT(test_case.last_modified_time)));
  }
  MTPDeviceObjectEnumerator enumerator(entries);
  TestEnumeratorIsEmpty(&enumerator);
  TestEnumeratorIsEmpty(&enumerator);
  for (const auto& test_case : kTestCases) {
    EXPECT_EQ(test_case.name, enumerator.Next().AsUTF16Unsafe());
    EXPECT_EQ(test_case.object_id, enumerator.GetObjectId());
    EXPECT_EQ(test_case.size, enumerator.Size());
    EXPECT_EQ(test_case.is_directory, enumerator.IsDirectory());
    EXPECT_EQ(test_case.last_modified_time,
              enumerator.LastModifiedTime().ToTimeT());
  }
  TestNextEntryIsEmpty(&enumerator);
  TestNextEntryIsEmpty(&enumerator);
  TestEnumeratorIsEmpty(&enumerator);
  TestNextEntryIsEmpty(&enumerator);
  TestEnumeratorIsEmpty(&enumerator);
}

}  // namespace
