// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/390223051): Remove C-library calls to fix the errors.
#pragma allow_unsafe_libc_calls
#endif

#include "third_party/blink/renderer/core/page/plugin_data.h"

#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/mojom/plugins/plugin_registry.mojom-blink.h"
#include "third_party/blink/renderer/platform/testing/task_environment.h"
#include "third_party/blink/renderer/platform/testing/testing_platform_support.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"

namespace blink {
namespace {

class MockPluginRegistry : public mojom::blink::PluginRegistry {
 public:
  void GetPlugins(bool refresh, GetPluginsCallback callback) override {
    DidGetPlugins(refresh);
    std::move(callback).Run(Vector<mojom::blink::PluginInfoPtr>());
  }

  MOCK_METHOD(void, DidGetPlugins, (bool));
};

TEST(PluginDataTest, UpdatePluginList) {
  test::TaskEnvironment task_environment;
  ScopedTestingPlatformSupport<TestingPlatformSupport> support;

  MockPluginRegistry mock_plugin_registry;
  mojo::Receiver<mojom::blink::PluginRegistry> registry_receiver(
      &mock_plugin_registry);
  TestingPlatformSupport::ScopedOverrideMojoInterface override_plugin_registry(
      WTF::BindRepeating(
          [](mojo::Receiver<mojom::blink::PluginRegistry>* registry_receiver,
             const char* interface, mojo::ScopedMessagePipeHandle pipe) {
            if (!strcmp(interface, mojom::blink::PluginRegistry::Name_)) {
              registry_receiver->Bind(
                  mojo::PendingReceiver<mojom::blink::PluginRegistry>(
                      std::move(pipe)));
              return;
            }
          },
          WTF::Unretained(&registry_receiver)));

  EXPECT_CALL(mock_plugin_registry, DidGetPlugins(false));

  auto* plugin_data = MakeGarbageCollected<PluginData>();
  plugin_data->UpdatePluginList();
}

}  // namespace
}  // namespace blink
