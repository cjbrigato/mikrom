// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/fast_ink/fast_ink_host.h"

#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "ash/fast_ink/fast_ink_host_test_api.h"
#include "ash/frame_sink/frame_sink_holder.h"
#include "ash/frame_sink/test/frame_sink_host_test_base.h"
#include "ash/frame_sink/test/test_begin_frame_source.h"
#include "ash/frame_sink/test/test_layer_tree_frame_sink.h"
#include "ash/frame_sink/ui_resource.h"
#include "ash/frame_sink/ui_resource_manager.h"
#include "ash/test/ash_test_base.h"
#include "ash/test/ash_test_helper.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "cc/base/math_util.h"
#include "components/viz/common/quads/compositor_frame.h"
#include "components/viz/common/quads/compositor_render_pass.h"
#include "components/viz/common/quads/texture_draw_quad.h"
#include "components/viz/common/resources/resource_id.h"
#include "gpu/command_buffer/client/client_shared_image.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/display/display_switches.h"
#include "ui/display/screen.h"
#include "ui/gfx/buffer_types.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/widget/widget.h"

namespace ash {
namespace {

class FastInkHostTest
    : public FrameSinkHostTestBase<FastInkHost>,
      public ::testing::WithParamInterface<
          std::tuple<std::string, bool, gfx::Rect, gfx::Rect, gfx::Rect>> {
 public:
  FastInkHostTest()
      : first_display_specs_(std::get<0>(GetParam())),
        auto_update_(std::get<1>(GetParam())),
        content_rect_(std::get<2>(GetParam())),
        expected_quad_rect_(std::get<3>(GetParam())),
        expected_quad_layer_rect_(std::get<4>(GetParam())) {}

  FastInkHostTest(const FastInkHostTest&) = delete;
  FastInkHostTest& operator=(const FastInkHostTest&) = delete;

  ~FastInkHostTest() override = default;

  // AshTestBase:
  void SetUp() override {
    SetDisplaySpecs(first_display_specs_);
    FrameSinkHostTestBase<FastInkHost>::SetUp();
  }

 protected:
  std::string first_display_specs_;
  bool auto_update_ = false;
  gfx::Rect content_rect_;
  gfx::Rect expected_quad_rect_;
  gfx::Rect expected_quad_layer_rect_;
};

TEST_P(FastInkHostTest, CorrectFrameSubmittedToLayerTreeFrameSink) {
  // Request the first frame.
  OnBeginFrame();

  SCOPED_TRACE(base::StringPrintf(
      "Test params: first_display_specs=%s | auto_update=%s | content_rect=%s "
      "| expected_quad_rect=%s | expected_quad_layer_rect=%s",
      first_display_specs_.c_str(), auto_update_ ? "true" : "false",
      content_rect_.ToString().c_str(), expected_quad_rect_.ToString().c_str(),
      expected_quad_layer_rect_.ToString().c_str()));

  constexpr gfx::Rect kTestTotalDamageRectInDIP = gfx::Rect(0, 0, 50, 25);

  if (auto_update_) {
    frame_sink_host()->AutoUpdateSurface(content_rect_,
                                         kTestTotalDamageRectInDIP);

    // When host auto updates, it only submits a frame when requested by
    // LayerTreeFrameSink via a BeginFrameSource.
    OnBeginFrame();
  } else {
    frame_sink_host()->UpdateSurface(content_rect_, kTestTotalDamageRectInDIP,
                                     /*synchronous_draw=*/true);
  }

  const viz::CompositorFrame& frame =
      layer_tree_frame_sink()->GetLatestReceivedFrame();

  const viz::CompositorRenderPassList& render_pass_list =
      frame.render_pass_list;

  ASSERT_EQ(render_pass_list.size(), 1u);
  auto& quad_list = render_pass_list.front()->quad_list;

  ASSERT_EQ(quad_list.size(), 1u);

  viz::DrawQuad* quad = quad_list.back();
  EXPECT_EQ(quad->material, viz::DrawQuad::Material::kTextureContent);

  EXPECT_EQ(quad->rect, expected_quad_rect_);
  EXPECT_EQ(quad->visible_rect, expected_quad_rect_);

  ASSERT_EQ(render_pass_list.front()->shared_quad_state_list.size(), 1u);
  auto* shared_quad_state =
      render_pass_list.front()->shared_quad_state_list.front();

  EXPECT_EQ(shared_quad_state->quad_layer_rect, expected_quad_layer_rect_);
  EXPECT_EQ(shared_quad_state->visible_quad_layer_rect,
            expected_quad_layer_rect_);

  EXPECT_EQ(frame.resource_list.back().is_overlay_candidate, auto_update_);
}

TEST_P(FastInkHostTest, RecreateGpuBufferOnLosingFrameSink) {
  FastInkHostTestApi fast_ink_host_test(frame_sink_host());

  // Buffer is not initialized when there is no begin frame received.
  ASSERT_FALSE(fast_ink_host_test.client_shared_image());

  // Request the first frame. It will call
  // `FrameSinkHost::OnFirstFrameRequested()` initializing the GPU buffer.
  OnBeginFrame();

  // MappableSI should be initialized after receiving the first begin frame.
  ASSERT_TRUE(fast_ink_host_test.client_shared_image());

  // A new frame-sink will be created. FastInkHost should also create a new
  // shared image.
  frame_sink_host()
      ->frame_sink_holder_for_testing()
      ->DidLoseLayerTreeFrameSink();

  // MappableSI should be destroyed after losing a frame sink.
  EXPECT_FALSE(fast_ink_host_test.client_shared_image());

  // A new MappableSI should be initialized once
  // `FrameSinkHost::OnFirstFrameRequested()` is called for the new
  // FrameSinkHolder.
  OnBeginFrame();

  EXPECT_TRUE(fast_ink_host_test.client_shared_image());
}

TEST_P(FastInkHostTest, DelayPaintingUntilReceivingFirstBeginFrame) {
  FastInkHostTestApi fast_ink_host_test(frame_sink_host());

  // Buffer is not initialized when there is no begin frame received.
  ASSERT_FALSE(fast_ink_host_test.client_shared_image());
  EXPECT_EQ(fast_ink_host_test.pending_bitmaps_size(), 0);

  int pending_bitmaps_size = 0;
  for (SkColor color : {SK_ColorRED, SK_ColorYELLOW, SK_ColorGREEN}) {
    {
      const gfx::Rect damage_rect_in_window =
          gfx::Rect(host_window()->bounds().size());
      auto paint = frame_sink_host()->CreateScopedPaint(damage_rect_in_window);
      paint->canvas().DrawRect(gfx::RectF(damage_rect_in_window), color);
    }
    // The bitmap is waiting to be drawn because no gpu memory buffer is
    // initialized.
    ++pending_bitmaps_size;
    EXPECT_EQ(fast_ink_host_test.pending_bitmaps_size(), pending_bitmaps_size);
  }

  // Request the first frame.
  OnBeginFrame();

  // MappableSI should be initialized after receiving the first begin frame.
  ASSERT_TRUE(fast_ink_host_test.client_shared_image());
  // Pending bitmaps should be drawn and cleared.
  EXPECT_EQ(fast_ink_host_test.pending_bitmaps_size(), 0);

  auto mapping = fast_ink_host_test.client_shared_image()->Map();
  ASSERT_TRUE(mapping);
  // Pending bitmaps should be correctly copied to the MappableSI's buffer.
  EXPECT_EQ(*reinterpret_cast<SkColor*>(mapping->GetMemoryForPlane(0).data()),
            SK_ColorGREEN);
}

INSTANTIATE_TEST_SUITE_P(
    /* no prefix */,
    FastInkHostTest,
    testing::Values(
        // When auto updating surface, we update the full surface, ignoring the
        // content_rect.
        std::make_tuple(
            /*first_display_specs=*/"1000x500",
            /*auto_update=*/true,
            /*content_rect=*/gfx::Rect(10, 10),
            /*expected_quad_rect=*/gfx::Rect(0, 0, 1000, 500),
            /*expected_quad_layer_rect=*/gfx::Rect(0, 0, 1000, 500)),
        std::make_tuple(
            /*first_display_specs=*/"1000x500*2",
            /*auto_update=*/true,
            /*content_rect=*/gfx::Rect(10, 10),
            /*expected_quad_rect=*/gfx::Rect(0, 0, 1000, 500),
            /*expected_quad_layer_rect=*/gfx::Rect(0, 0, 1000, 500)),
        std::make_tuple(
            /*first_display_specs=*/"1000x500*2/r",
            /*auto_update=*/true,
            /*content_rect=*/gfx::Rect(10, 10),
            /*expected_quad_rect=*/gfx::Rect(0, 0, 1000, 500),
            /*expected_quad_layer_rect=*/gfx::Rect(0, 0, 500, 1000)),
        // When auto updating is off, we update the surface enclosed by
        // content_rect.
        std::make_tuple(
            /*first_display_specs=*/"1000x500",
            /*auto_update=*/false,
            /*content_rect=*/gfx::Rect(10, 10),
            /*expected_quad_rect=*/gfx::Rect(0, 0, 10, 10),
            /*expected_quad_layer_rect=*/gfx::Rect(0, 0, 1000, 500)),
        std::make_tuple(
            /*first_display_specs=*/"1000x500*2",
            /*auto_update=*/false,
            /*content_rect=*/gfx::Rect(10, 10),
            /*expected_quad_rect=*/gfx::Rect(0, 0, 20, 20),
            /*expected_quad_layer_rect=*/gfx::Rect(0, 0, 1000, 500)),
        std::make_tuple(
            /*first_display_specs=*/"1000x500*2/l",
            /*auto_update=*/false,
            /*content_rect=*/gfx::Rect(10, 15),
            /*expected_quad_rect=*/gfx::Rect(0, 480, 30, 20),
            /*expected_quad_layer_rect=*/gfx::Rect(0, 0, 500, 1000)),
        // If content rect is partially outside of the buffer, quad rect is
        // clipped by buffer size.
        std::make_tuple(
            /*first_display_specs=*/"1000x500",
            /*auto_update=*/false,
            /*content_rect=*/gfx::Rect(995, 0, 10, 10),
            /*expected_quad_rect=*/gfx::Rect(995, 0, 5, 10),
            /*expected_quad_layer_rect=*/gfx::Rect(0, 0, 1000, 500)),
        std::make_tuple(
            /*first_display_specs=*/"3000x2000*2.252252",
            /*auto_update=*/false,
            /*content_rect=*/gfx::Rect(0, 0, 1332, 888),
            /*expected_quad_rect=*/gfx::Rect(0, 0, 3000, 2000),
            /*expected_quad_layer_rect=*/gfx::Rect(0, 0, 3000, 2000))));

}  // namespace
}  // namespace ash
