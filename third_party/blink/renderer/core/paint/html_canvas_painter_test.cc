// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/html_canvas_painter.h"

#include <memory>
#include <utility>

#include "cc/layers/layer.h"
#include "components/viz/test/test_context_provider.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/html/canvas/canvas_context_creation_attributes_core.h"
#include "third_party/blink/renderer/core/html/canvas/canvas_rendering_context.h"
#include "third_party/blink/renderer/core/html/canvas/html_canvas_element.h"
#include "third_party/blink/renderer/core/paint/paint_controller_paint_test.h"
#include "third_party/blink/renderer/platform/graphics/canvas_resource_provider.h"
#include "third_party/blink/renderer/platform/graphics/gpu/shared_gpu_context.h"
#include "third_party/blink/renderer/platform/graphics/test/gpu_test_utils.h"
#include "third_party/blink/renderer/platform/graphics/web_graphics_context_3d_provider_wrapper.h"

#include "third_party/blink/renderer/core/scroll/scrollbar_theme.h"

// Integration tests of canvas painting code.

namespace blink {

namespace {

class AcceleratedCompositingTestPlatform
    : public blink::TestingPlatformSupport {
 public:
  bool IsGpuCompositingDisabled() const override { return false; }
};

}  // namespace

class HTMLCanvasPainterTest : public PaintControllerPaintTestBase {
 protected:
  void SetUp() override {
    accelerated_compositing_scope_ = std::make_unique<
        ScopedTestingPlatformSupport<AcceleratedCompositingTestPlatform>>();
    test_context_provider_ = viz::TestContextProvider::Create();
    InitializeSharedGpuContextGLES2(test_context_provider_.get());
    PaintControllerPaintTestBase::SetUp();
  }

  void TearDown() override {
    PaintControllerPaintTestBase::TearDown();
    SharedGpuContext::Reset();
    accelerated_compositing_scope_ = nullptr;
  }

  FrameSettingOverrideFunction SettingOverrider() const override {
    return [](Settings& settings) {
      // LayoutHTMLCanvas doesn't exist if script is disabled.
      settings.SetScriptEnabled(true);
    };
  }

  bool HasLayerAttached(const cc::Layer& layer) {
    return GetChromeClient().HasLayer(layer);
  }

 private:
  scoped_refptr<viz::TestContextProvider> test_context_provider_;
  std::unique_ptr<
      ScopedTestingPlatformSupport<AcceleratedCompositingTestPlatform>>
      accelerated_compositing_scope_;
};

TEST_F(HTMLCanvasPainterTest, Canvas2DLayerAppearsInLayerTree) {
  // Insert a <canvas> and force it into accelerated mode.
  // Not using SetBodyInnerHTML() because we need to test before document
  // lifecyle update.
  GetDocument().body()->setInnerHTML("<canvas width=300 height=200>");
  auto* element = To<HTMLCanvasElement>(GetDocument().body()->firstChild());
  CanvasContextCreationAttributesCore attributes;
  attributes.alpha = true;
  CanvasRenderingContext* context =
      element->GetCanvasRenderingContext("2d", attributes);
  element->GetOrCreateCanvasResourceProviderForCanvas2D();
  ASSERT_EQ(context, element->RenderingContext());

  // Force the page to paint.
  element->PreFinalizeFrame();
  context->FinalizeFrame(FlushReason::kTesting);
  element->PostFinalizeFrame(FlushReason::kTesting);
  UpdateAllLifecyclePhasesForTest();

  ASSERT_TRUE(context->IsComposited());
  ASSERT_TRUE(element->IsAccelerated());

  // Fetch the layer associated with the <canvas>, and check that it was
  // correctly configured in the layer tree.
  const cc::Layer* layer = context->CcLayer();
  ASSERT_TRUE(layer);
  EXPECT_TRUE(HasLayerAttached(*layer));
  EXPECT_EQ(gfx::Size(300, 200), layer->bounds());
}

}  // namespace blink
