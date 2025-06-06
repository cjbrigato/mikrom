// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/posix/file_descriptor_shuffle.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace {

// 'Duplicated' file descriptors start at this number
const int kDuplicateBase = 1000;

}  // namespace

namespace base {

struct Action {
  enum Type {
    CLOSE,
    MOVE,
    DUPLICATE,
  };

  Action(Type in_type, int in_fd1, int in_fd2 = -1)
      : type(in_type), fd1(in_fd1), fd2(in_fd2) {}

  bool operator==(const Action& other) const {
    return other.type == type && other.fd1 == fd1 && other.fd2 == fd2;
  }

  Type type;
  int fd1;
  int fd2;
};

class InjectionTracer : public InjectionDelegate {
 public:
  InjectionTracer() : next_duplicate_(kDuplicateBase) {}

  bool Duplicate(int* result, int fd) override {
    *result = next_duplicate_++;
    actions_.emplace_back(Action::DUPLICATE, *result, fd);
    return true;
  }

  bool Move(int src, int dest) override {
    actions_.emplace_back(Action::MOVE, src, dest);
    return true;
  }

  void Close(int fd) override { actions_.emplace_back(Action::CLOSE, fd); }

  const std::vector<Action>& actions() const { return actions_; }

 private:
  int next_duplicate_;
  std::vector<Action> actions_;
};

TEST(FileDescriptorShuffleTest, Empty) {
  InjectiveMultimap map;
  InjectionTracer tracer;

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  EXPECT_EQ(0u, tracer.actions().size());
}

TEST(FileDescriptorShuffleTest, Noop) {
  InjectiveMultimap map;
  InjectionTracer tracer;
  map.emplace_back(0, 0, false);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  EXPECT_EQ(0u, tracer.actions().size());
}

TEST(FileDescriptorShuffleTest, NoopAndClose) {
  InjectiveMultimap map;
  InjectionTracer tracer;
  map.emplace_back(0, 0, true);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  EXPECT_EQ(0u, tracer.actions().size());
}

TEST(FileDescriptorShuffleTest, Simple1) {
  InjectiveMultimap map;
  InjectionTracer tracer;
  map.emplace_back(0, 1, false);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  ASSERT_EQ(1u, tracer.actions().size());
  EXPECT_TRUE(tracer.actions()[0] == Action(Action::MOVE, 0, 1));
}

TEST(FileDescriptorShuffleTest, Simple2) {
  InjectiveMultimap map;
  InjectionTracer tracer;
  map.emplace_back(0, 1, false);
  map.emplace_back(2, 3, false);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  ASSERT_EQ(2u, tracer.actions().size());
  EXPECT_TRUE(tracer.actions()[0] == Action(Action::MOVE, 0, 1));
  EXPECT_TRUE(tracer.actions()[1] == Action(Action::MOVE, 2, 3));
}

TEST(FileDescriptorShuffleTest, Simple3) {
  InjectiveMultimap map;
  InjectionTracer tracer;
  map.emplace_back(0, 1, true);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  ASSERT_EQ(2u, tracer.actions().size());
  EXPECT_TRUE(tracer.actions()[0] == Action(Action::MOVE, 0, 1));
  EXPECT_TRUE(tracer.actions()[1] == Action(Action::CLOSE, 0));
}

TEST(FileDescriptorShuffleTest, Simple4) {
  InjectiveMultimap map;
  InjectionTracer tracer;
  map.emplace_back(10, 0, true);
  map.emplace_back(1, 1, true);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  ASSERT_EQ(2u, tracer.actions().size());
  EXPECT_TRUE(tracer.actions()[0] == Action(Action::MOVE, 10, 0));
  EXPECT_TRUE(tracer.actions()[1] == Action(Action::CLOSE, 10));
}

TEST(FileDescriptorShuffleTest, Cycle) {
  InjectiveMultimap map;
  InjectionTracer tracer;
  map.emplace_back(0, 1, false);
  map.emplace_back(1, 0, false);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  ASSERT_EQ(4u, tracer.actions().size());
  EXPECT_TRUE(tracer.actions()[0] ==
              Action(Action::DUPLICATE, kDuplicateBase, 1));
  EXPECT_TRUE(tracer.actions()[1] == Action(Action::MOVE, 0, 1));
  EXPECT_TRUE(tracer.actions()[2] == Action(Action::MOVE, kDuplicateBase, 0));
  EXPECT_TRUE(tracer.actions()[3] == Action(Action::CLOSE, kDuplicateBase));
}

TEST(FileDescriptorShuffleTest, CycleAndClose1) {
  InjectiveMultimap map;
  InjectionTracer tracer;
  map.emplace_back(0, 1, true);
  map.emplace_back(1, 0, false);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  ASSERT_EQ(4u, tracer.actions().size());
  EXPECT_TRUE(tracer.actions()[0] ==
              Action(Action::DUPLICATE, kDuplicateBase, 1));
  EXPECT_TRUE(tracer.actions()[1] == Action(Action::MOVE, 0, 1));
  EXPECT_TRUE(tracer.actions()[2] == Action(Action::MOVE, kDuplicateBase, 0));
  EXPECT_TRUE(tracer.actions()[3] == Action(Action::CLOSE, kDuplicateBase));
}

TEST(FileDescriptorShuffleTest, CycleAndClose2) {
  InjectiveMultimap map;
  InjectionTracer tracer;
  map.emplace_back(0, 1, false);
  map.emplace_back(1, 0, true);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  ASSERT_EQ(4u, tracer.actions().size());
  EXPECT_TRUE(tracer.actions()[0] ==
              Action(Action::DUPLICATE, kDuplicateBase, 1));
  EXPECT_TRUE(tracer.actions()[1] == Action(Action::MOVE, 0, 1));
  EXPECT_TRUE(tracer.actions()[2] == Action(Action::MOVE, kDuplicateBase, 0));
  EXPECT_TRUE(tracer.actions()[3] == Action(Action::CLOSE, kDuplicateBase));
}

TEST(FileDescriptorShuffleTest, CycleAndClose3) {
  InjectiveMultimap map;
  InjectionTracer tracer;
  map.emplace_back(0, 1, true);
  map.emplace_back(1, 0, true);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  ASSERT_EQ(4u, tracer.actions().size());
  EXPECT_TRUE(tracer.actions()[0] ==
              Action(Action::DUPLICATE, kDuplicateBase, 1));
  EXPECT_TRUE(tracer.actions()[1] == Action(Action::MOVE, 0, 1));
  EXPECT_TRUE(tracer.actions()[2] == Action(Action::MOVE, kDuplicateBase, 0));
  EXPECT_TRUE(tracer.actions()[3] == Action(Action::CLOSE, kDuplicateBase));
}

TEST(FileDescriptorShuffleTest, Fanout) {
  InjectiveMultimap map;
  InjectionTracer tracer;
  map.emplace_back(0, 1, false);
  map.emplace_back(0, 2, false);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  ASSERT_EQ(2u, tracer.actions().size());
  EXPECT_TRUE(tracer.actions()[0] == Action(Action::MOVE, 0, 1));
  EXPECT_TRUE(tracer.actions()[1] == Action(Action::MOVE, 0, 2));
}

TEST(FileDescriptorShuffleTest, FanoutAndClose1) {
  InjectiveMultimap map;
  InjectionTracer tracer;
  map.emplace_back(0, 1, true);
  map.emplace_back(0, 2, false);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  ASSERT_EQ(3u, tracer.actions().size());
  EXPECT_TRUE(tracer.actions()[0] == Action(Action::MOVE, 0, 1));
  EXPECT_TRUE(tracer.actions()[1] == Action(Action::MOVE, 0, 2));
  EXPECT_TRUE(tracer.actions()[2] == Action(Action::CLOSE, 0));
}

TEST(FileDescriptorShuffleTest, FanoutAndClose2) {
  InjectiveMultimap map;
  InjectionTracer tracer;
  map.emplace_back(0, 1, false);
  map.emplace_back(0, 2, true);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  ASSERT_EQ(3u, tracer.actions().size());
  EXPECT_TRUE(tracer.actions()[0] == Action(Action::MOVE, 0, 1));
  EXPECT_TRUE(tracer.actions()[1] == Action(Action::MOVE, 0, 2));
  EXPECT_TRUE(tracer.actions()[2] == Action(Action::CLOSE, 0));
}

TEST(FileDescriptorShuffleTest, FanoutAndClose3) {
  InjectiveMultimap map;
  InjectionTracer tracer;
  map.emplace_back(0, 1, true);
  map.emplace_back(0, 2, true);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  ASSERT_EQ(3u, tracer.actions().size());
  EXPECT_TRUE(tracer.actions()[0] == Action(Action::MOVE, 0, 1));
  EXPECT_TRUE(tracer.actions()[1] == Action(Action::MOVE, 0, 2));
  EXPECT_TRUE(tracer.actions()[2] == Action(Action::CLOSE, 0));
}

TEST(FileDescriptorShuffleTest, DuplicateClash) {
  InjectiveMultimap map;
  InjectionTracer tracer;
  map.emplace_back(0, 1, false);
  // Duplicating 1 puts the fd in the spot it's supposed to go already.
  map.emplace_back(1, kDuplicateBase, false);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  ASSERT_EQ(2u, tracer.actions().size());
  // This duplication "accidentally" fulfills the second mapping.
  EXPECT_TRUE(tracer.actions()[0] ==
              Action(Action::DUPLICATE, kDuplicateBase, 1));
  EXPECT_TRUE(tracer.actions()[1] == Action(Action::MOVE, 0, 1));
  // There should be no extra MOVE or CLOSE as the mapping is done and there are
  // no superfluous fds left around.
}

TEST(FileDescriptorShuffleTest, DuplicateClashBad) {
  InjectiveMultimap map;
  InjectionTracer tracer;
  map.emplace_back(0, 2, false);
  map.emplace_back(1, kDuplicateBase, false);
  map.emplace_back(2, 3, false);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  ASSERT_EQ(6u, tracer.actions().size());
  // Clashes with the second mapping.
  EXPECT_TRUE(tracer.actions()[0] ==
              Action(Action::DUPLICATE, kDuplicateBase, 2));
  EXPECT_TRUE(tracer.actions()[1] == Action(Action::MOVE, 0, 2));
  EXPECT_TRUE(tracer.actions()[2] ==
              Action(Action::DUPLICATE, kDuplicateBase + 1, kDuplicateBase));
  EXPECT_TRUE(tracer.actions()[3] == Action(Action::MOVE, 1, kDuplicateBase));
  EXPECT_TRUE(tracer.actions()[4] ==
              Action(Action::MOVE, kDuplicateBase + 1, 3));
  EXPECT_TRUE(tracer.actions()[5] == Action(Action::CLOSE, kDuplicateBase + 1));
}

TEST(FileDescriptorShuffleTest, DuplicateClashBadWithClose) {
  InjectiveMultimap map;
  InjectionTracer tracer;
  map.emplace_back(0, 2, true);
  map.emplace_back(1, kDuplicateBase, true);
  map.emplace_back(2, 3, true);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &tracer));
  ASSERT_EQ(8u, tracer.actions().size());
  // Clashes with the second mapping.
  EXPECT_TRUE(tracer.actions()[0] ==
              Action(Action::DUPLICATE, kDuplicateBase, 2));
  EXPECT_TRUE(tracer.actions()[1] == Action(Action::MOVE, 0, 2));
  EXPECT_TRUE(tracer.actions()[2] == Action(Action::CLOSE, 0));
  EXPECT_TRUE(tracer.actions()[3] ==
              Action(Action::DUPLICATE, kDuplicateBase + 1, kDuplicateBase));
  EXPECT_TRUE(tracer.actions()[4] == Action(Action::MOVE, 1, kDuplicateBase));
  EXPECT_TRUE(tracer.actions()[5] == Action(Action::CLOSE, 1));
  EXPECT_TRUE(tracer.actions()[6] ==
              Action(Action::MOVE, kDuplicateBase + 1, 3));
  EXPECT_TRUE(tracer.actions()[7] == Action(Action::CLOSE, kDuplicateBase + 1));
}

class FailingDelegate : public InjectionDelegate {
 public:
  bool Duplicate(int* result, int fd) override { return false; }

  bool Move(int src, int dest) override { return false; }

  void Close(int fd) override {}
};

TEST(FileDescriptorShuffleTest, EmptyWithFailure) {
  InjectiveMultimap map;
  FailingDelegate failing;

  EXPECT_TRUE(PerformInjectiveMultimap(map, &failing));
}

TEST(FileDescriptorShuffleTest, NoopWithFailure) {
  InjectiveMultimap map;
  FailingDelegate failing;
  map.emplace_back(0, 0, false);

  EXPECT_TRUE(PerformInjectiveMultimap(map, &failing));
}

TEST(FileDescriptorShuffleTest, Simple1WithFailure) {
  InjectiveMultimap map;
  FailingDelegate failing;
  map.emplace_back(0, 1, false);

  EXPECT_FALSE(PerformInjectiveMultimap(map, &failing));
}

}  // namespace base
