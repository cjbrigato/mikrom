// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/power_monitor/power_observer.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/renderer_host/spare_render_process_host_manager_impl.h"
#include "content/browser/service_host/utility_process_host.h"
#include "content/public/browser/browser_child_process_host.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/gpu_service_registry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_features.h"
#include "content/public/common/process_type.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "content/shell/common/power_monitor_test.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/remote_set.h"
#include "services/device/public/mojom/power_monitor.mojom.h"

namespace content {

namespace {

void VerifyPowerStateInChildProcess(
    mojom::PowerMonitorTest* power_monitor_test,
    base::PowerStateObserver::BatteryPowerStatus expected_state) {
  base::RunLoop run_loop;
  power_monitor_test->QueryNextState(base::BindOnce(
      [](base::RunLoop* loop,
         base::PowerStateObserver::BatteryPowerStatus expected_state,
         base::PowerStateObserver::BatteryPowerStatus battery_power_status) {
        EXPECT_EQ(expected_state, battery_power_status);
        loop->Quit();
      },
      &run_loop, expected_state));
  run_loop.Run();
}

class MockPowerMonitorMessageBroadcaster : public device::mojom::PowerMonitor {
 public:
  MockPowerMonitorMessageBroadcaster() = default;

  MockPowerMonitorMessageBroadcaster(
      const MockPowerMonitorMessageBroadcaster&) = delete;
  MockPowerMonitorMessageBroadcaster& operator=(
      const MockPowerMonitorMessageBroadcaster&) = delete;

  ~MockPowerMonitorMessageBroadcaster() override = default;

  void Bind(mojo::PendingReceiver<device::mojom::PowerMonitor> receiver) {
    receivers_.Add(this, std::move(receiver));
  }

  // device::mojom::PowerMonitor:
  void AddClient(mojo::PendingRemote<device::mojom::PowerMonitorClient>
                     pending_power_monitor_client) override {
    mojo::Remote<device::mojom::PowerMonitorClient> power_monitor_client(
        std::move(pending_power_monitor_client));
    power_monitor_client->PowerStateChange(battery_power_status_);
    clients_.Add(std::move(power_monitor_client));
  }

  void OnPowerStateChange(
      base::PowerStateObserver::BatteryPowerStatus battery_power_status) {
    battery_power_status_ = battery_power_status;
    for (auto& client : clients_)
      client->PowerStateChange(battery_power_status);
  }

 private:
  base::PowerStateObserver::BatteryPowerStatus battery_power_status_ =
      base::PowerStateObserver::BatteryPowerStatus::kUnknown;

  mojo::ReceiverSet<device::mojom::PowerMonitor> receivers_;
  mojo::RemoteSet<device::mojom::PowerMonitorClient> clients_;
};

class PowerMonitorTest : public ContentBrowserTest {
 public:
  PowerMonitorTest() {
    // Intercept PowerMonitor binding requests from all types of child
    // processes.
    RenderProcessHost::InterceptBindHostReceiverForTesting(base::BindRepeating(
        &PowerMonitorTest::BindForRenderer, base::Unretained(this)));
    BrowserChildProcessHost::InterceptBindHostReceiverForTesting(
        base::BindRepeating(&PowerMonitorTest::BindForNonRenderer,
                            base::Unretained(this)));
  }

  PowerMonitorTest(const PowerMonitorTest&) = delete;
  PowerMonitorTest& operator=(const PowerMonitorTest&) = delete;

  ~PowerMonitorTest() override {
    RenderProcessHost::InterceptBindHostReceiverForTesting(
        base::NullCallback());
    BrowserChildProcessHost::InterceptBindHostReceiverForTesting(
        base::NullCallback());
  }

  void BindForRenderer(ChildProcessId render_process_id,
                       mojo::GenericPendingReceiver* receiver) {
    auto r = receiver->As<device::mojom::PowerMonitor>();
    if (!r)
      return;

    GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(&PowerMonitorTest::BindForRendererOnMainThread,
                       base::Unretained(this), std::move(r),
                       render_process_id));
  }

  void BindForNonRenderer(BrowserChildProcessHost* process_host,
                          mojo::GenericPendingReceiver* receiver) {
    auto r = receiver->As<device::mojom::PowerMonitor>();
    if (!r)
      return;

    GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(&PowerMonitorTest::BindForNonRendererOnMainThread,
                       base::Unretained(this),
                       process_host->GetData().process_type, std::move(r)));
  }

 protected:
  void StartUtilityProcess(
      mojo::Remote<mojom::PowerMonitorTest>* power_monitor_test,
      base::OnceClosure utility_bound_closure) {
    utility_bound_closure_ = std::move(utility_bound_closure);

    UtilityProcessHost::Start(
        UtilityProcessHost::Options()
            .WithMetricsName("test_process")
            .WithName(u"TestProcess")
            .WithBoundReceiverOnChildProcessForTesting(
                power_monitor_test->BindNewPipeAndPassReceiver())
            .Pass());
  }

  void set_renderer_bound_closure(base::OnceClosure closure) {
    renderer_bound_closure_ = std::move(closure);
  }

  void set_gpu_bound_closure(base::OnceClosure closure) {
    gpu_bound_closure_ = std::move(closure);
  }

  int request_count_from_renderer() { return request_count_from_renderer_; }
  int request_count_from_utility() { return request_count_from_utility_; }
  int request_count_from_gpu() { return request_count_from_gpu_; }

  void SimulatePowerStateChange(
      base::PowerStateObserver::BatteryPowerStatus battery_power_status) {
    power_monitor_message_broadcaster_.OnPowerStateChange(battery_power_status);
  }

 private:
  void BindForRendererOnMainThread(
      mojo::PendingReceiver<device::mojom::PowerMonitor> receiver,
      ChildProcessId render_process_id) {
    // We can receive binding requests for the spare RenderProcessHost -- this
    // might happen before the test has provided the |renderer_bound_closure_|.
    auto* render_process_host =
        content::RenderProcessHost::FromID(render_process_id);
    if (!render_process_host || render_process_host->IsSpare()) {
      return;
    }

    ASSERT_TRUE(renderer_bound_closure_);
    ++request_count_from_renderer_;
    std::move(renderer_bound_closure_).Run();

    power_monitor_message_broadcaster_.Bind(std::move(receiver));
  }

  void BindForNonRendererOnMainThread(
      int process_type,
      mojo::PendingReceiver<device::mojom::PowerMonitor> receiver) {
    if (process_type == PROCESS_TYPE_UTILITY) {
      if (utility_bound_closure_) {
        ++request_count_from_utility_;
        std::move(utility_bound_closure_).Run();
      }
    } else if (process_type == PROCESS_TYPE_GPU) {
      ++request_count_from_gpu_;

      // We ignore null gpu_bound_closure_ here for two possible scenarios:
      //  - TestRendererProcess and TestUtilityProcess also result in spinning
      //    up GPU processes as a side effect, but they do not set valid
      //    gpu_bound_closure_.
      //  - As GPU process is started during setup of browser test suite, so
      //    it's possible that TestGpuProcess execution may have not started
      //    yet when the PowerMonitor bind request comes here, in such case
      //    gpu_bound_closure_ will also be null.
      if (gpu_bound_closure_)
        std::move(gpu_bound_closure_).Run();
    }

    power_monitor_message_broadcaster_.Bind(std::move(receiver));
  }

  int request_count_from_renderer_ = 0;
  int request_count_from_utility_ = 0;
  int request_count_from_gpu_ = 0;
  base::OnceClosure renderer_bound_closure_;
  base::OnceClosure gpu_bound_closure_;
  base::OnceClosure utility_bound_closure_;

  MockPowerMonitorMessageBroadcaster power_monitor_message_broadcaster_;
};

IN_PROC_BROWSER_TEST_F(PowerMonitorTest, TestRendererProcess) {
  ASSERT_EQ(0, request_count_from_renderer());
  base::RunLoop run_loop;
  set_renderer_bound_closure(run_loop.QuitClosure());
  ASSERT_TRUE(NavigateToURL(shell(), GetTestUrl(".", "simple_page.html")));
  run_loop.Run();
  EXPECT_EQ(1, request_count_from_renderer());

  mojo::Remote<mojom::PowerMonitorTest> power_monitor_renderer;
  RenderProcessHost* rph =
      shell()->web_contents()->GetPrimaryMainFrame()->GetProcess();
  rph->BindReceiver(power_monitor_renderer.BindNewPipeAndPassReceiver());

  // Ensure that the PowerMonitorTestImpl instance has been created and is
  // observing power state changes in the child process before simulating a
  // power state change.
  power_monitor_renderer.FlushForTesting();

  SimulatePowerStateChange(
      base::PowerStateObserver::BatteryPowerStatus::kBatteryPower);
  // Verify renderer process battery_power_status changed to battery power.
  VerifyPowerStateInChildProcess(
      power_monitor_renderer.get(),
      base::PowerStateObserver::BatteryPowerStatus::kBatteryPower);

  SimulatePowerStateChange(
      base::PowerStateObserver::BatteryPowerStatus::kExternalPower);
  // Verify renderer process battery_power_status changed to external power.
  VerifyPowerStateInChildProcess(
      power_monitor_renderer.get(),
      base::PowerStateObserver::BatteryPowerStatus::kExternalPower);

  SimulatePowerStateChange(
      base::PowerStateObserver::BatteryPowerStatus::kUnknown);
  // Verify renderer process battery_power_status becomes unknown.
  VerifyPowerStateInChildProcess(
      power_monitor_renderer.get(),
      base::PowerStateObserver::BatteryPowerStatus::kUnknown);
}

IN_PROC_BROWSER_TEST_F(PowerMonitorTest, TestUtilityProcess) {
  mojo::Remote<mojom::PowerMonitorTest> power_monitor_utility;

  ASSERT_EQ(0, request_count_from_utility());
  base::RunLoop run_loop;
  StartUtilityProcess(&power_monitor_utility, run_loop.QuitClosure());
  run_loop.Run();
  EXPECT_EQ(1, request_count_from_utility());

  // Ensure that the PowerMonitorTestImpl instance has been created and is
  // observing power state changes in the child process before simulating a
  // power state change.
  power_monitor_utility.FlushForTesting();

  SimulatePowerStateChange(
      base::PowerStateObserver::BatteryPowerStatus::kBatteryPower);
  // Verify renderer process battery_power_status changed to battery power.
  VerifyPowerStateInChildProcess(
      power_monitor_utility.get(),
      base::PowerStateObserver::BatteryPowerStatus::kBatteryPower);

  SimulatePowerStateChange(
      base::PowerStateObserver::BatteryPowerStatus::kExternalPower);
  // Verify renderer process battery_power_status changed to external power.
  VerifyPowerStateInChildProcess(
      power_monitor_utility.get(),
      base::PowerStateObserver::BatteryPowerStatus::kExternalPower);
}

IN_PROC_BROWSER_TEST_F(PowerMonitorTest, TestGpuProcess) {
  // As gpu process is started automatically during the setup period of browser
  // test suite, it may have already started and bound PowerMonitor interface to
  // Device Service before execution of this TestGpuProcess test. So here we
  // do not wait for the connection if we found it has already been established.
  if (request_count_from_gpu() != 1) {
    ASSERT_EQ(0, request_count_from_gpu());
    base::RunLoop run_loop;
    set_gpu_bound_closure(run_loop.QuitClosure());
    // Wait for the connection from gpu process.
    run_loop.Run();
  }
  EXPECT_EQ(1, request_count_from_gpu());

  mojo::Remote<mojom::PowerMonitorTest> power_monitor_gpu;
  BindInterfaceInGpuProcess(power_monitor_gpu.BindNewPipeAndPassReceiver());

  // Ensure that the PowerMonitorTestImpl instance has been created and is
  // observing power state changes in the child process before simulating a
  // power state change.
  power_monitor_gpu.FlushForTesting();

  SimulatePowerStateChange(
      base::PowerStateObserver::BatteryPowerStatus::kBatteryPower);
  // Verify gpu process battery_power_status changed to battery power.
  VerifyPowerStateInChildProcess(
      power_monitor_gpu.get(),
      base::PowerStateObserver::BatteryPowerStatus::kBatteryPower);

  SimulatePowerStateChange(
      base::PowerStateObserver::BatteryPowerStatus::kExternalPower);
  // Verify gpu process battery_power_status changed to external power.
  VerifyPowerStateInChildProcess(
      power_monitor_gpu.get(),
      base::PowerStateObserver::BatteryPowerStatus::kExternalPower);
}

}  //  namespace

}  //  namespace content
