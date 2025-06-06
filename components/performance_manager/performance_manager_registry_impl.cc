// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/performance_manager/performance_manager_registry_impl.h"

#include <utility>

#include "base/observer_list.h"
#include "components/performance_manager/embedder/binders.h"
#include "components/performance_manager/graph/page_node_impl.h"
#include "components/performance_manager/graph/worker_node_impl.h"
#include "components/performance_manager/performance_manager_tab_helper.h"
#include "components/performance_manager/public/performance_manager.h"
#include "components/performance_manager/public/performance_manager_observer.h"
#include "components/performance_manager/render_process_user_data.h"
#include "components/performance_manager/service_worker_context_adapter.h"
#include "components/performance_manager/worker_watcher.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"

namespace performance_manager {

namespace {

PerformanceManagerRegistryImpl* g_instance = nullptr;

}  // namespace

PerformanceManagerRegistryImpl::PerformanceManagerRegistryImpl() {
  DCHECK(!g_instance);
  g_instance = this;

  // The registry should be created after the PerformanceManager.
  DCHECK(PerformanceManager::IsAvailable());

  browser_child_process_watcher_.Initialize();
}

PerformanceManagerRegistryImpl::~PerformanceManagerRegistryImpl() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // TearDown() should have been invoked to reset |g_instance| and clear
  // |web_contents_| and |render_process_user_data_| prior to destroying the
  // registry.
  DCHECK(!g_instance);
  DCHECK(web_contents_.empty());
  DCHECK(render_process_hosts_.empty());
  // TODO(crbug.com/40131811): |observers_| should also be empty by now!
}

// static
PerformanceManagerRegistryImpl* PerformanceManagerRegistryImpl::GetInstance() {
  return g_instance;
}

void PerformanceManagerRegistryImpl::AddObserver(
    PerformanceManagerObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void PerformanceManagerRegistryImpl::RemoveObserver(
    PerformanceManagerObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

Binders& PerformanceManagerRegistryImpl::GetBinders() {
  return binders_;
}

void PerformanceManagerRegistryImpl::CreatePageNodeForWebContents(
    content::WebContents* web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto result = web_contents_.insert(web_contents);
  DCHECK(result.second);

  PerformanceManagerTabHelper::CreateForWebContents(web_contents);
  PerformanceManagerTabHelper* tab_helper =
      PerformanceManagerTabHelper::FromWebContents(web_contents);
  DCHECK(tab_helper);
  tab_helper->SetDestructionObserver(this);

  for (auto& observer : observers_) {
    observer.OnPageNodeCreatedForWebContents(web_contents);
  }
}

void PerformanceManagerRegistryImpl::SetPageType(
    content::WebContents* web_contents,
    PageType type) {
  PerformanceManagerTabHelper* tab_helper =
      PerformanceManagerTabHelper::FromWebContents(web_contents);
  DCHECK(tab_helper);
  tab_helper->primary_page_node()->SetType(type);
}

void PerformanceManagerRegistryImpl::NotifyBrowserContextAdded(
    content::BrowserContext* browser_context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  content::StoragePartition* storage_partition =
      browser_context->GetDefaultStoragePartition();

  // Create an adapter for the service worker context.
  auto insertion_result = service_worker_context_adapters_.emplace(
      browser_context, std::make_unique<ServiceWorkerContextAdapterImpl>(
                           storage_partition->GetServiceWorkerContext()));
  DCHECK(insertion_result.second);
  ServiceWorkerContextAdapter* service_worker_context_adapter =
      insertion_result.first->second.get();

  auto worker_watcher = std::make_unique<WorkerWatcher>(
      browser_context->UniqueId(),
      storage_partition->GetDedicatedWorkerService(),
      storage_partition->GetSharedWorkerService(),
      service_worker_context_adapter, &frame_node_source_);
  bool inserted =
      worker_watchers_.emplace(browser_context, std::move(worker_watcher))
          .second;
  DCHECK(inserted);
}

void PerformanceManagerRegistryImpl::CreateProcessNode(
    content::RenderProcessHost* render_process_host) {
  // Ideally this would strictly be a "Create", but when a
  // RenderFrameHost is "resurrected" with a new process it will
  // already have user data attached. This will happen on renderer
  // crash.
  EnsureProcessNodeForRenderProcessHost(render_process_host);
}

void PerformanceManagerRegistryImpl::NotifyBrowserContextRemoved(
    content::BrowserContext* browser_context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = worker_watchers_.find(browser_context);
  CHECK(it != worker_watchers_.end());
  it->second->TearDown();
  worker_watchers_.erase(it);

  // Remove the adapter.
  size_t removed = service_worker_context_adapters_.erase(browser_context);
  DCHECK_EQ(removed, 1u);
}

void PerformanceManagerRegistryImpl::TearDown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // The registry should be torn down before the PerformanceManager.
  DCHECK(PerformanceManager::IsAvailable());

  // Notify any observers of the tear down. This lets them unregister things,
  // etc.
  for (auto& observer : observers_) {
    observer.OnBeforePerformanceManagerDestroyed();
  }

  DCHECK_EQ(g_instance, this);
  g_instance = nullptr;

  // Destroy WorkerNodes before ProcessNodes, because ProcessNode checks that it
  // has no associated WorkerNode when torn down.
  for (auto& kv : worker_watchers_) {
    kv.second->TearDown();
  }
  worker_watchers_.clear();

  service_worker_context_adapters_.clear();

  for (content::WebContents* web_contents : web_contents_) {
    PerformanceManagerTabHelper* tab_helper =
        PerformanceManagerTabHelper::FromWebContents(web_contents);
    DCHECK(tab_helper);
    // Clear the destruction observer to avoid a notification that will modify
    // `web_contents_` while iterating.
    tab_helper->SetDestructionObserver(nullptr);
    tab_helper->TearDownAndSelfDelete();
  }
  web_contents_.clear();

  for (content::RenderProcessHost* render_process_host :
       render_process_hosts_) {
    RenderProcessUserData* user_data =
        RenderProcessUserData::GetForRenderProcessHost(render_process_host);
    DCHECK(user_data);
    // Clear the destruction observer to avoid a nested notification.
    user_data->SetDestructionObserver(nullptr);
    // Destroy the user data.
    render_process_host->RemoveUserData(RenderProcessUserData::UserDataKey());
  }
  render_process_hosts_.clear();

  // Release the browser and utility process nodes.
  browser_child_process_watcher_.TearDown();

  // TODO(crbug.com/40131811): |observers_| and |mechanisms_| should also be
  // empty by now!
}

void PerformanceManagerRegistryImpl::OnPerformanceManagerTabHelperDestroying(
    content::WebContents* web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const size_t num_removed = web_contents_.erase(web_contents);
  DCHECK_EQ(1U, num_removed);
}

void PerformanceManagerRegistryImpl::OnRenderProcessUserDataDestroying(
    content::RenderProcessHost* render_process_host) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const size_t num_removed = render_process_hosts_.erase(render_process_host);
  DCHECK_EQ(1U, num_removed);
}

void PerformanceManagerRegistryImpl::EnsureProcessNodeForRenderProcessHost(
    content::RenderProcessHost* render_process_host) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto result = render_process_hosts_.insert(render_process_host);
  if (result.second) {
    // Create a RenderProcessUserData if |render_process_host| doesn't already
    // have one.
    RenderProcessUserData* user_data =
        RenderProcessUserData::CreateForRenderProcessHost(render_process_host);
    user_data->SetDestructionObserver(this);
  }
}

ProcessNodeImpl* PerformanceManagerRegistryImpl::GetBrowserProcessNode() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return browser_child_process_watcher_.browser_process_node();
}

ProcessNodeImpl* PerformanceManagerRegistryImpl::GetBrowserChildProcessNode(
    BrowserChildProcessHostId id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return browser_child_process_watcher_.GetChildProcessNode(id);
}

WorkerNodeImpl* PerformanceManagerRegistryImpl::FindWorkerNodeForToken(
    const blink::WorkerToken& token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (const auto& [browser_context, worker_watcher] : worker_watchers_) {
    WorkerNodeImpl* worker_node = worker_watcher->FindWorkerNodeForToken(token);
    if (worker_node) {
      return worker_node;
    }
  }
  return nullptr;
}

WorkerWatcher* PerformanceManagerRegistryImpl::GetWorkerWatcherForTesting(
    content::BrowserContext* browser_context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto it = worker_watchers_.find(browser_context);
  return it != worker_watchers_.end() ? it->second.get() : nullptr;
}

BrowserChildProcessWatcher&
PerformanceManagerRegistryImpl::GetBrowserChildProcessWatcherForTesting() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return browser_child_process_watcher_;
}

void PerformanceManagerRegistryImpl::OnRenderProcessHostCreated(
    content::RenderProcessHost* host) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Create the ProcessNode if it doesn't already exist. This is the case in
  // web_tests and content_browsertests which do not invoke CreateProcessNode().
  EnsureProcessNodeForRenderProcessHost(host);

  // Notify the ProcessNode that its process was launched.
  RenderProcessUserData::GetForRenderProcessHost(host)->OnProcessLaunched();
}

}  // namespace performance_manager
