// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/no_destructor.h"

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/barrier_closure.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/notreached.h"
#include "base/system/sys_info.h"
#include "base/threading/platform_thread.h"
#include "base/threading/simple_thread.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

namespace {

static_assert(!std::is_trivially_destructible_v<std::string>);
static_assert(
    std::is_trivially_destructible_v<base::NoDestructor<std::string>>);

struct NotreachedOnDestroy {
  ~NotreachedOnDestroy() { NOTREACHED(); }
};

TEST(NoDestructorTest, SkipsDestructors) {
  NoDestructor<NotreachedOnDestroy> destructor_should_not_run;
}

struct UncopyableUnmovable {
  UncopyableUnmovable() = default;
  explicit UncopyableUnmovable(int value) : value(value) {}

  UncopyableUnmovable(const UncopyableUnmovable&) = delete;
  UncopyableUnmovable& operator=(const UncopyableUnmovable&) = delete;

  int value = 1;
  std::string something_with_a_nontrivial_destructor;
};

struct CopyOnly {
  CopyOnly() = default;

  CopyOnly(const CopyOnly&) = default;
  CopyOnly& operator=(const CopyOnly&) = default;

  CopyOnly(CopyOnly&&) = delete;
  CopyOnly& operator=(CopyOnly&&) = delete;
};

struct MoveOnly {
  MoveOnly() = default;

  MoveOnly(const MoveOnly&) = delete;
  MoveOnly& operator=(const MoveOnly&) = delete;

  MoveOnly(MoveOnly&&) = default;
  MoveOnly& operator=(MoveOnly&&) = default;
};

struct ForwardingTestStruct {
  ForwardingTestStruct(const CopyOnly&, MoveOnly&&) {}

  std::string something_with_a_nontrivial_destructor;
};

TEST(NoDestructorTest, UncopyableUnmovable) {
  static NoDestructor<UncopyableUnmovable> default_constructed;
  EXPECT_EQ(1, default_constructed->value);

  static NoDestructor<UncopyableUnmovable> constructed_with_arg(-1);
  EXPECT_EQ(-1, constructed_with_arg->value);
}

TEST(NoDestructorTest, ForwardsArguments) {
  CopyOnly copy_only;
  MoveOnly move_only;

  static NoDestructor<ForwardingTestStruct> test_forwarding(
      copy_only, std::move(move_only));
}

TEST(NoDestructorTest, Accessors) {
  static NoDestructor<std::string> awesome("awesome");

  EXPECT_EQ("awesome", *awesome);
  EXPECT_EQ(0, awesome->compare("awesome"));
  EXPECT_EQ(0, awesome.get()->compare("awesome"));
}

// Passing initializer list to a NoDestructor like in this test
// is ambiguous in GCC.
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=84849
#if !defined(COMPILER_GCC) && !defined(__clang__)
TEST(NoDestructorTest, InitializerList) {
  static NoDestructor<std::vector<std::string>> vector({"a", "b", "c"});
}
#endif
}  // namespace

namespace {

// A class whose constructor busy-loops until it is told to complete
// construction.
class BlockingConstructor {
 public:
  BlockingConstructor() {
    EXPECT_FALSE(WasConstructorCalled());
    constructor_called_.store(true, std::memory_order_relaxed);
    EXPECT_TRUE(WasConstructorCalled());
    while (!complete_construction_.load(std::memory_order_relaxed)) {
      PlatformThread::YieldCurrentThread();
    }
    done_construction_ = true;
  }
  BlockingConstructor(const BlockingConstructor&) = delete;
  BlockingConstructor& operator=(const BlockingConstructor&) = delete;
  ~BlockingConstructor() = delete;

  // Returns true if BlockingConstructor() was entered.
  static bool WasConstructorCalled() {
    return constructor_called_.load(std::memory_order_relaxed);
  }

  // Instructs BlockingConstructor() that it may now unblock its construction.
  static void CompleteConstructionNow() {
    complete_construction_.store(true, std::memory_order_relaxed);
  }

  bool done_construction() const { return done_construction_; }

 private:
  static std::atomic<bool> constructor_called_;
  static std::atomic<bool> complete_construction_;

  bool done_construction_ = false;
};

// static
std::atomic<bool> BlockingConstructor::constructor_called_ = false;
// static
std::atomic<bool> BlockingConstructor::complete_construction_ = false;

// A SimpleThread running at |thread_type| which invokes |before_get| (optional)
// and then invokes thread-safe scoped-static-initializationconstruction on its
// NoDestructor instance.
class BlockingConstructorThread : public SimpleThread {
 public:
  BlockingConstructorThread(ThreadType thread_type, OnceClosure before_get)
      : SimpleThread("BlockingConstructorThread", Options(thread_type)),
        before_get_(std::move(before_get)) {}
  BlockingConstructorThread(const BlockingConstructorThread&) = delete;
  BlockingConstructorThread& operator=(const BlockingConstructorThread&) =
      delete;

  void Run() override {
    if (before_get_) {
      std::move(before_get_).Run();
    }

    static NoDestructor<BlockingConstructor> instance;
    EXPECT_TRUE(instance->done_construction());
  }

 private:
  OnceClosure before_get_;
};

}  // namespace

// Tests that if the thread assigned to construct the local-static
// initialization of the NoDestructor runs at background priority : the
// foreground threads will yield to it enough for it to eventually complete
// construction. While local-static thread-safe initialization isn't specific to
// NoDestructor, it is tested here as NoDestructor is set to replace
// LazyInstance and this is an important regression test for it
// (https://crbug.com/797129).
TEST(NoDestructorTest, PriorityInversionAtStaticInitializationResolves) {
  TimeTicks test_begin = TimeTicks::Now();

  // Construct BlockingConstructor from a thread that is lower priority than the
  // other threads that will be constructed. This thread used to be BACKGROUND
  // priority but that caused it to be starved by other simultaneously running
  // test processes, leading to false-positive failures.
  BlockingConstructorThread background_getter(ThreadType::kDefault,
                                              OnceClosure());
  background_getter.Start();

  while (!BlockingConstructor::WasConstructorCalled()) {
    PlatformThread::Sleep(Milliseconds(1));
  }

  // Spin 4 foreground thread per core contending to get the already under
  // construction NoDestructor. When they are all running and poking at it :
  // allow the background thread to complete its work.
  const int kNumForegroundThreads = 4 * SysInfo::NumberOfProcessors();
  std::vector<std::unique_ptr<SimpleThread>> foreground_threads;
  RepeatingClosure foreground_thread_ready_callback =
      BarrierClosure(kNumForegroundThreads,
                     BindOnce(&BlockingConstructor::CompleteConstructionNow));
  for (int i = 0; i < kNumForegroundThreads; ++i) {
    // Create threads that are higher priority than background_getter. See above
    // for why these particular priorities are chosen.
    foreground_threads.push_back(std::make_unique<BlockingConstructorThread>(
        ThreadType::kDisplayCritical, foreground_thread_ready_callback));
    foreground_threads.back()->Start();
  }

  // This test will hang if the foreground threads become stuck in
  // NoDestructor's construction per the background thread never being scheduled
  // to complete construction.
  for (auto& foreground_thread : foreground_threads) {
    foreground_thread->Join();
  }
  background_getter.Join();

  // Fail if this test takes more than 5 seconds (it takes 5-10 seconds on a
  // Z840 without https://crrev.com/527445 but is expected to be fast (~30ms)
  // with the fix).
  EXPECT_LT(TimeTicks::Now() - test_begin, Seconds(5));
}

}  // namespace base
