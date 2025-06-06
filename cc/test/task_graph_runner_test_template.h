// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TEST_TASK_GRAPH_RUNNER_TEST_TEMPLATE_H_
#define CC_TEST_TASK_GRAPH_RUNNER_TEST_TEMPLATE_H_

#include <array>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/synchronization/lock.h"
#include "base/threading/simple_thread.h"
#include "cc/raster/task_category.h"
#include "cc/raster/task_graph_runner.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cc {

class TaskGraphRunnerTestBase {
 public:
  struct TaskInfo {
    TaskInfo(int namespace_index,
             unsigned id,
             unsigned dependent_id,
             unsigned dependent_count,
             unsigned category,
             unsigned priority,
             bool has_external_dependency)
        : namespace_index(namespace_index),
          id(id),
          dependent_id(dependent_id),
          dependent_count(dependent_count),
          category(category),
          priority(priority),
          has_external_dependency(has_external_dependency) {}

    int namespace_index;
    unsigned id;
    unsigned dependent_id;
    unsigned dependent_count;
    unsigned category;
    unsigned priority;
    bool has_external_dependency;
  };

  TaskGraphRunnerTestBase();
  ~TaskGraphRunnerTestBase();

  void SetTaskGraphRunner(TaskGraphRunner* task_graph_runner);
  void ResetIds(int namespace_index);
  void RunAllTasks(int namespace_index);
  void RunUntilIdle();
  void RunTaskOnWorkerThread(int namespace_index, unsigned id);
  void OnTaskCompleted(int namespace_index, unsigned id);
  const std::vector<unsigned>& run_task_ids(int namespace_index);
  const std::vector<unsigned>& on_task_completed_ids(int namespace_index);
  void ScheduleTasks(int namespace_index, const std::vector<TaskInfo>& tasks);
  void ExternalDependencyCompletedForTask(int namespace_index, int task_index);

  static constexpr int kNamespaceCount = 3;

 protected:
  class FakeTaskImpl : public Task {
   public:
    FakeTaskImpl(TaskGraphRunnerTestBase* test, int namespace_index, int id)
        : test_(test), namespace_index_(namespace_index), id_(id) {}
    FakeTaskImpl(const FakeTaskImpl&) = delete;

    FakeTaskImpl& operator=(const FakeTaskImpl&) = delete;

    // Overridden from Task:
    void RunOnWorkerThread() override;

    virtual void OnTaskCompleted();

   protected:
    ~FakeTaskImpl() override {}

   private:
    raw_ptr<TaskGraphRunnerTestBase> test_;
    int namespace_index_;
    int id_;
  };

  class FakeDependentTaskImpl : public FakeTaskImpl {
   public:
    FakeDependentTaskImpl(TaskGraphRunnerTestBase* test,
                          int namespace_index,
                          int id)
        : FakeTaskImpl(test, namespace_index, id) {}
    FakeDependentTaskImpl(const FakeDependentTaskImpl&) = delete;

    FakeDependentTaskImpl& operator=(const FakeDependentTaskImpl&) = delete;

    // Overridden from FakeTaskImpl:
    void OnTaskCompleted() override {}

   private:
    ~FakeDependentTaskImpl() override {}
  };

  raw_ptr<TaskGraphRunner> task_graph_runner_ = nullptr;
  std::array<NamespaceToken, kNamespaceCount> namespace_token_;
  std::array<Task::Vector, kNamespaceCount> tasks_;
  std::array<Task::Vector, kNamespaceCount> dependents_;
  std::array<std::vector<unsigned int>, kNamespaceCount> run_task_ids_;
  base::Lock run_task_ids_lock_;
  std::array<std::vector<unsigned int>, kNamespaceCount> on_task_completed_ids_;
};

template <typename TaskRunnerTestDelegate>
class TaskGraphRunnerTest : public TaskGraphRunnerTestBase,
                            public testing::Test {
 public:
  // Overridden from testing::Test:
  void SetUp() override {
    delegate_.StartTaskGraphRunner();
    SetTaskGraphRunner(delegate_.GetTaskGraphRunner());

    for (NamespaceToken& namespace_token : namespace_token_) {
      namespace_token = task_graph_runner_->GenerateNamespaceToken();
    }
  }
  void TearDown() override {
    delegate_.StopTaskGraphRunner();
    SetTaskGraphRunner(nullptr);
  }

 private:
  TaskRunnerTestDelegate delegate_;
};

TYPED_TEST_SUITE_P(TaskGraphRunnerTest);

TYPED_TEST_P(TaskGraphRunnerTest, Basic) {
  const int kNamespaceCount = TaskGraphRunnerTestBase::kNamespaceCount;
  using TaskInfo = TaskGraphRunnerTestBase::TaskInfo;

  for (int i = 0; i < kNamespaceCount; ++i) {
    EXPECT_EQ(0u, this->run_task_ids(i).size());
    EXPECT_EQ(0u, this->on_task_completed_ids(i).size());

    this->ScheduleTasks(
        i, std::vector<TaskInfo>(1, TaskInfo(i, 0u, 0u, 0u, 0u, 0u, false)));
  }

  for (int i = 0; i < kNamespaceCount; ++i) {
    this->RunAllTasks(i);

    EXPECT_EQ(1u, this->run_task_ids(i).size());
    EXPECT_EQ(1u, this->on_task_completed_ids(i).size());
  }

  for (int i = 0; i < kNamespaceCount; ++i)
    this->ScheduleTasks(
        i, std::vector<TaskInfo>(1, TaskInfo(i, 0u, 0u, 1u, 0u, 0u, false)));

  for (int i = 0; i < kNamespaceCount; ++i) {
    this->RunAllTasks(i);

    EXPECT_EQ(3u, this->run_task_ids(i).size());
    EXPECT_EQ(2u, this->on_task_completed_ids(i).size());
  }

  for (int i = 0; i < kNamespaceCount; ++i)
    this->ScheduleTasks(
        i, std::vector<TaskInfo>(1, TaskInfo(i, 0u, 0u, 2u, 0u, 0u, false)));

  for (int i = 0; i < kNamespaceCount; ++i) {
    this->RunAllTasks(i);

    EXPECT_EQ(6u, this->run_task_ids(i).size());
    EXPECT_EQ(3u, this->on_task_completed_ids(i).size());
  }
}

TYPED_TEST_P(TaskGraphRunnerTest, Dependencies) {
  const int kNamespaceCount = TaskGraphRunnerTestBase::kNamespaceCount;
  using TaskInfo = TaskGraphRunnerTestBase::TaskInfo;

  for (int i = 0; i < kNamespaceCount; ++i) {
    this->ScheduleTasks(i, std::vector<TaskInfo>(1, TaskInfo(i, 0u, 1u,
                                                             1u,  // 1 dependent
                                                             0u, 0u, false)));
  }

  for (int i = 0; i < kNamespaceCount; ++i) {
    this->RunAllTasks(i);

    // Check if task ran before dependent.
    ASSERT_EQ(2u, this->run_task_ids(i).size());
    EXPECT_EQ(0u, this->run_task_ids(i)[0]);
    EXPECT_EQ(1u, this->run_task_ids(i)[1]);
    ASSERT_EQ(1u, this->on_task_completed_ids(i).size());
    EXPECT_EQ(0u, this->on_task_completed_ids(i)[0]);
  }

  for (int i = 0; i < kNamespaceCount; ++i) {
    this->ScheduleTasks(i,
                        std::vector<TaskInfo>(1, TaskInfo(i, 2u, 3u,
                                                          2u,  // 2 dependents
                                                          0u, 0u, false)));
  }

  for (int i = 0; i < kNamespaceCount; ++i) {
    this->RunAllTasks(i);

    // Task should only run once.
    ASSERT_EQ(5u, this->run_task_ids(i).size());
    EXPECT_EQ(2u, this->run_task_ids(i)[2]);
    EXPECT_EQ(3u, this->run_task_ids(i)[3]);
    EXPECT_EQ(3u, this->run_task_ids(i)[4]);
    ASSERT_EQ(2u, this->on_task_completed_ids(i).size());
    EXPECT_EQ(2u, this->on_task_completed_ids(i)[1]);
  }
}

TYPED_TEST_P(TaskGraphRunnerTest, ExternalDependency) {
  using TaskInfo = TaskGraphRunnerTestBase::TaskInfo;

  // Set up one task with an external dependent and one task witthout.
  std::vector<TaskInfo> tasks;
  tasks.emplace_back(0u, 0u, 1u,
                     1u,  // 1 dependent
                     0u, 0u, true /*has_external_dependency*/);
  tasks.emplace_back(0u, 2u, 3u,
                     1u,  // 1 dependent
                     0u, 0u, false /*has_external_dependency*/);
  this->ScheduleTasks(0, tasks);

  this->RunUntilIdle();

  // Only the tasks without an external dependency should have run.
  ASSERT_EQ(2u, this->run_task_ids(0).size());
  EXPECT_EQ(2u, this->run_task_ids(0)[0]);
  EXPECT_EQ(3u, this->run_task_ids(0)[1]);
  ASSERT_EQ(1u, this->on_task_completed_ids(0).size());
  EXPECT_EQ(2u, this->on_task_completed_ids(0)[0]);

  this->ExternalDependencyCompletedForTask(0, 0);

  this->RunAllTasks(0u);
  ASSERT_EQ(4u, this->run_task_ids(0).size());
  EXPECT_EQ(0u, this->run_task_ids(0)[2]);
  EXPECT_EQ(1u, this->run_task_ids(0)[3]);
  ASSERT_EQ(2u, this->on_task_completed_ids(0).size());
  EXPECT_EQ(0u, this->on_task_completed_ids(0)[1]);
}

TYPED_TEST_P(TaskGraphRunnerTest, Categories) {
  const int kNamespaceCount = TaskGraphRunnerTestBase::kNamespaceCount;
  const unsigned kCategoryCount = LAST_TASK_CATEGORY + 1;
  using TaskInfo = TaskGraphRunnerTestBase::TaskInfo;

  for (int i = 0; i < kNamespaceCount; ++i) {
    EXPECT_EQ(0u, this->run_task_ids(i).size());
    EXPECT_EQ(0u, this->on_task_completed_ids(i).size());
    std::vector<TaskInfo> tasks;
    for (unsigned j = 0; j < kCategoryCount; ++j) {
      tasks.emplace_back(i, 0u, 0u, 0u, j, 0u, false);
    }
    this->ScheduleTasks(i, tasks);
  }

  for (int i = 0; i < kNamespaceCount; ++i) {
    this->RunAllTasks(i);

    EXPECT_EQ(kCategoryCount, this->run_task_ids(i).size());
    EXPECT_EQ(kCategoryCount, this->on_task_completed_ids(i).size());
  }

  for (int i = 0; i < kNamespaceCount; ++i) {
    std::vector<TaskInfo> tasks;
    for (unsigned j = 0; j < kCategoryCount; ++j) {
      tasks.emplace_back(i, 0u, 0u, 1u, j, 0u, false);
    }
    this->ScheduleTasks(i, tasks);
  }

  for (int i = 0; i < kNamespaceCount; ++i) {
    this->RunAllTasks(i);

    EXPECT_EQ(kCategoryCount * 3u, this->run_task_ids(i).size());
    EXPECT_EQ(kCategoryCount * 2u, this->on_task_completed_ids(i).size());
  }

  for (int i = 0; i < kNamespaceCount; ++i) {
    std::vector<TaskInfo> tasks;
    for (unsigned j = 0; j < kCategoryCount; ++j) {
      tasks.emplace_back(i, 0u, 0u, 2u, j, 0u, false);
    }
    this->ScheduleTasks(i, tasks);
  }

  for (int i = 0; i < kNamespaceCount; ++i) {
    this->RunAllTasks(i);

    EXPECT_EQ(kCategoryCount * 6u, this->run_task_ids(i).size());
    EXPECT_EQ(kCategoryCount * 3u, this->on_task_completed_ids(i).size());
  }
}

REGISTER_TYPED_TEST_SUITE_P(TaskGraphRunnerTest,
                            Basic,
                            Dependencies,
                            ExternalDependency,
                            Categories);

template <typename TaskRunnerTestDelegate>
using SingleThreadTaskGraphRunnerTest =
    TaskGraphRunnerTest<TaskRunnerTestDelegate>;

TYPED_TEST_SUITE_P(SingleThreadTaskGraphRunnerTest);

TYPED_TEST_P(SingleThreadTaskGraphRunnerTest, Priority) {
  const int kNamespaceCount = TaskGraphRunnerTestBase::kNamespaceCount;
  using TaskInfo = TaskGraphRunnerTestBase::TaskInfo;

  for (int i = 0; i < kNamespaceCount; ++i) {
    TaskInfo tasks[] = {
        TaskInfo(i, 0u, 2u, 1u, 0u, 1u, false),  // Priority 1
        TaskInfo(i, 1u, 3u, 1u, 0u, 0u, false)   // Priority 0
    };
    this->ScheduleTasks(
        i, std::vector<TaskInfo>(std::begin(tasks), std::end(tasks)));
  }

  for (int i = 0; i < kNamespaceCount; ++i) {
    this->RunAllTasks(i);

    // Check if tasks ran in order of priority.
    ASSERT_EQ(4u, this->run_task_ids(i).size());
    EXPECT_EQ(1u, this->run_task_ids(i)[0]);
    EXPECT_EQ(3u, this->run_task_ids(i)[1]);
    EXPECT_EQ(0u, this->run_task_ids(i)[2]);
    EXPECT_EQ(2u, this->run_task_ids(i)[3]);
    ASSERT_EQ(2u, this->on_task_completed_ids(i).size());
    EXPECT_EQ(1u, this->on_task_completed_ids(i)[0]);
    EXPECT_EQ(0u, this->on_task_completed_ids(i)[1]);
  }
}

REGISTER_TYPED_TEST_SUITE_P(SingleThreadTaskGraphRunnerTest, Priority);

}  // namespace cc

#endif  // CC_TEST_TASK_GRAPH_RUNNER_TEST_TEMPLATE_H_
