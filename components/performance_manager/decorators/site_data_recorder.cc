// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/performance_manager/public/decorators/site_data_recorder.h"

#include <memory>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/notreached.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "components/performance_manager/graph/node_inline_data.h"
#include "components/performance_manager/graph/page_node_impl.h"
#include "components/performance_manager/persistence/site_data/site_data_cache.h"
#include "components/performance_manager/persistence/site_data/site_data_cache_factory.h"
#include "components/performance_manager/persistence/site_data/site_data_writer.h"
#include "components/performance_manager/public/graph/graph.h"
#include "components/performance_manager/public/persistence/site_data/site_data_reader.h"

namespace performance_manager {

namespace {

// The period of time after loading during which we ignore title/favicon
// change events. It's possible for some site that are loaded in background to
// use some of these features without this being an attempt to communicate
// with the user (e.g. the page is just really finishing to load).
constexpr base::TimeDelta kTitleOrFaviconChangePostLoadGracePeriod =
    base::Seconds(20);

// The period of time during which audio usage gets ignored after a page gets
// backgrounded. It's necessary because there might be a delay between a media
// request gets initiated and the time the audio actually starts.
constexpr base::TimeDelta kFeatureUsagePostBackgroundGracePeriod =
    base::Seconds(10);

SiteDataRecorder* g_site_data_recorder = nullptr;

TabVisibility GetPageNodeVisibility(const PageNode* page_node) {
  return page_node->IsVisible() ? TabVisibility::kForeground
                                : TabVisibility::kBackground;
}

// Default implementation of SiteDataRecorderHeuristics that's used in
// production.
class DefaultHeuristics final : public SiteDataRecorderHeuristics {
 public:
  DefaultHeuristics() = default;
  ~DefaultHeuristics() final = default;

  DefaultHeuristics(const DefaultHeuristics& other) = delete;
  DefaultHeuristics& operator=(const DefaultHeuristics&) = delete;

  bool IsLoadedIdle(PageNode::LoadingState loading_state) const final {
    return DefaultIsLoadedIdle(loading_state);
  }

  bool IsInBackground(const PageNode* page_node) const final {
    return DefaultIsInBackground(page_node);
  }

  bool IsOutsideLoadingGracePeriod(
      const PageNode* page_node,
      FeatureType feature_type,
      base::TimeDelta time_since_load) const final {
    return DefaultIsOutsideLoadingGracePeriod(page_node, feature_type,
                                              time_since_load);
  }

  bool IsOutsideBackgroundingGracePeriod(
      const PageNode* page_node,
      FeatureType feature_type,
      base::TimeDelta time_since_backgrounding) const final {
    return DefaultIsOutsideBackgroundingGracePeriod(page_node, feature_type,
                                                    time_since_backgrounding);
  }
};

// Helper class that watches a PageNode, and invokes a callback when that node
// has a SiteDataReader whose data is ready.
class SiteDataReaderWaiter final : public PageNodeObserver {
 public:
  using DataReadyCallback = base::OnceCallback<void(const SiteDataReader&)>;

  static void WaitForSiteData(const PageNode* page_node,
                              DataReadyCallback callback) {
    // SiteDataReaderWaiter will become self-owned.
    new SiteDataReaderWaiter(page_node, std::move(callback));
  }

  ~SiteDataReaderWaiter() final = default;

  SiteDataReaderWaiter(const SiteDataReaderWaiter&) = delete;
  SiteDataReaderWaiter& operator=(const SiteDataReaderWaiter&) = delete;

  // PageNodeObserver:
  void OnBeforePageNodeRemoved(const PageNode* page_node) final {
    if (page_node == watched_page_node_) {
      // No longer needed.
      DeleteSelfSoon();
    }
  }

  void OnMainFrameUrlChanged(const PageNode* page_node) final {
    if (page_node == watched_page_node_ && !waiting_for_data_ready_) {
      CheckForSiteDataReader();
    }
  }

 private:
  SiteDataReaderWaiter(const PageNode* page_node, DataReadyCallback callback)
      : watched_page_node_(page_node),
        data_ready_callback_(std::move(callback)) {
    self_ = base::WrapUnique(this);
    watched_page_node_->GetGraph()->AddPageNodeObserver(this);
    CheckForSiteDataReader();
  }

  void DeleteSelfSoon() {
    // `watched_page_node_` may be deleted before the destructor runs,
    // so clear it now.
    CHECK(watched_page_node_);
    watched_page_node_->GetGraph()->RemovePageNodeObserver(this);
    watched_page_node_ = nullptr;
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(self_));
  }

  void CheckForSiteDataReader() {
    CHECK(watched_page_node_);
    CHECK(!waiting_for_data_ready_);
    auto* site_data_reader =
        SiteDataRecorder::Data::GetReaderForPageNode(watched_page_node_);
    if (site_data_reader) {
      waiting_for_data_ready_ = true;
      // If data is already loaded, OnDataReady() will be called immediately.
      site_data_reader->RegisterDataLoadedCallback(
          base::BindOnce(&SiteDataReaderWaiter::OnDataReady,
                         weak_factory_.GetWeakPtr(), site_data_reader));
    }
  }

  void OnDataReady(const SiteDataReader* site_data_reader) {
    CHECK(site_data_reader);
    std::move(data_ready_callback_).Run(*site_data_reader);
    DeleteSelfSoon();
  }

  raw_ptr<const PageNode> watched_page_node_;
  DataReadyCallback data_ready_callback_;

  bool waiting_for_data_ready_ = false;

  // Self-owned. Will delete itself when `watched_page_node_` is deleted or
  // OnDataReady() fires.
  std::unique_ptr<SiteDataReaderWaiter> self_;

  base::WeakPtrFactory<SiteDataReaderWaiter> weak_factory_{this};
};

SiteDataNodeData& GetSiteDataNodeDataFromPageNode(const PageNode* page_node) {
  DCHECK(page_node);
  auto* page_node_impl = PageNodeImpl::FromNode(page_node);
  return SiteDataNodeData::Get(page_node_impl);
}

}  // namespace

SiteDataRecorder::SiteDataRecorder()
    : heuristics_impl_(std::make_unique<DefaultHeuristics>()) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

SiteDataRecorder::~SiteDataRecorder() = default;

void SiteDataRecorder::OnPassedToGraph(Graph* graph) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(!g_site_data_recorder);
  g_site_data_recorder = this;
  RegisterObservers(graph);
}

void SiteDataRecorder::OnTakenFromGraph(Graph* graph) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  UnregisterObservers(graph);
  CHECK_EQ(g_site_data_recorder, this);
  g_site_data_recorder = nullptr;
}

void SiteDataRecorder::OnPageNodeAdded(const PageNode* page_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SetPageNodeDataCache(page_node);
}

void SiteDataRecorder::OnBeforePageNodeRemoved(const PageNode* page_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SiteDataNodeData& data = GetSiteDataNodeDataFromPageNode(page_node);
  data.Reset();
}

void SiteDataRecorder::OnMainFrameUrlChanged(const PageNode* page_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SiteDataNodeData& data = GetSiteDataNodeDataFromPageNode(page_node);
  data.OnMainFrameUrlChanged(page_node->GetMainFrameUrl(),
                             page_node->IsVisible());
}

void SiteDataRecorder::OnLoadingStateChanged(
    const PageNode* page_node,
    PageNode::LoadingState previous_state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SiteDataNodeData& data = GetSiteDataNodeDataFromPageNode(page_node);
  const bool is_loaded_idle =
      heuristics_impl_->IsLoadedIdle(page_node->GetLoadingState());
  const bool was_loaded_idle = heuristics_impl_->IsLoadedIdle(previous_state);
  if (is_loaded_idle != was_loaded_idle) {
    data.OnIsLoadedIdleChanged(is_loaded_idle);
  }
}

void SiteDataRecorder::OnIsVisibleChanged(const PageNode* page_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SiteDataNodeData& data = GetSiteDataNodeDataFromPageNode(page_node);
  data.OnIsVisibleChanged(page_node->IsVisible());
}

void SiteDataRecorder::OnIsAudibleChanged(const PageNode* page_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SiteDataNodeData& data = GetSiteDataNodeDataFromPageNode(page_node);
  data.OnIsAudibleChanged(page_node->IsAudible());
}

void SiteDataRecorder::OnTitleUpdated(const PageNode* page_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SiteDataNodeData& data = GetSiteDataNodeDataFromPageNode(page_node);
  data.OnTitleUpdated();
}

void SiteDataRecorder::OnFaviconUpdated(const PageNode* page_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SiteDataNodeData& data = GetSiteDataNodeDataFromPageNode(page_node);
  data.OnFaviconUpdated();
}

// static
void SiteDataRecorder::SetHeuristicsImplementationForTesting(
    std::unique_ptr<SiteDataRecorderHeuristics> heuristics) {
  CHECK(g_site_data_recorder);
  g_site_data_recorder->heuristics_impl_ =
      heuristics ? std::move(heuristics)
                 : std::make_unique<DefaultHeuristics>();
}

void SiteDataRecorder::RegisterObservers(Graph* graph) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  graph->AddPageNodeObserver(this);
}

void SiteDataRecorder::UnregisterObservers(Graph* graph) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  graph->RemovePageNodeObserver(this);
}

void SiteDataRecorder::SetPageNodeDataCache(const PageNode* page_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto* page_node_impl = PageNodeImpl::FromNode(page_node);
  DCHECK(page_node_impl);
  auto& data = SiteDataNodeData::Create(page_node_impl, page_node_impl, this);
  // If this PageNode is in a browser context that doesn't enable keyed services
  // (such as the system profile), it will have no SiteDataCache.
  if (auto* factory = SiteDataCacheFactory::GetInstance()) {
    if (auto* data_cache = factory->GetDataCacheForBrowserContext(
            page_node->GetBrowserContextID())) {
      data.set_data_cache(data_cache);
    }
  }
}

// static
bool SiteDataRecorderHeuristics::DefaultIsLoadedIdle(
    PageNode::LoadingState loading_state) {
  switch (loading_state) {
    case PageNode::LoadingState::kLoadingNotStarted:
    case PageNode::LoadingState::kLoadedBusy:
    case PageNode::LoadingState::kLoading:
    case PageNode::LoadingState::kLoadingTimedOut:
      return false;
    case PageNode::LoadingState::kLoadedIdle:
      return true;
  }
  NOTREACHED();
}

// static
bool SiteDataRecorderHeuristics::DefaultIsInBackground(
    const PageNode* page_node) {
  return GetPageNodeVisibility(page_node) != TabVisibility::kForeground;
}

// static
bool SiteDataRecorderHeuristics::DefaultIsOutsideLoadingGracePeriod(
    const PageNode* page_node,
    FeatureType feature_type,
    base::TimeDelta time_since_load) {
  if (feature_type == FeatureType::kTitleChange ||
      feature_type == FeatureType::kFaviconChange) {
    return time_since_load >= kTitleOrFaviconChangePostLoadGracePeriod;
  }
  return true;
}

// static
bool SiteDataRecorderHeuristics::DefaultIsOutsideBackgroundingGracePeriod(
    const PageNode* page_node,
    FeatureType feature_type,
    base::TimeDelta time_since_backgrounding) {
  // Ignore events happening shortly after the tab being backgrounded, they're
  // usually false positives.
  return time_since_backgrounding >= kFeatureUsagePostBackgroundGracePeriod;
}

// static
const SiteDataRecorder::Data& SiteDataRecorder::Data::FromPageNode(
    const PageNode* page_node) {
  return SiteDataNodeData::Get(PageNodeImpl::FromNode(page_node));
}

// static
SiteDataRecorder::Data& SiteDataRecorder::Data::GetForTesting(
    const PageNode* page_node) {
  return GetSiteDataNodeDataFromPageNode(page_node);
}

// static
SiteDataReader* SiteDataRecorder::Data::GetReaderForPageNode(
    const PageNode* page_node) {
  PageNodeImpl* page_node_impl = PageNodeImpl::FromNode(page_node);
  if (SiteDataNodeData::Exists(page_node_impl)) {
    return SiteDataNodeData::Get(page_node_impl).reader();
  }
  return nullptr;
}

void WaitForSiteDataReader(
    base::WeakPtr<PageNode> page_node,
    base::OnceCallback<void(const SiteDataReader&)> callback) {
  if (page_node) {
    SiteDataReaderWaiter::WaitForSiteData(page_node.get(), std::move(callback));
  }
}

}  // namespace performance_manager
