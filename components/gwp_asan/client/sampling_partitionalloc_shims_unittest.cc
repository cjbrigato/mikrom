// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40285824): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include "components/gwp_asan/client/sampling_partitionalloc_shims.h"

#include <stdlib.h>

#include <algorithm>
#include <iterator>
#include <set>
#include <string>

#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/page_size.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/gtest_util.h"
#include "base/test/multiprocess_test.h"
#include "base/test/test_timeouts.h"
#include "build/build_config.h"
#include "components/crash/core/common/crash_key.h"
#include "components/gwp_asan/client/guarded_page_allocator.h"
#include "components/gwp_asan/client/gwp_asan.h"
#include "components/gwp_asan/common/crash_key_name.h"
#include "partition_alloc/partition_alloc.h"
#include "partition_alloc/partition_root.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/multiprocess_func_list.h"

// PartitionAlloc (and hence hooking) are disabled with sanitizers that replace
// allocation routines.
#if !defined(MEMORY_TOOL_REPLACES_ALLOCATOR)

// These tests install global PartitionAlloc hooks so they are not safe to run
// in multi-threaded contexts. Instead they're implemented as multi-process
// tests.

namespace gwp_asan {
namespace internal {

extern GuardedPageAllocator& GetPartitionAllocGpaForTesting();

namespace {

constexpr const char* kFakeType = "fake type";
constexpr const char* kFakeType2 = "fake type #2";
constexpr size_t kSamplingFrequency = 10;

// Number of loop iterations required to definitely hit a sampled allocation.
constexpr size_t kLoopIterations = kSamplingFrequency * 4;

constexpr int kSuccess = 0;
constexpr int kFailure = 1;
constexpr int kSamplingMaxSize = 16;

static constexpr size_t kMaxMetadata = 2048;
static constexpr size_t kMaxRequestedSlots = 8192;

constexpr partition_alloc::PartitionOptions kAllocatorOptions = {};

static void HandleOOM(size_t unused_size) {
  LOG(FATAL) << "Out of memory.";
}

class SamplingPartitionAllocShimsTest : public base::MultiProcessTest {
 public:
  static void multiprocessTestSetup() {
    crash_reporter::InitializeCrashKeys();
    partition_alloc::PartitionAllocGlobalInit(HandleOOM);
    CHECK(InstallPartitionAllocHooks(
        AllocatorSettings{
            .max_allocated_pages = kMaxMetadata,
            .num_metadata = kMaxMetadata,
            .total_pages = kMaxRequestedSlots,
            .sampling_frequency = kSamplingFrequency,
            .sampling_min_size = 1,
            .sampling_max_size = std::numeric_limits<int>::max(),
        },
        base::DoNothing()));
  }

  static void multiprocessTestSetupWithSamplingMaxSize() {
    crash_reporter::InitializeCrashKeys();
    partition_alloc::PartitionAllocGlobalInit(HandleOOM);
    CHECK(InstallPartitionAllocHooks(
        AllocatorSettings{.max_allocated_pages = kMaxMetadata,
                          .num_metadata = kMaxMetadata,
                          .total_pages = kMaxRequestedSlots,
                          .sampling_frequency = kSamplingFrequency,
                          .sampling_min_size = 1,
                          .sampling_max_size = kSamplingMaxSize},
        base::DoNothing()));
  }

 protected:
  void runTest(const char* name) {
    base::Process process = SpawnChild(name);
    int exit_code = -1;
    ASSERT_TRUE(WaitForMultiprocessTestChildExit(
        process, TestTimeouts::action_max_timeout(), &exit_code));
    EXPECT_EQ(exit_code, kSuccess);
  }
};

MULTIPROCESS_TEST_MAIN_WITH_SETUP(
    BasicFunctionality,
    SamplingPartitionAllocShimsTest::multiprocessTestSetup) {
  partition_alloc::PartitionAllocator allocator;
  allocator.init(kAllocatorOptions);
  for (size_t i = 0; i < kLoopIterations; i++) {
    void* ptr = allocator.root()->Alloc(1, kFakeType);
    if (GetPartitionAllocGpaForTesting().PointerIsMine(ptr))
      return kSuccess;

    allocator.root()->Free(ptr);
  }

  return kFailure;
}

TEST_F(SamplingPartitionAllocShimsTest, BasicFunctionality) {
  runTest("BasicFunctionality");
}

MULTIPROCESS_TEST_MAIN_WITH_SETUP(
    Realloc,
    SamplingPartitionAllocShimsTest::multiprocessTestSetup) {
  partition_alloc::PartitionAllocator allocator;
  allocator.init(kAllocatorOptions);

  void* alloc = GetPartitionAllocGpaForTesting().Allocate(base::GetPageSize());
  CHECK_NE(alloc, nullptr);

  constexpr unsigned char kFillChar = 0xff;
  memset(alloc, kFillChar, base::GetPageSize());

  unsigned char* new_alloc = static_cast<unsigned char*>(
      allocator.root()->Realloc(alloc, base::GetPageSize() + 1, kFakeType));
  CHECK_NE(alloc, new_alloc);
  CHECK_EQ(GetPartitionAllocGpaForTesting().PointerIsMine(new_alloc), false);

  for (size_t i = 0; i < base::GetPageSize(); i++)
    CHECK_EQ(new_alloc[i], kFillChar);

  allocator.root()->Free(new_alloc);
  return kSuccess;
}

TEST_F(SamplingPartitionAllocShimsTest, Realloc) {
  runTest("Realloc");
}

// Ensure sampled GWP-ASan allocations with different types never overlap.
MULTIPROCESS_TEST_MAIN_WITH_SETUP(
    DifferentTypesDontOverlap,
    SamplingPartitionAllocShimsTest::multiprocessTestSetup) {
  partition_alloc::PartitionAllocator allocator;
  allocator.init(kAllocatorOptions);

  std::set<void*> type1, type2;
  for (size_t i = 0; i < kLoopIterations * kMaxRequestedSlots;
       i++) {
    void* ptr1 = allocator.root()->Alloc(1, kFakeType);
    void* ptr2 = allocator.root()->Alloc(1, kFakeType2);

    if (GetPartitionAllocGpaForTesting().PointerIsMine(ptr1))
      type1.insert(ptr1);
    if (GetPartitionAllocGpaForTesting().PointerIsMine(ptr2))
      type2.insert(ptr2);

    allocator.root()->Free(ptr1);
    allocator.root()->Free(ptr2);
  }

  std::vector<void*> intersection;
  std::set_intersection(type1.begin(), type1.end(), type2.begin(), type2.end(),
                        std::back_inserter(intersection));

  if (intersection.size() != 0)
    return kFailure;

  return kSuccess;
}

TEST_F(SamplingPartitionAllocShimsTest, DifferentTypesDontOverlap) {
  runTest("DifferentTypesDontOverlap");
}

// GetCrashKeyValue() operates on a per-component basis, can't read the crash
// key from the gwp_asan_client component in a component build.
#if !defined(COMPONENT_BUILD)
MULTIPROCESS_TEST_MAIN_WITH_SETUP(
    CrashKey,
    SamplingPartitionAllocShimsTest::multiprocessTestSetup) {
  if (crash_reporter::GetCrashKeyValue(kPartitionAllocCrashKey) !=
      GetPartitionAllocGpaForTesting().GetCrashKey()) {
    return kFailure;
  }

  return kSuccess;
}

TEST_F(SamplingPartitionAllocShimsTest, CrashKey) {
  runTest("CrashKey");
}
#endif  // !defined(COMPONENT_BUILD)

MULTIPROCESS_TEST_MAIN_WITH_SETUP(
    SamplingRange,
    SamplingPartitionAllocShimsTest::multiprocessTestSetupWithSamplingMaxSize) {
  partition_alloc::PartitionAllocator allocator;
  allocator.init(kAllocatorOptions);

  for (size_t i = 0; i < kLoopIterations; i++) {
    void* ptr = allocator.root()->Alloc(kSamplingMaxSize * 2, kFakeType);
    if (GetPartitionAllocGpaForTesting().PointerIsMine(ptr)) {
      return kFailure;
    }
    allocator.root()->Free(ptr);
  }

  return kSuccess;
}

TEST_F(SamplingPartitionAllocShimsTest, SamplingRange) {
  runTest("SamplingRange");
}

}  // namespace

}  // namespace internal
}  // namespace gwp_asan

#endif  // !defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
