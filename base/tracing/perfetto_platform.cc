// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/tracing/perfetto_platform.h"

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/trace_event/trace_event.h"
#include "base/tracing/perfetto_task_runner.h"
#include "base/tracing_buildflags.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_ANDROID)
#include "base/android/apk_info.h"
#endif  // BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_NACL)
#include "third_party/perfetto/include/perfetto/ext/base/thread_task_runner.h"
#endif

namespace base::tracing {

PerfettoPlatform::PerfettoPlatform(
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    Options options)
    : process_name_prefix_(std::move(options.process_name_prefix)),
      defer_delayed_tasks_(options.defer_delayed_tasks),
      task_runner_(std::move(task_runner)),
      thread_local_object_([](void* object) {
        delete static_cast<ThreadLocalObject*>(object);
      }) {}

PerfettoPlatform::~PerfettoPlatform() = default;

PerfettoPlatform::ThreadLocalObject*
PerfettoPlatform::GetOrCreateThreadLocalObject() {
  auto* object = static_cast<ThreadLocalObject*>(thread_local_object_.Get());
  if (!object) {
    object = ThreadLocalObject::CreateInstance().release();
    thread_local_object_.Set(object);
  }
  return object;
}

std::unique_ptr<perfetto::base::TaskRunner> PerfettoPlatform::CreateTaskRunner(
    const CreateTaskRunnerArgs&) {
  // TODO(b/242965112): Add support for the builtin task runner
  DCHECK(!perfetto_task_runner_);
  auto perfetto_task_runner =
      std::make_unique<PerfettoTaskRunner>(task_runner_, defer_delayed_tasks_);
  perfetto_task_runner_ = perfetto_task_runner->GetWeakPtr();
  return perfetto_task_runner;
}

void PerfettoPlatform::ResetTaskRunner(
    scoped_refptr<base::SequencedTaskRunner> task_runner) {
  task_runner_ = task_runner;
  if (perfetto_task_runner_) {
    perfetto_task_runner_->ResetTaskRunner(task_runner);
  }
}

// This method is used by the SDK to determine the producer name.
// Note that we override the producer name for the mojo backend in ProducerHost,
// and thus this only affects the producer name for the system backend.
std::string PerfettoPlatform::GetCurrentProcessName() {
#if BUILDFLAG(IS_ANDROID)
  const std::string& host_package_name = android::apk_info::host_package_name();
#else
  std::string host_package_name;
#endif  // BUILDFLAG(IS_ANDROID)

  // On Android we want to include if this is webview inside of an app or
  // Android Chrome. To aid this we add the host_package_name to differentiate
  // the various apps and sources.
  std::string process_name;
  if (!host_package_name.empty()) {
    process_name = StrCat(
        {process_name_prefix_, host_package_name, "-",
         NumberToString(trace_event::TraceLog::GetInstance()->process_id())});
  } else {
    process_name = StrCat(
        {process_name_prefix_,
         NumberToString(trace_event::TraceLog::GetInstance()->process_id())});
  }
  return process_name;
}

perfetto::base::PlatformThreadId PerfettoPlatform::GetCurrentThreadId() {
  return base::strict_cast<perfetto::base::PlatformThreadId>(
      base::PlatformThread::CurrentId().raw());
}

}  // namespace base::tracing
