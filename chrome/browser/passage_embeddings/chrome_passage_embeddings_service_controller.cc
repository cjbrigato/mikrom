// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/passage_embeddings/chrome_passage_embeddings_service_controller.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/notreached.h"
#include "base/process/process.h"
#include "chrome/browser/passage_embeddings/cpu_histogram_logger.h"
#include "components/passage_embeddings/passage_embeddings_features.h"
#include "content/public/browser/browser_child_process_host.h"
#include "content/public/browser/browser_child_process_host_iterator.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/service_process_host.h"
#include "content/public/common/process_type.h"

namespace passage_embeddings {

// static
ChromePassageEmbeddingsServiceController*
ChromePassageEmbeddingsServiceController::Get() {
  static base::NoDestructor<ChromePassageEmbeddingsServiceController> instance;
  return instance.get();
}

ChromePassageEmbeddingsServiceController::
    ChromePassageEmbeddingsServiceController() = default;

ChromePassageEmbeddingsServiceController::
    ~ChromePassageEmbeddingsServiceController() = default;

void ChromePassageEmbeddingsServiceController::MaybeLaunchService() {
  // No-op if already launched.
  if (service_remote_) {
    return;
  }

  auto receiver = service_remote_.BindNewPipeAndPassReceiver();
  // Unretained is safe because `this` owns `service_remote_`, which
  // synchronously calls the disconnect and idle handlers.
  service_remote_.set_disconnect_handler(base::BindOnce(
      &ChromePassageEmbeddingsServiceController::ResetServiceRemote,
      base::Unretained(this)));
  service_remote_.set_idle_handler(
      kEmbeddingsServiceTimeout.Get(),
      base::BindRepeating(
          &ChromePassageEmbeddingsServiceController::ResetServiceRemote,
          base::Unretained(this)));
  content::ServiceProcessHost::Launch<mojom::PassageEmbeddingsService>(
      std::move(receiver),
      content::ServiceProcessHost::Options()
          .WithDisplayName("Passage Embeddings Service")
          .WithProcessCallback(base::BindOnce(
              &ChromePassageEmbeddingsServiceController::OnServiceLaunched,
              weak_ptr_factory_.GetWeakPtr()))
          .Pass());
}

void ChromePassageEmbeddingsServiceController::ResetServiceRemote() {
  ResetEmbedderRemote();
  service_remote_.reset();
  cpu_logger_.StopLoggingAfterNextUpdate();
}

void ChromePassageEmbeddingsServiceController::OnServiceLaunched(
    const base::Process& process) {
  // `OnServiceLaunched` is triggered by the same observable that
  // `PerformanceManager` uses to register new process hosts, which is necessary
  // before we can start the CPU histogram logger. As such, this has to be a
  // `PostTask` to ensure that `InitializeCpuLogger` is invoked after the
  // service process host is registered.
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(
          &ChromePassageEmbeddingsServiceController::InitializeCpuLogger,
          weak_ptr_factory_.GetWeakPtr()));
}

void ChromePassageEmbeddingsServiceController::InitializeCpuLogger() {
  content::BrowserChildProcessHostIterator iter(content::PROCESS_TYPE_UTILITY);
  while (!iter.Done()) {
    const content::ChildProcessData& data = iter.GetData();
    if (data.name == u"Passage Embeddings Service") {
      cpu_logger_.StartLogging(
          content::BrowserChildProcessHost::FromID(data.id),
          base::BindRepeating(
              &PassageEmbeddingsServiceController::EmbedderRunning,
              base::Unretained(this)));
      return;
    }
    ++iter;
  }
}

}  // namespace passage_embeddings
