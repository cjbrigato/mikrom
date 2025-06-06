// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ui_devtools/devtools_server.h"

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/test/test_io_thread.h"
#include "build/build_config.h"
#include "components/ui_devtools/switches.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/address_list.h"
#include "net/base/completion_once_callback.h"
#include "net/base/test_completion_callback.h"
#include "net/server/http_server_request_info.h"
#include "net/socket/tcp_client_socket.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ui_devtools {

class UIDevToolsServerTest : public testing::Test {
 public:
  UIDevToolsServerTest() {
    base::CommandLine::ForCurrentProcess()->AppendSwitch(
        switches::kEnableUiDevTools);
  }
  ~UIDevToolsServerTest() override = default;

  void SetUp() override {
    base::RunLoop run_loop;
    server_ = UiDevToolsServer::CreateForViews(
        io_thread_.task_runner(), /*port=*/0,
        /*active_port_output_directory=*/base::FilePath());
    server_->SetOnSocketConnectedForTesting(run_loop.QuitClosure());
    run_loop.Run();
  }

 protected:
  // To fully test thread-hopping logic, it's important that this not be an IO
  // thread, so any socket IO incorrectly done on the main thread runs into a
  // CHECK failure.
  base::test::TaskEnvironment task_environment_;
  base::TestIOThread io_thread_{base::TestIOThread::kAutoStart};

  std::unique_ptr<UiDevToolsServer> server_;
};

// Tests whether the server for Views is properly created so we can connect to
// it.
TEST_F(UIDevToolsServerTest, ConnectionToViewsServer) {
  // As with the test server itself, TCPClientSocket can only be connected on an
  // IO thread, so have to do all the work here on the IO thread.
  base::RunLoop run_loop;

  // This socket is created and destroyed on the IO thread, but it's simplest to
  // declare it here, since its deleted by a callback that may be invoked
  // asynchronously by its connect method, or calls its Connect method,
  // depending on whether the connection attempt completes synchronously. Having
  // two deletion paths makes BindOnce() not work well with it.
  std::unique_ptr<net::TCPClientSocket> client_socket;

  // Called on the IO thread once connection has completed. Destroys the
  // `client_socket`, which may or may not be the object invoking the callback.
  auto on_connect_complete =
      base::BindLambdaForTesting([&](int connect_result) {
        ASSERT_EQ(connect_result, net::OK);
        ASSERT_TRUE(client_socket->IsConnected());
        client_socket.reset();
        run_loop.Quit();
      });

  io_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          base::BindLambdaForTesting([&](int port) {
            net::AddressList addr(
                net::IPEndPoint(net::IPAddress(127, 0, 0, 1), port));
            client_socket = std::make_unique<net::TCPClientSocket>(
                addr, nullptr, nullptr, nullptr, net::NetLogSource());
            int connect_result = client_socket->Connect(on_connect_complete);
            // On ERR_IO_PENDING, `on_connect_complete` will be invoked
            // asynchronously, so need to let the message loop spin until that
            // happens.
            if (connect_result == net::ERR_IO_PENDING) {
              return;
            }
            // Otherwise, run `on_connect_complete` immediately.
            on_connect_complete.Run(connect_result);
          }),
          // EmbeddedTestServer isn't threadsafe, so have to get the port on the
          // main thread, before posting the task.
          server_->port()));
  run_loop.Run();
}

// Ensure we don't crash from OOB vector access when passed an incorrect
// client ID.
TEST_F(UIDevToolsServerTest, OutOfBoundsClientTest) {
  auto client =
      std::make_unique<UiDevToolsClient>("UiDevToolsClient", server_.get());
  server_->AttachClient(std::move(client));
  // Cast for public `OnWebSocketRequest`.
  net::HttpServerRequestInfo request;
  request.path = "/1";
  server_->OnWebSocketRequestForTesting(0, request);
  request.path = "/42";
  server_->OnWebSocketRequestForTesting(0, request);
  request.path = "/-5000";
  server_->OnWebSocketRequestForTesting(0, request);
}

}  // namespace ui_devtools
