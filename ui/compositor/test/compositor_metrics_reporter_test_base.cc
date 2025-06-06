// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/compositor/test/compositor_metrics_reporter_test_base.h"

#include <memory>

#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/compositor/test/test_context_factories.h"
#include "ui/gfx/geometry/rect.h"

namespace ui {

CompositorMetricsReporterTestBase::CompositorMetricsReporterTestBase() =
    default;
CompositorMetricsReporterTestBase::~CompositorMetricsReporterTestBase() =
    default;

void CompositorMetricsReporterTestBase::SetUp() {
  context_factories_ = std::make_unique<TestContextFactories>(false);

  const gfx::Rect bounds(100, 100);
  host_.reset(TestCompositorHost::Create(
      bounds, context_factories_->GetContextFactory()));
  host_->Show();

  compositor()->SetRootLayer(&root_);

  frame_generation_timer_.Start(
      FROM_HERE, base::Milliseconds(16), this,
      &CompositorMetricsReporterTestBase::GenerateOneFrame);
}

void CompositorMetricsReporterTestBase::TearDown() {
  frame_generation_timer_.Stop();
  host_.reset();
  context_factories_.reset();
}

void CompositorMetricsReporterTestBase::Advance(const base::TimeDelta& delta) {
  run_loop_ = std::make_unique<base::RunLoop>();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, run_loop_->QuitClosure(), delta);
  run_loop_->Run();
}

void CompositorMetricsReporterTestBase::QuitRunLoop() {
  run_loop_->Quit();
}

}  // namespace ui
