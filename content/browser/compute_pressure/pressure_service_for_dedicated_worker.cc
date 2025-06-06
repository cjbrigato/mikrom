// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/compute_pressure/pressure_service_for_dedicated_worker.h"

#include "base/system/sys_info.h"
#include "content/browser/compute_pressure/web_contents_pressure_manager_proxy.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/worker_host/dedicated_worker_host.h"
#include "content/public/browser/browser_child_process_host.h"
#include "content/public/browser/browser_thread.h"

namespace content {

PressureServiceForDedicatedWorker::PressureServiceForDedicatedWorker(
    DedicatedWorkerHost* host)
    : worker_host_(host),
      metrics_(
#if BUILDFLAG(IS_MAC)
          base::ProcessMetrics::CreateProcessMetrics(
              host->GetProcessHost()->GetProcess().Handle(),
              BrowserChildProcessHost::GetPortProvider())
#else
          base::ProcessMetrics::CreateProcessMetrics(
              host->GetProcessHost()->GetProcess().Handle())
#endif  // BUILDFLAG(IS_MAC)
      ) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
}

PressureServiceForDedicatedWorker::~PressureServiceForDedicatedWorker() =
    default;

bool PressureServiceForDedicatedWorker::ShouldDeliverUpdate() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // https://www.w3.org/TR/compute-pressure/#dfn-owning-document-set
  // https://www.w3.org/TR/compute-pressure/#dfn-may-receive-data
  auto* rfh =
      RenderFrameHostImpl::FromID(worker_host_->GetAncestorRenderFrameHostId());
  return HasImplicitFocus(rfh);
}

std::optional<base::UnguessableToken>
PressureServiceForDedicatedWorker::GetTokenFor(
    device::mojom::PressureSource source) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const auto* web_contents =
      WebContents::FromRenderFrameHost(RenderFrameHostImpl::FromID(
          worker_host_->GetAncestorRenderFrameHostId()));
  if (const auto* pressure_manager_proxy =
          WebContentsPressureManagerProxy::FromWebContents(web_contents)) {
    return pressure_manager_proxy->GetTokenFor(source);
  }
  return std::nullopt;
}

RenderFrameHost* PressureServiceForDedicatedWorker::GetRenderFrameHost() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return RenderFrameHostImpl::FromID(
      worker_host_->GetAncestorRenderFrameHostId());
}

double PressureServiceForDedicatedWorker::CalculateOwnContributionEstimate(
    double global_cpu_utilization) {
  double process_pressure =
      metrics_->GetPlatformIndependentCPUUsage().value_or(-1.0) /
      static_cast<double>(base::SysInfo::NumberOfProcessors());

  return process_pressure / (global_cpu_utilization * 100);
}

}  // namespace content
