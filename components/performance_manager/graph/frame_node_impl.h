// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PERFORMANCE_MANAGER_GRAPH_FRAME_NODE_IMPL_H_
#define COMPONENTS_PERFORMANCE_MANAGER_GRAPH_FRAME_NODE_IMPL_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/types/pass_key.h"
#include "base/unguessable_token.h"
#include "components/performance_manager/execution_context/execution_context_impl.h"
#include "components/performance_manager/graph/node_attached_data_storage.h"
#include "components/performance_manager/graph/node_base.h"
#include "components/performance_manager/graph/node_inline_data.h"
#include "components/performance_manager/public/graph/frame_node.h"
#include "components/performance_manager/public/graph/node_attached_data.h"
#include "components/performance_manager/public/mojom/coordination_unit.mojom.h"
#include "components/performance_manager/public/mojom/web_memory.mojom.h"
#include "components/performance_manager/public/render_frame_host_proxy.h"
#include "components/performance_manager/resource_attribution/cpu_measurement_data.h"
#include "content/public/browser/browsing_instance_id.h"
#include "content/public/browser/site_instance.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "third_party/blink/public/common/tokens/tokens.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace performance_manager {

class FrameNodeImplDescriber;

// Frame nodes for a tree structure that is described in
// components/performance_manager/public/graph/frame_node.h.
class FrameNodeImpl
    : public PublicNodeImpl<FrameNodeImpl, FrameNode>,
      public TypedNodeBase<FrameNodeImpl, FrameNode, FrameNodeObserver>,
      public mojom::DocumentCoordinationUnit,
      public SupportsNodeInlineData<
          execution_context::FrameExecutionContext,
          resource_attribution::SharedCPUTimeResultData,
          // Keep this last to avoid merge conflicts.
          NodeAttachedDataStorage> {
 public:
  static const char kDefaultPriorityReason[];

  using TypedNodeBase<FrameNodeImpl, FrameNode, FrameNodeObserver>::FromNode;

  // Construct a frame node associated with a `process_node`, a `page_node` and
  // optionally with a `parent_frame_node`. For the main frame of `page_node`
  // the `parent_frame_node` parameter should be nullptr. For <fencedframe>s,
  // and MPArch aware <webview>s,  `outer_document_for_inner_frame_root` should
  // be set to its outer document, nullptr otherwise. `render_frame_id` is the
  // routing id of the frame (from RenderFrameHost::GetRoutingID).
  FrameNodeImpl(ProcessNodeImpl* process_node,
                PageNodeImpl* page_node,
                FrameNodeImpl* parent_frame_node,
                FrameNodeImpl* outer_document_for_inner_frame_root,
                int render_frame_id,
                const blink::LocalFrameToken& frame_token,
                content::BrowsingInstanceId browsing_instance_id,
                content::SiteInstanceGroupId site_instance_group_id,
                bool is_current);

  FrameNodeImpl(const FrameNodeImpl&) = delete;
  FrameNodeImpl& operator=(const FrameNodeImpl&) = delete;

  ~FrameNodeImpl() override;

  void Bind(mojo::PendingReceiver<mojom::DocumentCoordinationUnit> receiver);

  // mojom::DocumentCoordinationUnit implementation.
  void SetNetworkAlmostIdle() override;
  void SetLifecycleState(LifecycleState state) override;
  void SetHasNonEmptyBeforeUnload(bool has_nonempty_beforeunload) override;
  void SetIsAdFrame(bool is_ad_frame) override;
  void SetHadFormInteraction() override;
  void SetHadUserEdits() override;
  void OnStartedUsingWebRTC() override;
  void OnStoppedUsingWebRTC() override;
  void OnNonPersistentNotificationCreated() override;
  void OnFirstContentfulPaint(
      base::TimeDelta time_since_navigation_start) override;
  void OnWebMemoryMeasurementRequested(
      mojom::WebMemoryMeasurement::Mode mode,
      OnWebMemoryMeasurementRequestedCallback callback) override;
  void OnFreezingOriginTrialOptOut() override;

  // Partial FrameNode implementation:
  const blink::LocalFrameToken& GetFrameToken() const override;
  content::BrowsingInstanceId GetBrowsingInstanceId() const override;
  content::SiteInstanceGroupId GetSiteInstanceGroupId() const override;
  resource_attribution::FrameContext GetResourceContext() const override;
  bool IsMainFrame() const override;
  LifecycleState GetLifecycleState() const override;
  bool HasNonemptyBeforeUnload() const override;
  const GURL& GetURL() const override;
  const std::optional<url::Origin>& GetOrigin() const override;
  bool IsCurrent() const override;
  const PriorityAndReason& GetPriorityAndReason() const override;
  bool GetNetworkAlmostIdle() const override;
  bool IsAdFrame() const override;
  bool IsHoldingWebLock() const override;
  bool IsHoldingBlockingIndexedDBLock() const override;
  bool UsesWebRTC() const override;
  bool HadUserActivation() const override;
  bool HadFormInteraction() const override;
  bool HadUserEdits() const override;
  bool IsAudible() const override;
  bool IsCapturingMediaStream() const override;
  bool HasFreezingOriginTrialOptOut() const override;
  ViewportIntersection GetViewportIntersection() const override;
  Visibility GetVisibility() const override;
  bool IsIntersectingLargeArea() const override;
  bool IsImportant() const override;
  const RenderFrameHostProxy& GetRenderFrameHostProxy() const override;
  uint64_t GetResidentSetKbEstimate() const override;
  uint64_t GetPrivateFootprintKbEstimate() const override;

  // Getters for const properties.
  FrameNodeImpl* parent_frame_node() const;
  FrameNodeImpl* parent_or_outer_document_or_embedder() const;
  PageNodeImpl* page_node() const;
  ProcessNodeImpl* process_node() const;
  int render_frame_id() const;

  // Getters for non-const properties. These are not thread safe.
  NodeSetView<FrameNodeImpl*> child_frame_nodes() const;
  NodeSetView<PageNodeImpl*> opened_page_nodes() const;
  NodeSetView<PageNodeImpl*> embedded_page_nodes() const;
  NodeSetView<WorkerNodeImpl*> child_worker_nodes() const;

  // Setters are not thread safe.
  // Updates the IsCurrent() property on both `previous_frame_node` and
  // `current_frame_node` and sends a single notification to FrameNodeObservers.
  static void UpdateCurrentFrame(FrameNodeImpl* previous_frame_node,
                                 FrameNodeImpl* current_frame_node,
                                 GraphImpl* graph);
  void SetHadUserActivation();
  void SetIsHoldingWebLock(bool is_holding_weblock);
  void SetIsHoldingBlockingIndexedDBLock(
      bool is_holding_blocking_indexeddb_lock);
  void SetIsAudible(bool is_audible);
  void SetIsCapturingMediaStream(bool is_capturing_media_stream);
  void SetViewportIntersection(ViewportIntersection viewport_intersection);
  void SetInitialVisibility(Visibility visibility);
  void SetVisibility(Visibility visibility);
  void SetIsIntersectingLargeArea(bool is_intersecting_large_area);
  void SetIsImportant(bool is_important);
  void SetResidentSetKbEstimate(uint64_t rss_estimate);
  void SetPrivateFootprintKbEstimate(uint64_t private_footprint_estimate);

  // Invoked when a navigation is committed in the frame.
  void OnNavigationCommitted(GURL url,
                             url::Origin origin,
                             bool same_document,
                             bool is_served_from_back_forward_cache);

  // Traverses the frame tree and notifies all frames that their embedding
  // primary page is about to be discarded.
  void OnPrimaryPageAboutToBeDiscarded();

  // Invoked by |worker_node| when it starts/stops being a child of this frame.
  void AddChildWorker(WorkerNodeImpl* worker_node);
  void RemoveChildWorker(WorkerNodeImpl* worker_node);

  // Invoked to set the frame priority, and the reason behind it.
  void SetPriorityAndReason(const PriorityAndReason& priority_and_reason);

  base::WeakPtr<FrameNodeImpl> GetWeakPtr();

  void SeverPageRelationshipsAndMaybeReparentForTesting() {
    SeverPageRelationshipsAndMaybeReparent();
  }

  // Implementation details below this point.

  // Invoked by opened pages when this frame is set/cleared as their opener.
  // See PageNodeImpl::(Set|Clear)OpenerFrameNode.
  void AddOpenedPage(base::PassKey<PageNodeImpl> key, PageNodeImpl* page_node);
  void RemoveOpenedPage(base::PassKey<PageNodeImpl> key,
                        PageNodeImpl* page_node);

  // Invoked by embedded pages when this frame is set/cleared as their embedder.
  // See PageNodeImpl::(Set|Clear)EmbedderFrameNodeAndEmbeddingType.
  void AddEmbeddedPage(base::PassKey<PageNodeImpl> key,
                       PageNodeImpl* page_node);
  void RemoveEmbeddedPage(base::PassKey<PageNodeImpl> key,
                          PageNodeImpl* page_node);

 private:
  friend class FrameNodeImplDescriber;
  friend class ProcessNodeImpl;

  // Rest of FrameNode implementation. These are private so that users of the
  // impl use the private getters rather than the public interface.
  const FrameNode* GetParentFrameNode() const override;
  const FrameNode* GetParentOrOuterDocumentOrEmbedder() const override;
  const PageNode* GetPageNode() const override;
  const ProcessNode* GetProcessNode() const override;
  NodeSetView<const FrameNode*> GetChildFrameNodes() const override;
  NodeSetView<const PageNode*> GetOpenedPageNodes() const override;
  NodeSetView<const PageNode*> GetEmbeddedPageNodes() const override;
  NodeSetView<const WorkerNode*> GetChildWorkerNodes() const override;

  // Properties associated with a Document, which are reset when a
  // different-document navigation is committed in the frame.
  // LINT.IfChange(document_prop)
  struct DocumentProperties {
    DocumentProperties();
    ~DocumentProperties();

    void Reset(FrameNodeImpl* frame_node, GURL url_in, url::Origin origin_in);

    // FrameNodeObserver::OnURLChanged/OnOriginChanged() is invoked when the
    // URL/origin changes. Not using ObservedProperty here to allow updating
    // both properties before notifying observers (see
    // `FrameNodeImpl::DocumentProperties::Reset` implementation).
    GURL url;
    std::optional<url::Origin> origin;

    bool has_nonempty_beforeunload = false;

    // Network is considered almost idle when there are no more than 2 network
    // connections.
    ObservedProperty::NotifiesOnlyOnChanges<
        bool,
        &FrameNodeObserver::OnNetworkAlmostIdleChanged>
        network_almost_idle{false};

    // Indicates if a form in the frame has been interacted with.
    // TODO(crbug.com/40735910): Remove this once HadUserEdits is known to cover
    // all existing cases.
    ObservedProperty::NotifiesOnlyOnChanges<
        bool,
        &FrameNodeObserver::OnHadFormInteractionChanged>
        had_form_interaction{false};

    // Indicates that the user has made edits to the page. This is a superset of
    // `had_form_interaction`, but can also represent changes to
    // `contenteditable` elements.
    ObservedProperty::
        NotifiesOnlyOnChanges<bool, &FrameNodeObserver::OnHadUserEditsChanged>
            had_user_edits{false};

    // Whether the document uses WebRTC.
    ObservedProperty::NotifiesOnlyOnChanges<
        bool,
        &FrameNodeObserver::OnFrameUsesWebRTCChanged>
        uses_web_rtc{false};

    // Whether the document is opted-out from freezing via origin trial.
    ObservedProperty::NotifiesOnlyOnChanges<
        bool,
        &FrameNodeObserver::OnFrameHasFreezingOriginTrialOptOutChanged>
        has_freezing_origin_trial_opt_out{false};
  };
  // LINT.ThenChange(//components/performance_manager/graph/frame_node_impl.cc:document_prop_reset)

  // Invoked by subframes on joining/leaving the graph.
  void AddChildFrame(FrameNodeImpl* frame_node);
  void RemoveChildFrame(FrameNodeImpl* frame_node);

  // NodeBase:
  void OnInitializingProperties() override;
  void OnInitializingEdges() override;
  void OnBeforeLeavingGraph() override;
  void OnUninitializingEdges() override;
  void CleanUpNodeState() override;

  // Helper function to sever all opened/embedded page relationships. This is
  // called before destroying the frame node in "OnBeforeLeavingGraph". Note
  // that this will reparent embedded pages to this frame's parent so that
  // tracking is maintained.
  void SeverPageRelationshipsAndMaybeReparent();

  // This is not quite the same as GetMainFrame, because there can be multiple
  // main frames while the main frame is navigating. This explicitly walks up
  // the tree to find the main frame that corresponds to this frame tree node,
  // even if it is not current.
  FrameNodeImpl* GetFrameTreeRoot() const;

  bool HasFrameNodeInAncestors(FrameNodeImpl* frame_node) const;
  bool HasFrameNodeInDescendants(FrameNodeImpl* frame_node) const;
  bool HasFrameNodeInTree(FrameNodeImpl* frame_node) const;

  // Sets the `is_current_` property. Returns true if its value changed as a
  // result of this call.
  bool SetIsCurrent(bool is_current);

  // Updates the inherited `IsIntersectingLargeArea()` property of this frame.
  void SetInheritedIsIntersectingLargeArea(bool is_intersecting_large_area);

  void SetIsIntersectingLargeAreaImpl(bool is_intersecting_large_area);

  mojo::Receiver<mojom::DocumentCoordinationUnit> receiver_{this};

  const raw_ptr<FrameNodeImpl, DanglingUntriaged> parent_frame_node_;
  const raw_ptr<FrameNodeImpl, DanglingUntriaged>
      outer_document_for_inner_frame_root_;
  const raw_ptr<PageNodeImpl, DanglingUntriaged> page_node_;
  const raw_ptr<ProcessNodeImpl, DanglingUntriaged> process_node_;
  // The routing id of the frame.
  const int render_frame_id_;

  // This is the unique token for this frame instance as per e.g.
  // RenderFrameHost::GetFrameToken().
  const blink::LocalFrameToken frame_token_;

  // The unique ID of the BrowsingInstance this frame belongs to. Frames in the
  // same BrowsingInstance are allowed to script each other at least
  // asynchronously (if cross-site), and sometimes synchronously (if same-site,
  // and thus same SiteInstance).
  const content::BrowsingInstanceId browsing_instance_id_;

  // The unique ID of the SiteInstanceGroup this frame belongs to. Frames in the
  // same SiteInstanceGroup are in the same process and exist as LocalFrames in
  // the same blink::FrameTree. Frames with the same |site_instance_group_id_|
  // will also have the same |browsing_instance_id_|.
  const content::SiteInstanceGroupId site_instance_group_id_;

  // A proxy object that lets the underlying RFH be safely dereferenced on the
  // UI thread.
  const RenderFrameHostProxy render_frame_host_proxy_;

  NodeSet child_frame_nodes_;

  // The set of pages that have been opened by this frame.
  NodeSet opened_page_nodes_;

  // The set of pages that have been embedded by this frame.
  NodeSet embedded_page_nodes_;

  uint64_t resident_set_kb_estimate_ = 0;

  uint64_t private_footprint_kb_estimate_ = 0;

  // Does *not* change when a navigation is committed.
  ObservedProperty::NotifiesOnlyOnChanges<
      LifecycleState,
      &FrameNodeObserver::OnFrameLifecycleStateChanged>
      lifecycle_state_{LifecycleState::kRunning};

  // Indicates if the frame has been interacted with.
  ObservedProperty::NotifiesOnlyOnChanges<
      bool,
      &FrameNodeObserver::OnHadUserActivationChanged>
      had_user_activation_{false};

  ObservedProperty::
      NotifiesOnlyOnChanges<bool, &FrameNodeObserver::OnIsAdFrameChanged>
          is_ad_frame_{false};

  // Locks held by a frame are tracked independently from navigation
  // (specifically, a few tasks must run in the Web Lock and IndexedDB
  // subsystems after a navigation for locks to be released).
  ObservedProperty::NotifiesOnlyOnChanges<
      bool,
      &FrameNodeObserver::OnFrameIsHoldingWebLockChanged>
      is_holding_weblock_{false};
  ObservedProperty::NotifiesOnlyOnChanges<
      bool,
      &FrameNodeObserver::OnFrameIsHoldingBlockingIndexedDBLockChanged>
      is_holding_blocking_indexeddb_lock_{false};

  bool is_current_{false};

  // Properties associated with a Document, which are reset when a
  // different-document navigation is committed in the frame.
  //
  // TODO(fdoray): Cleanup this once there is a 1:1 mapping between
  // RenderFrameHost and Document https://crbug.com/936696.
  DocumentProperties document_;

  // The child workers of this frame.
  NodeSet child_worker_nodes_;

  // Frame priority information. Set via ExecutionContextPriorityDecorator.
  ObservedProperty::NotifiesOnlyOnChangesWithPreviousValue<
      PriorityAndReason,
      const PriorityAndReason&,
      &FrameNodeObserver::OnPriorityAndReasonChanged>
      priority_and_reason_{PriorityAndReason(base::TaskPriority::LOWEST,
                                             kDefaultPriorityReason)};

  // Indicates if the frame is audible. This is tracked independently of a
  // document, and if a document swap occurs the audio stream monitor machinery
  // will keep this up to date.
  ObservedProperty::
      NotifiesOnlyOnChanges<bool, &FrameNodeObserver::OnIsAudibleChanged>
          is_audible_{false};

  // Indicates if the frame is capturing at least one media stream.
  ObservedProperty::NotifiesOnlyOnChanges<
      bool,
      &FrameNodeObserver::OnIsCapturingMediaStreamChanged>
      is_capturing_media_stream_{false};

  // Indicates the intersection between the frame and the viewport.
  //
  // Note that calling `GetViewportIntersection()` will always returns
  // ViewportIntersection::kIntersecting for the outermost main frame. This is
  // because it always occupies the entirety of the viewport, so there is no
  // point in tracking it.
  ObservedProperty::NotifiesOnlyOnChanges<
      ViewportIntersection,
      &FrameNodeObserver::OnViewportIntersectionChanged>
      viewport_intersection_{ViewportIntersection::kUnknown};

  // Indicates if the frame is visible. This is maintained by the
  // FrameVisibilityDecorator.
  ObservedProperty::NotifiesOnlyOnChangesWithPreviousValue<
      Visibility,
      Visibility,
      &FrameNodeObserver::OnFrameVisibilityChanged>
      visibility_{Visibility::kUnknown};

  // Indicates if this frame intersects with a large area of the viewport.
  // Defaults to true when its value is unknown.
  bool is_intersecting_large_area_ = true;

  // Indicates that SetIsIntersectingLargeArea() was called for this frame. This
  // is only called for local root frames and means this frame should not
  // inherit the value of its parent.
  bool has_is_intersecting_large_area_updates_ = false;

  ObservedProperty::
      NotifiesOnlyOnChanges<bool, &FrameNodeObserver::OnIsImportantChanged>
          is_important_{true};

  base::WeakPtrFactory<FrameNodeImpl> weak_factory_
      GUARDED_BY_CONTEXT(sequence_checker_){this};
};

}  // namespace performance_manager

#endif  // COMPONENTS_PERFORMANCE_MANAGER_GRAPH_FRAME_NODE_IMPL_H_
