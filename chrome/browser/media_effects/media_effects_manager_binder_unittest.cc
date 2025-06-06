// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media_effects/media_effects_manager_binder.h"

#include "base/test/test_future.h"
#include "components/media_effects/media_effects_service.h"
#include "components/user_prefs/test/test_browser_context_with_prefs.h"
#include "content/public/test/browser_task_environment.h"
#include "media/capture/mojom/video_effects_manager.mojom.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/video_effects/public/cpp/buildflags.h"
#include "services/video_effects/public/cpp/video_effects_service_host.h"
#include "services/video_effects/public/mojom/video_effects_processor.mojom.h"
#include "services/video_effects/public/mojom/video_effects_service.mojom.h"
#include "services/video_effects/test/fake_video_effects_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/insets_f.h"

static_assert(BUILDFLAG(ENABLE_VIDEO_EFFECTS),
              "Requires enable_video_effects to be true");

namespace {
media::mojom::VideoEffectsConfigurationPtr GetConfigurationSync(
    mojo::Remote<media::mojom::ReadonlyVideoEffectsManager>& effect_manager) {
  base::test::TestFuture<media::mojom::VideoEffectsConfigurationPtr>
      output_configuration;
  effect_manager->GetConfiguration(output_configuration.GetCallback());
  return output_configuration.Take();
}
}  // namespace

class MediaEffectsManagerBinderTest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
  user_prefs::TestBrowserContextWithPrefs browser_context_;
};

TEST_F(MediaEffectsManagerBinderTest, BindReadonlyVideoEffectsManager) {
  const char* kDeviceId = "device_id";

  mojo::Remote<media::mojom::ReadonlyVideoEffectsManager>
      readonly_video_effects_manager;
  media_effects::BindReadonlyVideoEffectsManager(
      kDeviceId, &browser_context_,
      readonly_video_effects_manager.BindNewPipeAndPassReceiver());

  // Allow queued device registration to complete.
  base::RunLoop().RunUntilIdle();

  base::WeakPtr<VideoEffectsManagerImpl> video_effects_manager_impl =
      media_effects::GetOrCreateVideoEffectsManager(kDeviceId,
                                                    &browser_context_);
  ASSERT_TRUE(video_effects_manager_impl);

  const float kPaddingRatio = 0.383;
  video_effects_manager_impl->SetConfiguration(
      media::mojom::VideoEffectsConfiguration::New(
          nullptr, nullptr,
          media::mojom::Framing::New(gfx::InsetsF{kPaddingRatio})));

  EXPECT_EQ(kPaddingRatio, GetConfigurationSync(readonly_video_effects_manager)
                               ->framing->padding_ratios.top());
}

TEST_F(MediaEffectsManagerBinderTest, BindVideoEffectsProcessor) {
  // Tests that `media_effects::BindVideoEffectsProcessor()` works, i.e.
  // causes the passed in remote to be connected.

  mojo::Remote<video_effects::mojom::VideoEffectsService> service;
  video_effects::FakeVideoEffectsService fake_effects_service(
      service.BindNewPipeAndPassReceiver());
  auto service_reset =
      video_effects::SetVideoEffectsServiceRemoteForTesting(&service);

  auto effects_processor_future =
      fake_effects_service.GetEffectsProcessorCreationFuture();

  mojo::Remote<video_effects::mojom::VideoEffectsProcessor>
      video_effects_processor;
  media_effects::BindVideoEffectsProcessor(
      "some_device_id", &browser_context_,
      video_effects_processor.BindNewPipeAndPassReceiver());

  EXPECT_TRUE(effects_processor_future.Wait());
  EXPECT_TRUE(video_effects_processor.is_connected());
}
