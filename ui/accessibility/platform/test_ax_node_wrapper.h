// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_ACCESSIBILITY_PLATFORM_TEST_AX_NODE_WRAPPER_H_
#define UI_ACCESSIBILITY_PLATFORM_TEST_AX_NODE_WRAPPER_H_

#include <set>
#include <string>
#include <vector>

#include "base/auto_reset.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "build/build_config.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/accessibility/ax_tree_observer.h"
#include "ui/accessibility/platform/ax_platform_node.h"
#include "ui/accessibility/platform/ax_platform_node_delegate.h"
#include "ui/accessibility/platform/ax_unique_id.h"

#if BUILDFLAG(IS_WIN)
namespace gfx {
const AcceleratedWidget kMockAcceleratedWidget = reinterpret_cast<HWND>(-1);
}
#endif

namespace ui {

// For testing, a TestAXNodeWrapper wraps an AXNode, implements
// AXPlatformNodeDelegate, and owns an AXPlatformNode.
// TODO(crbug.com/374813016): Refactor global variables into class members in
// ui/accessibility/platform/test_ax_node_wrapper.h
class TestAXNodeWrapper : public AXPlatformNodeDelegate, public AXTreeObserver {
 public:
  // Create TestAXNodeWrapper instances on-demand from an AXTree and AXNode.
  static TestAXNodeWrapper* GetOrCreate(AXTree* tree, AXNode* node);

  // Set a global coordinate offset for testing.
  static void SetGlobalCoordinateOffset(const gfx::Vector2d& offset);

  // Get the last node which ShowContextMenu was called from for testing.
  static const AXNode* GetNodeFromLastShowContextMenu();

  // Get the last node which AccessibilityPerformAction default action was
  // called from for testing.
  static const AXNode* GetNodeFromLastDefaultAction();

  // Set the last node which AccessibilityPerformAction default action was
  // called for testing.
  static void SetNodeFromLastDefaultAction(AXNode* node);

  // Set a global scale factor for testing.
  static std::unique_ptr<base::AutoReset<float>> SetScaleFactor(float value);

  // Set a global indicating that AXPlatformNodeDelegates are for web content.
  static void SetGlobalIsWebContent(bool is_web_content);

  // When a hit test is called on |src_node_id|, return |dst_node_id| as
  // the result.
  static void SetHitTestResult(AXNodeID src_node_id, AXNodeID dst_node_id);

  // This is used to make sure global state doesn't persist across tests.
  static void ResetGlobalState();

  ~TestAXNodeWrapper() override;

  AXPlatformNode* ax_platform_node() const { return platform_node_.get(); }
  void set_minimized(bool minimized) { minimized_ = minimized; }

  // Test helpers.
  void BuildAllWrappers(AXTree* tree, AXNode* node);
  void ResetNativeEventTarget();

  // AXPlatformNodeDelegate.
  const AXNodeData& GetData() const override;
  const AXTreeData& GetTreeData() const override;
  const AXSelection GetUnignoredSelection() const override;
  AXNodePosition::AXPositionInstance CreatePositionAt(
      int offset,
      ax::mojom::TextAffinity affinity =
          ax::mojom::TextAffinity::kDownstream) const override;
  AXNodePosition::AXPositionInstance CreateTextPositionAt(
      int offset,
      ax::mojom::TextAffinity affinity =
          ax::mojom::TextAffinity::kDownstream) const override;
  gfx::NativeViewAccessible GetNativeViewAccessible() override;
  gfx::NativeViewAccessible GetParent() const override;
  size_t GetChildCount() const override;
  gfx::NativeViewAccessible ChildAtIndex(size_t index) const override;
  gfx::Rect GetBoundsRect(const AXCoordinateSystem coordinate_system,
                          const AXClippingBehavior clipping_behavior,
                          AXOffscreenResult* offscreen_result) const override;
  gfx::Rect GetInnerTextRangeBoundsRect(
      const int start_offset,
      const int end_offset,
      const AXCoordinateSystem coordinate_system,
      const AXClippingBehavior clipping_behavior,
      AXOffscreenResult* offscreen_result) const override;
  gfx::Rect GetHypertextRangeBoundsRect(
      const int start_offset,
      const int end_offset,
      const AXCoordinateSystem coordinate_system,
      const AXClippingBehavior clipping_behavior,
      AXOffscreenResult* offscreen_result) const override;
  gfx::NativeViewAccessible HitTestSync(
      int screen_physical_pixel_x,
      int screen_physical_pixel_y) const override;
  gfx::NativeViewAccessible GetFocus() const override;
  bool IsMinimized() const override;
  bool IsWebContent() const override;
  bool IsReadOnlySupported() const override;
  bool IsReadOnlyOrDisabled() const override;
  AXPlatformNode* GetFromNodeID(int32_t id) override;
  AXPlatformNode* GetFromTreeIDAndNodeID(const AXTreeID& ax_tree_id,
                                         int32_t id) override;
  std::optional<size_t> GetIndexInParent() const override;
  std::optional<int> GetTableRowCount() const override;
  std::optional<int> GetTableColCount() const override;
  std::optional<int> GetTableAriaColCount() const override;
  std::optional<int> GetTableAriaRowCount() const override;
  std::optional<int> GetTableCellCount() const override;
  std::vector<int32_t> GetColHeaderNodeIds() const override;
  std::vector<int32_t> GetColHeaderNodeIds(int col_index) const override;
  std::vector<int32_t> GetRowHeaderNodeIds() const override;
  std::vector<int32_t> GetRowHeaderNodeIds(int row_index) const override;
  bool IsTableRow() const override;
  std::optional<int> GetTableRowRowIndex() const override;
  bool IsTableCellOrHeader() const override;
  std::optional<int> GetTableCellIndex() const override;
  std::optional<int> GetTableCellColIndex() const override;
  std::optional<int> GetTableCellRowIndex() const override;
  std::optional<int> GetTableCellColSpan() const override;
  std::optional<int> GetTableCellRowSpan() const override;
  std::optional<int> GetTableCellAriaColIndex() const override;
  std::optional<int> GetTableCellAriaRowIndex() const override;
  std::optional<int32_t> GetCellId(int row_index, int col_index) const override;
  std::optional<int32_t> GetCellIdAriaCoords(int aria_row_index,
                                             int aria_col_index) const override;
  std::optional<int32_t> CellIndexToId(int cell_index) const override;
  bool IsCellOrHeaderOfAriaGrid() const override;
  gfx::AcceleratedWidget GetTargetForNativeAccessibilityEvent() override;
  bool AccessibilityPerformAction(const AXActionData& data) override;
  std::u16string GetLocalizedRoleDescriptionForUnlabeledImage() const override;
  std::u16string GetLocalizedStringForLandmarkType() const override;
  std::u16string GetLocalizedStringForRoleDescription() const override;
  std::u16string GetLocalizedStringForImageAnnotationStatus(
      ax::mojom::ImageAnnotationStatus status) const override;
  std::u16string GetStyleNameAttributeAsLocalizedString() const override;
  bool ShouldIgnoreHoveredStateForTesting() override;
  AXPlatformNodeId GetUniqueId() const override;
  bool HasVisibleCaretOrSelection() const override;
  std::vector<AXPlatformNode*> GetSourceNodesForReverseRelations(
      ax::mojom::IntAttribute attr) override;
  std::vector<AXPlatformNode*> GetSourceNodesForReverseRelations(
      ax::mojom::IntListAttribute attr) override;
  bool IsOrderedSetItem() const override;
  bool IsOrderedSet() const override;
  std::optional<int> GetPosInSet() const override;
  std::optional<int> GetSetSize() const override;
  SkColor GetColor() const override;
  SkColor GetBackgroundColor() const override;

  const std::vector<gfx::NativeViewAccessible> GetUIADirectChildrenInRange(
      AXPlatformNodeDelegate* start,
      AXPlatformNodeDelegate* end) override;
  gfx::RectF GetLocation() const;
  size_t InternalChildCount() const;
  TestAXNodeWrapper* InternalGetChild(size_t index) const;

 private:
  TestAXNodeWrapper(AXTree* tree, AXNode* node);
  void ReplaceIntAttribute(int32_t node_id,
                           ax::mojom::IntAttribute attribute,
                           int32_t value);
  void ReplaceFloatAttribute(ax::mojom::FloatAttribute attribute, float value);
  void ReplaceBoolAttribute(ax::mojom::BoolAttribute attribute, bool value);
  void ReplaceStringAttribute(ax::mojom::StringAttribute attribute,
                              std::string value);
  void ReplaceTreeDataTextSelection(int32_t anchor_node_id,
                                    int32_t anchor_offset,
                                    int32_t focus_node_id,
                                    int32_t focus_offset);

  TestAXNodeWrapper* HitTestSyncInternal(int x, int y);
  void UIADescendants(
      const AXNode* node,
      std::vector<gfx::NativeViewAccessible>* descendants) const;
  static bool ShouldHideChildrenForUIA(const AXNode* node);

  // Return the bounds of inline text in this node's coordinate system (which is
  // relative to its container node specified in AXRelativeBounds).
  gfx::RectF GetInlineTextRect(const int start_offset,
                               const int end_offset) const;

  // Helper for determining if the two rects, including empty rects, intersect
  // each other.
  bool Intersects(gfx::RectF rect1, gfx::RectF rect2) const;

  // Determine the offscreen status of a particular element given its bounds.
  AXOffscreenResult DetermineOffscreenResult(gfx::RectF bounds) const;

  // `AXTreeObserver`
  void OnNodeWillBeDeleted(AXTree* tree, AXNode* node) override;

  raw_ptr<AXTree> tree_;
  raw_ptr<AXNode> node_;
  AXUniqueId unique_id_;
  AXPlatformNode::Pointer platform_node_;
  gfx::AcceleratedWidget native_event_target_;
  bool minimized_ = false;
  base::ScopedObservation<AXTree, AXTreeObserver> observation_{this};
};

}  // namespace ui

#endif  // UI_ACCESSIBILITY_PLATFORM_TEST_AX_NODE_WRAPPER_H_
