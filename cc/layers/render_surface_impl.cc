// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/layers/render_surface_impl.h"

#include <stddef.h>

#include <algorithm>

#include "base/check_op.h"
#include "cc/base/math_util.h"
#include "cc/debug/debug_colors.h"
#include "cc/layers/append_quads_context.h"
#include "cc/layers/append_quads_data.h"
#include "cc/paint/element_id.h"
#include "cc/paint/filter_operations.h"
#include "cc/trees/damage_tracker.h"
#include "cc/trees/effect_node.h"
#include "cc/trees/layer_tree_impl.h"
#include "cc/trees/occlusion.h"
#include "cc/trees/transform_node.h"
#include "components/viz/common/quads/compositor_render_pass.h"
#include "components/viz/common/quads/compositor_render_pass_draw_quad.h"
#include "components/viz/common/quads/content_draw_quad_base.h"
#include "components/viz/common/quads/debug_border_draw_quad.h"
#include "components/viz/common/quads/shared_quad_state.h"
#include "components/viz/common/quads/solid_color_draw_quad.h"
#include "components/viz/common/quads/tile_draw_quad.h"
#include "third_party/skia/include/core/SkImageFilter.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/geometry/transform.h"

namespace cc {

RenderSurfaceImpl::RenderSurfaceImpl(LayerTreeImpl* layer_tree_impl,
                                     ElementId id)
    : layer_tree_impl_(layer_tree_impl),
      id_(id),
      effect_tree_index_(kInvalidPropertyNodeId),
      layer_id_(Layer::GetNextLayerId()) {
  DCHECK(id);
  damage_tracker_ = DamageTracker::Create();
}

RenderSurfaceImpl::~RenderSurfaceImpl() = default;

RenderSurfaceImpl* RenderSurfaceImpl::render_target() {
  EffectTree& effect_tree =
      layer_tree_impl_->property_trees()->effect_tree_mutable();
  EffectNode* node = effect_tree.Node(EffectTreeIndex());
  if (node->target_id != kRootPropertyNodeId)
    return effect_tree.GetRenderSurface(node->target_id);
  else
    return this;
}

const RenderSurfaceImpl* RenderSurfaceImpl::render_target() const {
  const EffectTree& effect_tree =
      layer_tree_impl_->property_trees()->effect_tree();
  const EffectNode* node = effect_tree.Node(EffectTreeIndex());
  if (node->target_id != kRootPropertyNodeId)
    return effect_tree.GetRenderSurface(node->target_id);
  else
    return this;
}

RenderSurfaceImpl::DrawProperties::DrawProperties() = default;

RenderSurfaceImpl::DrawProperties::~DrawProperties() = default;

gfx::RectF RenderSurfaceImpl::DrawableContentRect() const {
  if (content_rect().IsEmpty())
    return gfx::RectF();

  gfx::Rect surface_content_rect = content_rect();
  const FilterOperations& filters = Filters();
  if (!filters.IsEmpty()) {
    surface_content_rect =
        filters.MapRect(surface_content_rect,
                        gfx::TransformToFlattenedSkMatrix(SurfaceScale()));
  }
  gfx::RectF drawable_content_rect = MathUtil::MapClippedRect(
      draw_transform(), gfx::RectF(surface_content_rect));
  if (is_clipped()) {
    drawable_content_rect.Intersect(gfx::RectF(clip_rect()));
  }

  // If the rect has a NaN coordinate, we return empty rect to avoid crashes in
  // functions (for example, gfx::ToEnclosedRect) that are called on this rect.
  if (std::isnan(drawable_content_rect.x()) ||
      std::isnan(drawable_content_rect.y()) ||
      std::isnan(drawable_content_rect.right()) ||
      std::isnan(drawable_content_rect.bottom()))
    return gfx::RectF();

  return drawable_content_rect;
}

SkBlendMode RenderSurfaceImpl::BlendMode() const {
  return OwningEffectNode()->blend_mode;
}

SkColor4f RenderSurfaceImpl::GetDebugBorderColor() const {
  return DebugColors::SurfaceBorderColor();
}

float RenderSurfaceImpl::GetDebugBorderWidth() const {
  return DebugColors::SurfaceBorderWidth(
      layer_tree_impl_ ? layer_tree_impl_->device_scale_factor() : 1);
}

LayerImpl* RenderSurfaceImpl::BackdropMaskLayer() const {
  ElementId mask_element_id = OwningEffectNode()->backdrop_mask_element_id;
  if (!mask_element_id)
    return nullptr;
  return layer_tree_impl_->LayerByElementId(mask_element_id);
}

bool RenderSurfaceImpl::HasMaskingContributingSurface() const {
  return OwningEffectNode()->has_masking_child;
}

const FilterOperations& RenderSurfaceImpl::Filters() const {
  return OwningEffectNode()->filters;
}

gfx::Transform RenderSurfaceImpl::SurfaceScale() const {
  gfx::Transform surface_scale;
  surface_scale.Scale(OwningEffectNode()->surface_contents_scale.x(),
                      OwningEffectNode()->surface_contents_scale.y());
  return surface_scale;
}

const FilterOperations& RenderSurfaceImpl::BackdropFilters() const {
  return OwningEffectNode()->backdrop_filters;
}

std::optional<SkPath> RenderSurfaceImpl::BackdropFilterBounds() const {
  return OwningEffectNode()->backdrop_filter_bounds;
}

bool RenderSurfaceImpl::TrilinearFiltering() const {
  return OwningEffectNode()->trilinear_filtering;
}

bool RenderSurfaceImpl::HasCopyRequest() const {
  return OwningEffectNode()->has_copy_request;
}

viz::SubtreeCaptureId RenderSurfaceImpl::SubtreeCaptureId() const {
  return OwningEffectNode()->subtree_capture_id;
}

gfx::Size RenderSurfaceImpl::SubtreeSize() const {
  return OwningEffectNode()->subtree_size;
}

bool RenderSurfaceImpl::ShouldCacheRenderSurface() const {
  return OwningEffectNode()->cache_render_surface;
}

bool RenderSurfaceImpl::CopyOfOutputRequired() const {
  return HasCopyRequest() || ShouldCacheRenderSurface() ||
         SubtreeCaptureId().is_valid() || IsViewTransitionElement();
}

bool RenderSurfaceImpl::IsViewTransitionElement() const {
  return ViewTransitionElementResourceId().IsValid();
}

const viz::ViewTransitionElementResourceId&
RenderSurfaceImpl::ViewTransitionElementResourceId() const {
  return OwningEffectNode()->view_transition_element_resource_id;
}

int RenderSurfaceImpl::TransformTreeIndex() const {
  return OwningEffectNode()->transform_id;
}

int RenderSurfaceImpl::ClipTreeIndex() const {
  return OwningEffectNode()->clip_id;
}

int RenderSurfaceImpl::EffectTreeIndex() const {
  return effect_tree_index_;
}

const EffectNode* RenderSurfaceImpl::OwningEffectNode() const {
  return layer_tree_impl_->property_trees()->effect_tree().Node(
      EffectTreeIndex());
}

EffectNode* RenderSurfaceImpl::OwningEffectNodeMutableForTest() const {
  return layer_tree_impl_->property_trees()->effect_tree_mutable().Node(
      EffectTreeIndex());
}

void RenderSurfaceImpl::SetClipRect(const gfx::Rect& clip_rect) {
  if (clip_rect == draw_properties_.clip_rect)
    return;

  surface_property_changed_ = true;
  draw_properties_.clip_rect = clip_rect;
}

void RenderSurfaceImpl::SetContentRect(const gfx::Rect& content_rect) {
  if (content_rect == draw_properties_.content_rect)
    return;

  surface_property_changed_ = true;
  draw_properties_.content_rect = content_rect;
}

void RenderSurfaceImpl::SetContentRectForTesting(const gfx::Rect& rect) {
  SetContentRect(rect);
}

gfx::Rect RenderSurfaceImpl::CalculateExpandedClipForFilters(
    const gfx::Transform& target_to_surface) {
  gfx::Rect clip_in_surface_space =
      MathUtil::ProjectEnclosingClippedRect(target_to_surface, clip_rect());
  gfx::Rect expanded_clip_in_surface_space = Filters().MapRect(
      clip_in_surface_space, gfx::TransformToFlattenedSkMatrix(SurfaceScale()));
  gfx::Rect expanded_clip_in_target_space = MathUtil::MapEnclosingClippedRect(
      draw_transform(), expanded_clip_in_surface_space);
  return expanded_clip_in_target_space;
}

gfx::Rect RenderSurfaceImpl::CalculateClippedAccumulatedContentRect() {
  if (!ShouldClip() || !is_clipped()) {
    return accumulated_content_rect();
  }

  if (accumulated_content_rect().IsEmpty())
    return gfx::Rect();

  // Calculate projection from the target surface rect to local
  // space. Non-invertible draw transforms means no able to bring clipped rect
  // in target space back to local space, early out without clip.
  gfx::Transform target_to_surface;
  if (!draw_transform().GetInverse(&target_to_surface))
    return accumulated_content_rect();

  // Clip rect is in target space. Bring accumulated content rect to
  // target space in preparation for clipping.
  gfx::Rect accumulated_rect_in_target_space =
      MathUtil::MapEnclosingClippedRect(draw_transform(),
                                        accumulated_content_rect());
  // If accumulated content rect is contained within clip rect, early out
  // without clipping.
  if (clip_rect().Contains(accumulated_rect_in_target_space))
    return accumulated_content_rect();

  gfx::Rect clipped_accumulated_rect_in_target_space;
  if (Filters().HasFilterThatMovesPixels()) {
    clipped_accumulated_rect_in_target_space =
        CalculateExpandedClipForFilters(target_to_surface);
  } else {
    clipped_accumulated_rect_in_target_space = clip_rect();
  }
  clipped_accumulated_rect_in_target_space.Intersect(
      accumulated_rect_in_target_space);

  if (clipped_accumulated_rect_in_target_space.IsEmpty())
    return gfx::Rect();

  gfx::Rect clipped_accumulated_rect_in_local_space =
      MathUtil::ProjectEnclosingClippedRect(
          target_to_surface, clipped_accumulated_rect_in_target_space);
  // Bringing clipped accumulated rect back to local space may result
  // in inflation due to axis-alignment.
  clipped_accumulated_rect_in_local_space.Intersect(accumulated_content_rect());
  return clipped_accumulated_rect_in_local_space;
}

void RenderSurfaceImpl::CalculateContentRectFromAccumulatedContentRect(
    int max_texture_size) {
  // Root render surface use viewport, and does not calculate content rect.
  DCHECK_NE(render_target(), this);

  for (LayerImpl* layer : deferred_contributing_layers_) {
    DCHECK(layer->draws_content());
    accumulated_content_rect_.Union(layer->visible_drawable_content_rect());
    view_transition_capture_content_rect_.Union(
        layer->visible_drawable_content_rect());
  }

  deferred_contributing_layers_.clear();

  // Surface's content rect is the clipped accumulated content rect. By default
  // use accumulated content rect, and then try to clip it.
  gfx::Rect surface_content_rect = CalculateClippedAccumulatedContentRect();

  // Render passes induced for elements participating in a ViewTransition
  // shouldn't be larger than max texture size.
#if DCHECK_IS_ON()
  if (IsViewTransitionElement()) {
    DCHECK_LE(surface_content_rect.width(), max_texture_size);
    DCHECK_LE(surface_content_rect.height(), max_texture_size);
  }
#endif

  // The RenderSurfaceImpl backing texture cannot exceed the maximum supported
  // texture size.
  surface_content_rect.set_width(
      std::min(surface_content_rect.width(), max_texture_size));
  surface_content_rect.set_height(
      std::min(surface_content_rect.height(), max_texture_size));

  SetContentRect(surface_content_rect);

  view_transition_capture_content_rect_.set_width(std::min(
      view_transition_capture_content_rect_.width(), max_texture_size));
  view_transition_capture_content_rect_.set_height(std::min(
      view_transition_capture_content_rect_.height(), max_texture_size));
}

void RenderSurfaceImpl::SetContentRectToViewport() {
  // Only root render surface use viewport as content rect.
  DCHECK_EQ(render_target(), this);
  DCHECK(deferred_contributing_layers_.empty());
  gfx::Rect viewport = gfx::ToEnclosingRect(
      layer_tree_impl_->property_trees()->clip_tree().ViewportClip());
  SetContentRect(viewport);
}

void RenderSurfaceImpl::ClearAccumulatedContentRect() {
  accumulated_content_rect_ = gfx::Rect();
  view_transition_capture_content_rect_ = gfx::Rect();
}

void RenderSurfaceImpl::AccumulateContentRectFromContributingLayer(
    LayerImpl* layer,
    const base::flat_set<blink::ViewTransitionToken>&
        capture_view_transition_tokens) {
  DCHECK(layer->draws_content());
  DCHECK_EQ(this, layer->render_target());

  // Root render surface doesn't accumulate content rect, it always uses
  // viewport for content rect.
  if (render_target() == this)
    return;

  // Contributions from view-transition capture layers are deferred until
  // their surface content rect is computed.
  if (layer->ViewTransitionResourceId().IsValid()) {
    // If the token doesn't match, then we will accumulate the rect later (defer
    // for now). If the token does match, we don't need to accumulate the rect
    // at all since the corresponding ViewTransitionContentLayerImpl won't
    // append its quads and not contribute to the content of this render
    // surface.
    // TODO(vmpstr): This can be simplified when we remove the
    // ViewTransitionContentLayerImpls for capture phase view transitions.
    if (!layer->ViewTransitionResourceId().MatchesToken(
            capture_view_transition_tokens)) {
      deferred_contributing_layers_.push_back(layer);
    }
  } else {
    accumulated_content_rect_.Union(layer->visible_drawable_content_rect());
    view_transition_capture_content_rect_.Union(
        layer->visible_drawable_content_rect());
  }
}

void RenderSurfaceImpl::AccumulateContentRectFromContributingRenderSurface(
    RenderSurfaceImpl* contributing_surface,
    const base::flat_set<blink::ViewTransitionToken>&
        capture_view_transition_tokens) {
  DCHECK_NE(this, contributing_surface);
  DCHECK_EQ(this, contributing_surface->render_target());

  // Root render surface doesn't accumulate content rect, it always uses
  // viewport for content rect.
  if (render_target() == this) {
    return;
  }

  // If the contributing surface is a non-capture view transition surface, then
  // it will drawn independently and not need to contribute to this surface.
  if (contributing_surface->IsViewTransitionElement() &&
      !contributing_surface->ViewTransitionElementResourceId().MatchesToken(
          capture_view_transition_tokens)) {
    return;
  }

  // The content rect of contributing surface is in its own space. Instead, we
  // will use contributing surface's DrawableContentRect which is in target
  // space (local space for this render surface) as required.
  accumulated_content_rect_.Union(
      gfx::ToEnclosedRect(contributing_surface->DrawableContentRect()));

  // Now if contributing surface is a *matching* view transition element,
  // meaning that we're doing a capture, then above we ensure that we can use
  // the content rect to draw the frame, but we don't need that surface to
  // contribute for capture.
  if (contributing_surface->IsViewTransitionElement()) {
    return;
  }

  view_transition_capture_content_rect_.Union(
      gfx::ToEnclosedRect(contributing_surface->DrawableContentRect()));
}

bool RenderSurfaceImpl::SurfacePropertyChanged() const {
  // |surface_property_changed_| is flagged when the clip_rect or content_rect
  // change. As of now, these are the only two properties that can be affected
  // by descendant layers.
  return surface_property_changed_;
}

bool RenderSurfaceImpl::AncestorPropertyChanged() const {
  // All property changes come from the surface's property tree nodes.
  // (or some ancestor node that propagates its change to one of these nodes).
  //
  const PropertyTrees* property_trees = layer_tree_impl_->property_trees();
  return ancestor_property_changed_ || property_trees->full_tree_damaged() ||
         property_trees->transform_tree()
             .Node(TransformTreeIndex())
             ->transform_changed() ||
         property_trees->effect_tree().Node(EffectTreeIndex())->effect_changed;
}

void RenderSurfaceImpl::NoteAncestorPropertyChanged() {
  ancestor_property_changed_ = true;
}

bool RenderSurfaceImpl::HasDamageFromeContributingContent() const {
  return damage_tracker_->has_damage_from_contributing_content();
}

gfx::Rect RenderSurfaceImpl::GetDamageRect() const {
  gfx::Rect damage_rect;
  bool is_valid_rect = damage_tracker_->GetDamageRectIfValid(&damage_rect);
  if (!is_valid_rect) {
    return content_rect();
  }
  return damage_rect;
}

RenderSurfacePropertyChangedFlags RenderSurfaceImpl::GetPropertyChangeFlags()
    const {
  return {surface_property_changed_, ancestor_property_changed_};
}

void RenderSurfaceImpl::ApplyPropertyChangeFlags(
    const RenderSurfacePropertyChangedFlags& flags) {
  surface_property_changed_ = flags.self_changed();
  ancestor_property_changed_ = flags.ancestor_changed();
}

void RenderSurfaceImpl::ResetPropertyChangedFlags() {
  surface_property_changed_ = false;
  ancestor_property_changed_ = false;
}

std::unique_ptr<viz::CompositorRenderPass> RenderSurfaceImpl::CreateRenderPass(
    const base::flat_set<blink::ViewTransitionToken>&
        capture_view_transition_tokens) {
  auto pass = CreateRenderPassCommon(render_pass_id(), content_rect());
  // If we have a subtree_capture_id, it would only be a part of the regular
  // render pass.
  pass->subtree_capture_id = SubtreeCaptureId();
  // We never cache the capture render pass, so it's only specified here.
  pass->cache_render_pass = ShouldCacheRenderSurface();

  // We only set the view transition tokens on a regular pass that don't match
  // the capture phase tokens. The capture phase tokens will only be set on the
  // view transition render pass.
  if (!ViewTransitionElementResourceId().MatchesToken(
          capture_view_transition_tokens)) {
    pass->view_transition_element_resource_id =
        ViewTransitionElementResourceId();
  }
  return pass;
}

std::unique_ptr<viz::CompositorRenderPass>
RenderSurfaceImpl::CreateViewTransitionCaptureRenderPass(
    const base::flat_set<blink::ViewTransitionToken>&
        capture_view_transition_tokens) {
  auto pass = CreateRenderPassCommon(view_transition_capture_render_pass_id(),
                                     view_transition_capture_content_rect_);

  DCHECK(IsViewTransitionElement() ||
         has_view_transition_capture_contributions());
  pass->view_transition_element_resource_id = ViewTransitionElementResourceId();
  return pass;
}

std::unique_ptr<viz::CompositorRenderPass>
RenderSurfaceImpl::CreateRenderPassCommon(viz::CompositorRenderPassId id,
                                          const gfx::Rect& output_rect) {
  std::unique_ptr<viz::CompositorRenderPass> pass =
      viz::CompositorRenderPass::Create(num_contributors_);
  gfx::Rect damage_rect = GetDamageRect();
  damage_rect.Intersect(output_rect);
  pass->SetNew(id, output_rect, damage_rect,
               draw_properties_.screen_space_transform);
  pass->filters = Filters();
  pass->backdrop_filters = BackdropFilters();
  pass->backdrop_filter_bounds = BackdropFilterBounds();
  pass->generate_mipmap = TrilinearFiltering();
  // The subtree size may be slightly larger than our content rect during
  // some animations, so we clamp it here.
  pass->subtree_size = SubtreeSize();
  pass->subtree_size.SetToMin(output_rect.size());
  pass->has_damage_from_contributing_content =
      HasDamageFromeContributingContent();
  return pass;
}

void RenderSurfaceImpl::AppendQuads(const AppendQuadsContext& context,
                                    viz::CompositorRenderPass* render_pass,
                                    AppendQuadsData* append_quads_data) {
  // There are several cases to consider if we are a ViewTransition element:
  // We're doing a regular append (not for capture) and the token matches:
  //   - We should append the quad, because we're producing a regular draw
  //     frame.
  // We're doing a regular append (not for capture) and the token doesn't match:
  //   - We should skip: this is the regular view transition skip in a
  //     non-capture phase.
  // We're doing a for-capture append and the token matches
  //   - We should skip: this is a for capture render pass, so we need to
  //     extract the render pass into its own set without contributing to the
  //     parent.
  // We're doing a for-capture append and the token doesn't match
  //   - We should skip: this is the regular view transition skip in a
  //     non-capture phase.
  // Overall, we only append in the case that we are doing a regular append
  // (not for capture) and the token matches. All other cases return here.
  // Note that we only omit real ViewTransition render passes, since passes that
  // has_view_transition_capture_contributions may also have contributions
  // from non-view-transition items that need to be included.
  if (IsViewTransitionElement() &&
      (context.for_view_transition_capture ||
       !ViewTransitionElementResourceId().MatchesToken(
           context.capture_view_transition_tokens))) {
    return;
  }

  gfx::Rect output_rect = context.for_view_transition_capture &&
                                  has_view_transition_capture_contributions()
                              ? view_transition_capture_content_rect_
                              : content_rect();
  gfx::Rect unoccluded_output_rect =
      occlusion_in_content_space().GetUnoccludedContentRect(output_rect);
  // Contributions to the output rect from a reference filter are not included
  // in the unoccluded_output_rect, so do not skip the quad or the target
  // surface will be missing the reference filter content.
  if (unoccluded_output_rect.IsEmpty() && !Filters().HasReferenceFilter()) {
    return;
  }

  const PropertyTrees* property_trees = layer_tree_impl_->property_trees();
  int sorting_context_id = property_trees->transform_tree()
                               .Node(TransformTreeIndex())
                               ->sorting_context_id;
  bool contents_opaque = false;
  viz::SharedQuadState* shared_quad_state =
      render_pass->CreateAndAppendSharedQuadState();
  std::optional<gfx::Rect> clip_rect;
  if (draw_properties_.is_clipped) {
    clip_rect = draw_properties_.clip_rect;
  }
  shared_quad_state->SetAll(
      draw_transform(), output_rect, output_rect, mask_filter_info(), clip_rect,
      contents_opaque, draw_properties_.draw_opacity, BlendMode(),
      sorting_context_id, layer_id_, is_fast_rounded_corner());

  if (layer_tree_impl_->debug_state().show_debug_borders.test(
          DebugBorderType::RENDERPASS)) {
    auto* debug_border_quad =
        render_pass->CreateAndAppendDrawQuad<viz::DebugBorderDrawQuad>();
    debug_border_quad->SetNew(shared_quad_state, output_rect,
                              unoccluded_output_rect, GetDebugBorderColor(),
                              GetDebugBorderWidth());
  }

  LayerImpl* mask_layer = BackdropMaskLayer();
  viz::ResourceId mask_resource_id = viz::kInvalidResourceId;
  gfx::Size mask_texture_size;
  gfx::RectF mask_uv_rect;
  gfx::Vector2dF surface_contents_scale =
      OwningEffectNode()->surface_contents_scale;
  // Resourceless mode does not support masks.
  if (context.draw_mode != DRAW_MODE_RESOURCELESS_SOFTWARE && mask_layer &&
      mask_layer->draws_content() && !mask_layer->bounds().IsEmpty()) {
    // The software renderer applies mask layer and blending in the wrong
    // order but kDstIn doesn't commute with masking. It is okay to not
    // support this configuration because kDstIn was introduced to replace
    // mask layers.
    DCHECK(BlendMode() != SkBlendMode::kDstIn)
        << "kDstIn blend mode with mask layer is unsupported.";
    TRACE_EVENT1("cc", "RenderSurfaceImpl::AppendQuads",
                 "mask_layer_gpu_memory_usage",
                 mask_layer->GPUMemoryUsageInBytes());

    gfx::SizeF mask_uv_size;
    mask_layer->GetContentsResourceId(&mask_resource_id, &mask_texture_size,
                                      &mask_uv_size);
    gfx::SizeF unclipped_mask_target_size =
        gfx::ScaleSize(gfx::SizeF(mask_layer->bounds()),
                       surface_contents_scale.x(), surface_contents_scale.y());
    gfx::Vector2dF mask_offset = gfx::ScaleVector2d(
        mask_layer->offset_to_transform_parent(), surface_contents_scale.x(),
        surface_contents_scale.y());
    // Convert content_rect from target space to normalized mask UV space.
    // Where |unclipped_mask_target_size| maps to |mask_uv_size|.
    mask_uv_rect = gfx::ScaleRect(
        // Translate output_rect into mask resource's space.
        gfx::RectF(output_rect) - mask_offset,
        mask_uv_size.width() / unclipped_mask_target_size.width(),
        mask_uv_size.height() / unclipped_mask_target_size.height());
  }

  gfx::RectF tex_coord_rect(gfx::Rect(output_rect.size()));
  auto* quad =
      render_pass->CreateAndAppendDrawQuad<viz::CompositorRenderPassDrawQuad>();
  const auto& append_render_pass_id =
      context.for_view_transition_capture &&
              has_view_transition_capture_contributions()
          ? view_transition_capture_render_pass_id()
          : render_pass_id();
  quad->SetAll(
      shared_quad_state, output_rect, unoccluded_output_rect,
      /*needs_blending=*/true, append_render_pass_id, mask_resource_id,
      mask_uv_rect, mask_texture_size, surface_contents_scale, gfx::PointF(),
      tex_coord_rect, !layer_tree_impl_->settings().enable_edge_anti_aliasing,
      OwningEffectNode()->backdrop_filter_quality, intersects_damage_under_);
}

bool RenderSurfaceImpl::ShouldClip() const {
  return !HasCopyRequest() && !ShouldCacheRenderSurface() &&
         !IsViewTransitionElement();
}

}  // namespace cc
