// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/check_op.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/notreached.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "build/build_config.h"
#include "chrome/browser/content_settings/cookie_settings_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/login/login_handler.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/web_applications/isolated_web_apps/test/isolated_web_app_builder.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/common/content_settings_metadata.h"
#include "components/content_settings/core/common/features.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "net/base/features.h"
#include "net/base/network_isolation_key.h"
#include "net/cookies/site_for_cookies.h"
#include "net/dns/mock_host_resolver.h"
#include "net/storage_access_api/status.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/install_default_websocket_handlers.h"
#include "net/test/embedded_test_server/register_basic_auth_handler.h"
#include "net/test/test_data_directory.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/websocket.mojom.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace {

using testing::HasSubstr;
using testing::Not;

constexpr char kHostA[] = "a.test";
constexpr char kHostB[] = "b.test";

class WebSocketBrowserTest : public InProcessBrowserTest {
 public:
  explicit WebSocketBrowserTest(net::EmbeddedTestServer::ServerCertificate
                                    cert = net::EmbeddedTestServer::CERT_OK)
      : ws_server_(net::EmbeddedTestServer::TYPE_HTTP),
        wss_server_(net::EmbeddedTestServer::TYPE_HTTPS) {
    // Set SSL configuration for the secure WebSocket server.
    wss_server_.SetSSLConfig(cert);

    // Install default WebSocket handlers for both HTTP and HTTPS servers.
    net::test_server::InstallDefaultWebSocketHandlers(
        &ws_server_, /*serve_websocket_test_data=*/true);
    net::test_server::InstallDefaultWebSocketHandlers(
        &wss_server_, /*serve_websocket_test_data=*/true);
  }

  WebSocketBrowserTest(const WebSocketBrowserTest&) = delete;
  WebSocketBrowserTest& operator=(const WebSocketBrowserTest&) = delete;

 protected:
  void NavigateToHTTPPage(const std::string& path) {
    // Visit a HTTP page for testing.
    const GURL url = ws_server_.GetURL(path);
    ASSERT_TRUE(url.SchemeIs("http"));
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  }

  void NavigateToHTTPSPage(const std::string& path) {
    // Visit a HTTPS page for testing.
    const GURL url = wss_server_.GetURL(path);
    ASSERT_TRUE(url.SchemeIs("https"));
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  }

  void NavigateToPath(const std::string& relative) {
    base::FilePath path;
    EXPECT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &path));
    path =
        path.Append(net::GetWebSocketTestDataDirectory()).AppendASCII(relative);
    GURL url(std::string("file://") + path.MaybeAsASCII());
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  }

  // Prepare the title watcher.
  void SetUpOnMainThread() override {
    watcher_ = std::make_unique<content::TitleWatcher>(
        browser()->tab_strip_model()->GetActiveWebContents(), u"PASS");
    watcher_->AlsoWaitForTitle(u"FAIL");
    host_resolver()->AddRule("a.test", "127.0.0.1");
  }

  void AlsoWaitForTitle(const std::u16string& title) {
    watcher_->AlsoWaitForTitle(title);
  }

  void TearDownOnMainThread() override { watcher_.reset(); }

  std::string WaitAndGetTitle() {
    return base::UTF16ToUTF8(watcher_->WaitAndGetTitle());
  }

  // Triggers a WebSocket connection to the given |url| in the context of the
  // main frame of the active WebContents.
  void MakeWebSocketConnection(
      const GURL& url,
      mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
          handshake_client) {
    content::RenderFrameHost* const frame = browser()
                                                ->tab_strip_model()
                                                ->GetActiveWebContents()
                                                ->GetPrimaryMainFrame();
    content::RenderProcessHost* const process = frame->GetProcess();

    const std::vector<std::string> requested_protocols;
    const net::SiteForCookies site_for_cookies;
    // The actual value of this doesn't actually matter, it just can't be empty,
    // to avoid a DCHECK.
    const net::IsolationInfo isolation_info =
        net::IsolationInfo::CreateForInternalRequest(url::Origin::Create(url));
    std::vector<network::mojom::HttpHeaderPtr> additional_headers;
    const url::Origin origin;

    process->GetStoragePartition()->GetNetworkContext()->CreateWebSocket(
        url, requested_protocols, site_for_cookies,
        net::StorageAccessApiStatus::kNone, isolation_info,
        std::move(additional_headers), process->GetDeprecatedID(), origin,
        network::mojom::kWebSocketOptionNone,
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS),
        std::move(handshake_client),
        process->GetStoragePartition()->CreateURLLoaderNetworkObserverForFrame(
            process->GetDeprecatedID(), frame->GetRoutingID()),
        /*auth_handler=*/mojo::NullRemote(),
        /*header_client=*/mojo::NullRemote(),
        /*throttling_profile_id=*/std::nullopt);
  }

  void SetBlockThirdPartyCookies(bool blocked) {
    browser()->profile()->GetPrefs()->SetInteger(
        prefs::kCookieControlsMode,
        static_cast<int>(
            blocked ? content_settings::CookieControlsMode::kBlockThirdParty
                    : content_settings::CookieControlsMode::kOff));
  }

  net::EmbeddedTestServer ws_server_;
  net::EmbeddedTestServer wss_server_;

 private:
  std::unique_ptr<content::TitleWatcher> watcher_;
};

class WebSocketBrowserTestWithAllowFileAccessFromFiles
    : public WebSocketBrowserTest {
 private:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(switches::kAllowFileAccessFromFiles);

    WebSocketBrowserTest::SetUpCommandLine(command_line);
  }
};

// Framework for tests using the connect_to.html page served by a separate HTTP
// or HTTPS server.
class WebSocketBrowserConnectToTest : public WebSocketBrowserTest {
 protected:
  explicit WebSocketBrowserConnectToTest(
      net::EmbeddedTestServer::ServerCertificate cert =
          net::EmbeddedTestServer::CERT_OK)
      : WebSocketBrowserTest(cert) {}

  // The title watcher and HTTP server are set up automatically by the test
  // framework. Each test case still needs to configure and start the
  // WebSocket server(s) it needs.
  void SetUpOnMainThread() override {
    server().ServeFilesFromSourceDirectory(
        net::GetWebSocketTestDataDirectory());
    WebSocketBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(server().Start());
  }

  // Supply a ws: or wss: URL to connect to. Serves connect_to.html from the
  // server's default host.
  void ConnectTo(const GURL& url) {
    ConnectTo(server().base_url().host(), url);
  }

  // Supply a ws: or wss: URL to connect to via loading `host`/connect_to.html.
  void ConnectTo(const std::string& host, const GURL& url) {
    ASSERT_TRUE(server().Started());
    std::string query("url=" + url.spec());
    GURL::Replacements replacements;
    replacements.SetQueryStr(query);
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), server()
                       .GetURL(host, "/connect_to.html")
                       .ReplaceComponents(replacements)));
  }

  virtual net::EmbeddedTestServer& server() = 0;
};

// Websocket upgrades can't happen when only top-level navigations are
// upgraded, so disable the feature for these tests.
class WebSocketBrowserTestWebSocketHSTS : public WebSocketBrowserTest {
 public:
  WebSocketBrowserTestWebSocketHSTS() {
    feature_list_.InitAndDisableFeature(
        net::features::kHstsTopLevelNavigationsOnly);
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

// Concrete impl for tests that use connect_to.html over HTTP.
class WebSocketBrowserHTTPConnectToTest : public WebSocketBrowserConnectToTest {
 protected:
  net::EmbeddedTestServer& server() override { return http_server_; }

  net::EmbeddedTestServer http_server_;
};

// Concrete impl for tests that use connect_to.html over HTTPS.
class WebSocketBrowserHTTPSConnectToTest
    : public WebSocketBrowserConnectToTest {
 protected:
  explicit WebSocketBrowserHTTPSConnectToTest(
      net::EmbeddedTestServer::ServerCertificate cert =
          net::EmbeddedTestServer::CERT_OK)
      : WebSocketBrowserConnectToTest(cert),
        https_server_(net::test_server::EmbeddedTestServer::TYPE_HTTPS) {}

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    server().SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
    WebSocketBrowserConnectToTest::SetUpOnMainThread();
  }

  net::EmbeddedTestServer& server() override { return https_server_; }

  net::EmbeddedTestServer https_server_;
};

class WebSocketBrowserHTTPSConnectToTestPre3pcd
    : public WebSocketBrowserHTTPSConnectToTest {
  void SetUp() override {
    feature_list_.InitAndDisableFeature(
        content_settings::features::kTrackingProtection3pcd);
    WebSocketBrowserHTTPSConnectToTest::SetUp();
  }
  base::test::ScopedFeatureList feature_list_;
};

// Test that the browser can handle a WebSocket frame split into multiple TCP
// segments.
IN_PROC_BROWSER_TEST_F(WebSocketBrowserTest, WebSocketSplitSegments) {
  // Launch a WebSocket server.
  ASSERT_TRUE(ws_server_.Start());

  NavigateToHTTPPage("/split_packet_check.html");

  EXPECT_EQ("PASS", WaitAndGetTitle());
}

IN_PROC_BROWSER_TEST_F(WebSocketBrowserTest, SecureWebSocketSplitRecords) {
  // Launch a secure WebSocket server.
  ASSERT_TRUE(wss_server_.Start());

  NavigateToHTTPSPage("/split_packet_check.html");

  EXPECT_EQ("PASS", WaitAndGetTitle());
}

IN_PROC_BROWSER_TEST_F(WebSocketBrowserTest, SendCloseFrameWhenTabIsClosed) {
  // Launch a WebSocket server.
  ASSERT_TRUE(ws_server_.Start());

  {
    // Create a new tab, establish a WebSocket connection and close the tab.
    content::WebContents* tab =
        browser()->tab_strip_model()->GetActiveWebContents();
    std::unique_ptr<content::WebContents> new_tab =
        content::WebContents::Create(
            content::WebContents::CreateParams(tab->GetBrowserContext()));
    content::WebContents* raw_new_tab = new_tab.get();
    browser()->tab_strip_model()->AppendWebContents(std::move(new_tab), true);
    ASSERT_EQ(raw_new_tab, browser()->tab_strip_model()->GetWebContentsAt(1));

    content::TitleWatcher connected_title_watcher(raw_new_tab, u"CONNECTED");
    connected_title_watcher.AlsoWaitForTitle(u"CLOSED");
    NavigateToHTTPPage("/connect_and_be_observed.html");
    const std::u16string result = connected_title_watcher.WaitAndGetTitle();
    EXPECT_TRUE(base::EqualsASCII(result, "CONNECTED"));

    content::WebContentsDestroyedWatcher destroyed_watcher(raw_new_tab);
    browser()->tab_strip_model()->CloseWebContentsAt(1, 0);
    destroyed_watcher.Wait();
  }

  NavigateToHTTPPage("/close_observer.html");
  EXPECT_EQ("PASS", WaitAndGetTitle());
}

IN_PROC_BROWSER_TEST_F(WebSocketBrowserTest, WebSocketBasicAuthInHTTPURL) {
  // Launch a basic-auth-protected WebSocket server.
  net::test_server::RegisterBasicAuthHandler(ws_server_, "test", "test");
  ASSERT_TRUE(ws_server_.Start());

  // Open connect_check.html via HTTP with credentials in the URL.
  GURL::Replacements replacements;
  replacements.SetSchemeStr("http");

  GURL url = net::test_server::GetURLWithUserAndPassword(
      ws_server_, "/connect_check.html", "test", "test");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  EXPECT_EQ("PASS", WaitAndGetTitle());
}

IN_PROC_BROWSER_TEST_F(WebSocketBrowserTest, WebSocketBasicAuthInHTTPSURL) {
  // Launch a basic-auth-protected secure WebSocket server.
  net::test_server::RegisterBasicAuthHandler(wss_server_, "test", "test");
  ASSERT_TRUE(wss_server_.Start());

  // Open connect_check.html via HTTPS with credentials in the URL.
  GURL::Replacements replacements;
  replacements.SetSchemeStr("http");

  GURL url = net::test_server::GetURLWithUserAndPassword(
      wss_server_, "/connect_check.html", "test", "test");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  EXPECT_EQ("PASS", WaitAndGetTitle());
}

// This test verifies that login details entered by the user into the login
// prompt to authenticate the main page are re-used for WebSockets from the same
// origin.
IN_PROC_BROWSER_TEST_F(WebSocketBrowserTest,
                       ReuseMainPageBasicAuthCredentialsForWebSocket) {
  // Launch a basic-auth-protected WebSocket server.
  net::test_server::RegisterBasicAuthHandler(ws_server_, "test", "test");
  ASSERT_TRUE(ws_server_.Start());
  NavigateToHTTPPage("/connect_check.html");

  ASSERT_TRUE(base::test::RunUntil(
      []() { return LoginHandler::GetAllLoginHandlersForTest().size() == 1; }));
  LoginHandler::GetAllLoginHandlersForTest().front()->SetAuth(u"test", u"test");

  EXPECT_EQ("PASS", WaitAndGetTitle());
}

IN_PROC_BROWSER_TEST_F(WebSocketBrowserHTTPConnectToTest,
                       WebSocketBasicAuthInWSURL) {
  // Launch a basic-auth-protected WebSocket server.
  net::test_server::RegisterBasicAuthHandler(ws_server_, "test", "test");
  ASSERT_TRUE(ws_server_.Start());

  GURL url = net::test_server::GetURLWithUserAndPassword(
      ws_server_, "/echo-with-no-extension", "test", "test");

  ConnectTo(url);

  EXPECT_EQ("PASS", WaitAndGetTitle());
}

IN_PROC_BROWSER_TEST_F(WebSocketBrowserHTTPConnectToTest,
                       WebSocketBasicAuthInWSURLBadCreds) {
  // Launch a basic-auth-protected WebSocket server.
  net::test_server::RegisterBasicAuthHandler(ws_server_, "test", "test");
  ASSERT_TRUE(ws_server_.Start());

  GURL url = net::test_server::GetURLWithUserAndPassword(
      ws_server_, "/echo-with-no-extension", "wrong-user", "wrong-password");

  ConnectTo(url);

  EXPECT_EQ("FAIL", WaitAndGetTitle());
}

IN_PROC_BROWSER_TEST_F(WebSocketBrowserHTTPConnectToTest,
                       WebSocketBasicAuthNoCreds) {
  // Launch a basic-auth-protected WebSocket server.
  net::test_server::RegisterBasicAuthHandler(ws_server_, "test", "test");
  ASSERT_TRUE(ws_server_.Start());

  ConnectTo(ws_server_.GetURL("/echo-with-no-extension"));

  EXPECT_EQ("FAIL", WaitAndGetTitle());
}

// HTTPS connection limits should not be applied to wss:. This is only tested
// for secure connections here because the unencrypted case is tested in the
// Blink layout tests, and browser tests are expensive to run.
IN_PROC_BROWSER_TEST_F(WebSocketBrowserTest, SSLConnectionLimit) {
  ASSERT_TRUE(wss_server_.Start());

  NavigateToHTTPSPage("/multiple-connections.html");

  EXPECT_EQ("PASS", WaitAndGetTitle());
}

// Regression test for crbug.com/903553005
IN_PROC_BROWSER_TEST_F(WebSocketBrowserTestWebSocketHSTS,
                       WebSocketAppliesHSTS) {
  net::EmbeddedTestServer https_server(net::EmbeddedTestServer::TYPE_HTTPS);
  https_server.SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
  https_server.ServeFilesFromSourceDirectory(GetChromeTestDataDir());
  // This test sets HSTS on a.test. To avoid being redirected to https, start
  // the http server on 127.0.0.1 instead.
  net::EmbeddedTestServer http_server;
  http_server.ServeFilesFromSourceDirectory(GetChromeTestDataDir());

  net::EmbeddedTestServer wss_server(net::EmbeddedTestServer::TYPE_HTTPS);
  wss_server.SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
  wss_server.ServeFilesFromSourceDirectory(
      net::GetWebSocketTestDataDirectory());
  net::test_server::InstallDefaultWebSocketHandlers(&wss_server);

  ASSERT_TRUE(https_server.Start());
  ASSERT_TRUE(http_server.Start());
  ASSERT_TRUE(wss_server.Start());

  // Navigate to a page that will set HSTS on |test_server_hostname|.
  std::string test_server_hostname = "a.test";
  content::TitleWatcher title_watcher(
      browser()->tab_strip_model()->GetActiveWebContents(), u"SET");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      https_server.GetURL(test_server_hostname, "/websocket/set-hsts.html")));
  const std::u16string result = title_watcher.WaitAndGetTitle();
  EXPECT_TRUE(base::EqualsASCII(result, "SET"));

  // Verify that HSTS applies to WebSocket connections.
  GURL wss_url = net::test_server::GetWebSocketURL(
      wss_server, test_server_hostname, "/echo-with-no-extension");

  std::string scheme("ws");
  GURL::Replacements scheme_replacement;
  scheme_replacement.SetSchemeStr(scheme);
  GURL ws_url = wss_url.ReplaceComponents(scheme_replacement);

  // An https: URL won't work here here because the mixed content policy
  // disallows connections to unencrypted WebSockets from encrypted pages.
  GURL http_url =
      http_server.GetURL("/websocket/check-hsts.html#" + ws_url.spec());

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), http_url));

  EXPECT_EQ("PASS", WaitAndGetTitle());
}

// An implementation of WebSocketClient that expects the mojo connection to be
// disconnected due to invalid UTF-8.
class ExpectInvalidUtf8Client : public network::mojom::WebSocketClient {
 public:
  ExpectInvalidUtf8Client(base::OnceClosure success_closure,
                          base::RepeatingClosure fail_closure)
      : success_closure_(std::move(success_closure)),
        fail_closure_(fail_closure) {}

  ~ExpectInvalidUtf8Client() override = default;

  ExpectInvalidUtf8Client(const ExpectInvalidUtf8Client&) = delete;
  ExpectInvalidUtf8Client& operator=(ExpectInvalidUtf8Client&) = delete;

  void Bind(mojo::PendingReceiver<network::mojom::WebSocketClient> receiver) {
    client_receiver_.Bind(std::move(receiver));
    // This use of base::Unretained is safe because the disconnect handler will
    // not be called after |client_receiver_| is destroyed.
    client_receiver_.set_disconnect_with_reason_handler(base::BindRepeating(
        &ExpectInvalidUtf8Client::OnDisconnect, base::Unretained(this)));
  }

  // Implementation of WebSocketClient
  void OnDataFrame(bool fin,
                   network::mojom::WebSocketMessageType,
                   uint64_t data_length) override {
    NOTREACHED();
  }

  void OnDropChannel(bool was_clean,
                     uint16_t code,
                     const std::string& reason) override {
    NOTREACHED();
  }

  void OnClosingHandshake() override { NOTREACHED(); }

 private:
  void OnDisconnect(uint32_t reason, const std::string& message) {
    if (message == "Browser sent a text frame containing invalid UTF-8") {
      std::move(success_closure_).Run();
    } else {
      ADD_FAILURE() << "Unexpected disconnect: reason=" << reason
                    << " message=\"" << message << '"';
      fail_closure_.Run();
    }
  }

  base::OnceClosure success_closure_;
  const base::RepeatingClosure fail_closure_;

  mojo::Receiver<network::mojom::WebSocketClient> client_receiver_{this};
};

// An implementation of WebSocketHandshakeClient that sends a text message
// containing invalid UTF-8 when the connection is established.
class InvalidUtf8HandshakeClient
    : public network::mojom::WebSocketHandshakeClient {
 public:
  InvalidUtf8HandshakeClient(std::unique_ptr<ExpectInvalidUtf8Client> client,
                             base::RepeatingClosure fail_closure)
      : client_(std::move(client)), fail_closure_(fail_closure) {}
  ~InvalidUtf8HandshakeClient() override = default;

  InvalidUtf8HandshakeClient(const InvalidUtf8HandshakeClient&) = delete;
  InvalidUtf8HandshakeClient& operator=(const InvalidUtf8HandshakeClient&) =
      delete;

  mojo::PendingRemote<network::mojom::WebSocketHandshakeClient> Bind() {
    auto pending_remote = handshake_client_receiver_.BindNewPipeAndPassRemote();
    // This use of base::Unretained is safe because the disconnect handler will
    // not be called after |handshake_client_receiver_| is destroyed.
    handshake_client_receiver_.set_disconnect_handler(
        base::BindOnce(&InvalidUtf8HandshakeClient::FailIfNotConnected,
                       base::Unretained(this)));
    return pending_remote;
  }

  // Implementation of WebSocketHandshakeClient
  void OnOpeningHandshakeStarted(
      network::mojom::WebSocketHandshakeRequestPtr) override {}

  void OnFailure(const std::string& message,
                 int net_error,
                 int response_code) override {}

  void OnConnectionEstablished(
      mojo::PendingRemote<network::mojom::WebSocket> websocket,
      mojo::PendingReceiver<network::mojom::WebSocketClient> client_receiver,
      network::mojom::WebSocketHandshakeResponsePtr,
      mojo::ScopedDataPipeConsumerHandle readable,
      mojo::ScopedDataPipeProducerHandle writable) override {
    client_->Bind(std::move(client_receiver));
    websocket_.Bind(std::move(websocket));

    // Invalid UTF-8.
    static const uint32_t message = 0xff;
    size_t actually_written_bytes = 0;
    websocket_->SendMessage(network::mojom::WebSocketMessageType::TEXT,
                            sizeof(message));

    EXPECT_EQ(
        writable->WriteData(base::byte_span_from_ref(message),
                            MOJO_WRITE_DATA_FLAG_NONE, actually_written_bytes),
        MOJO_RESULT_OK);
    EXPECT_EQ(actually_written_bytes, sizeof(message));

    connected_ = true;
  }

 private:
  void FailIfNotConnected() {
    if (!connected_) {
      fail_closure_.Run();
    }
  }

  const std::unique_ptr<ExpectInvalidUtf8Client> client_;
  const base::RepeatingClosure fail_closure_;
  bool connected_ = false;

  mojo::Receiver<network::mojom::WebSocketHandshakeClient>
      handshake_client_receiver_{this};
  mojo::Remote<network::mojom::WebSocket> websocket_;
};

IN_PROC_BROWSER_TEST_F(WebSocketBrowserTest, SendBadUtf8) {
  ASSERT_TRUE(ws_server_.Start());

  base::RunLoop run_loop;
  bool failed = false;

  // This is a repeating closure for convenience so that we can use it in two
  // places.
  const base::RepeatingClosure fail_closure = base::BindLambdaForTesting([&]() {
    failed = true;
    run_loop.Quit();
  });

  auto client = std::make_unique<ExpectInvalidUtf8Client>(
      run_loop.QuitClosure(), fail_closure);
  auto handshake_client = std::make_unique<InvalidUtf8HandshakeClient>(
      std::move(client), fail_closure);

  MakeWebSocketConnection(
      net::test_server::GetWebSocketURL(ws_server_, "/close"),
      handshake_client->Bind());

  run_loop.Run();

  EXPECT_FALSE(failed);
}

class FailureMonitoringHandshakeClient
    : public network::mojom::WebSocketHandshakeClient {
 public:
  struct Result {
    bool failure_reported = false;
    int net_error = -1;
    int response_code = -1;
  };

  explicit FailureMonitoringHandshakeClient(base::OnceClosure quit)
      : quit_(std::move(quit)) {}

  FailureMonitoringHandshakeClient(const FailureMonitoringHandshakeClient&) =
      delete;
  FailureMonitoringHandshakeClient& operator=(
      const FailureMonitoringHandshakeClient&) = delete;

  mojo::PendingRemote<network::mojom::WebSocketHandshakeClient> Bind() {
    CHECK(quit_);
    auto pending_remote = handshake_client_receiver_.BindNewPipeAndPassRemote();
    handshake_client_receiver_.set_disconnect_handler(std::move(quit_));
    return pending_remote;
  }

  const Result& result() const { return result_; }

  // Implementation of WebSocketHandshakeClient
  void OnOpeningHandshakeStarted(
      network::mojom::WebSocketHandshakeRequestPtr) override {}

  void OnFailure(const std::string& message,
                 int net_error,
                 int response_code) override {
    result_.failure_reported = true;
    result_.net_error = net_error;
    result_.response_code = response_code;
  }

  void OnConnectionEstablished(
      mojo::PendingRemote<network::mojom::WebSocket> websocket,
      mojo::PendingReceiver<network::mojom::WebSocketClient> client_receiver,
      network::mojom::WebSocketHandshakeResponsePtr,
      mojo::ScopedDataPipeConsumerHandle readable,
      mojo::ScopedDataPipeProducerHandle writable) override {}

 private:
  Result result_;
  base::OnceClosure quit_;
  mojo::Receiver<network::mojom::WebSocketHandshakeClient>
      handshake_client_receiver_{this};
};

IN_PROC_BROWSER_TEST_F(WebSocketBrowserTest, FailuresReported) {
  ASSERT_TRUE(ws_server_.Start());

  {
    // The OnFailure method should not be called for a successful connection.
    base::RunLoop run_loop;
    auto handshake_client = std::make_unique<FailureMonitoringHandshakeClient>(
        run_loop.QuitClosure());
    MakeWebSocketConnection(net::test_server::GetWebSocketURL(
                                ws_server_, "/echo-with-no-extension"),
                            handshake_client->Bind());
    run_loop.Run();
    EXPECT_FALSE(handshake_client->result().failure_reported);
  }

  {
    // If the server returns a 404 status, that should be surfaced via
    // OnFailure.
    base::RunLoop run_loop;
    auto handshake_client = std::make_unique<FailureMonitoringHandshakeClient>(
        run_loop.QuitClosure());
    MakeWebSocketConnection(
        net::test_server::GetWebSocketURL(ws_server_, "/nonsensedoesntexist"),
        handshake_client->Bind());
    run_loop.Run();
    EXPECT_TRUE(handshake_client->result().failure_reported);
    EXPECT_EQ(404, handshake_client->result().response_code);
  }
}

IN_PROC_BROWSER_TEST_F(WebSocketBrowserTest, CheckFileOrigin) {
  ASSERT_TRUE(ws_server_.Start());
  int port = ws_server_.host_port_pair().port();

  AlsoWaitForTitle(u"NULL");
  AlsoWaitForTitle(u"FILE");

  base::RunLoop run_loop;
  NavigateToPath(base::StringPrintf("check-origin.html?port=%d", port));
  EXPECT_EQ("NULL", WaitAndGetTitle());
}

IN_PROC_BROWSER_TEST_F(WebSocketBrowserTestWithAllowFileAccessFromFiles,
                       CheckFileOrigin) {
  ASSERT_TRUE(ws_server_.Start());
  int port = ws_server_.host_port_pair().port();

  AlsoWaitForTitle(u"NULL");
  AlsoWaitForTitle(u"FILE");

  base::RunLoop run_loop;
  NavigateToPath(base::StringPrintf("check-origin.html?port=%d", port));
  EXPECT_EQ("FILE", WaitAndGetTitle());
}

IN_PROC_BROWSER_TEST_F(WebSocketBrowserHTTPSConnectToTestPre3pcd,
                       CookieAccess_ThirdPartyAllowed) {
  wss_server_.SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
  ASSERT_TRUE(wss_server_.Start());

  SetBlockThirdPartyCookies(false);

  ASSERT_TRUE(content::SetCookie(browser()->profile(),
                                 server().GetURL(kHostA, "/"),
                                 "cookie=1; SameSite=None; Secure"));

  content::DOMMessageQueue message_queue(
      browser()->tab_strip_model()->GetActiveWebContents());
  ConnectTo(kHostB, net::test_server::GetWebSocketURL(wss_server_, kHostA,
                                                      "/echo-request-headers"));

  std::string message;
  EXPECT_TRUE(message_queue.WaitForMessage(&message));
  EXPECT_THAT(message, HasSubstr("cookie=1"));
  EXPECT_EQ("PASS", WaitAndGetTitle());
}

IN_PROC_BROWSER_TEST_F(WebSocketBrowserHTTPSConnectToTest,
                       CookieAccess_ThirdPartyBlocked) {
  wss_server_.SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
  ASSERT_TRUE(wss_server_.Start());

  SetBlockThirdPartyCookies(true);

  ASSERT_TRUE(content::SetCookie(browser()->profile(),
                                 server().GetURL(kHostA, "/"),
                                 "cookie=1; SameSite=None; Secure"));

  content::DOMMessageQueue message_queue(
      browser()->tab_strip_model()->GetActiveWebContents());
  ConnectTo(kHostB, net::test_server::GetWebSocketURL(wss_server_, kHostA,
                                                      "/echo-request-headers"));

  std::string message;
  EXPECT_TRUE(message_queue.WaitForMessage(&message));
  EXPECT_THAT(message, Not(HasSubstr("cookie=1")));
  EXPECT_EQ("PASS", WaitAndGetTitle());
}

IN_PROC_BROWSER_TEST_F(WebSocketBrowserHTTPSConnectToTest,
                       CookieAccess_ThirdPartyAllowedBySetting) {
  wss_server_.SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
  ASSERT_TRUE(wss_server_.Start());

  SetBlockThirdPartyCookies(true);

  GURL::Replacements port_replacement;
  std::string port_str =
      base::NumberToString(wss_server_.host_port_pair().port());
  port_replacement.SetPortStr(port_str);

  {
    base::test::TestFuture<void> future;
    browser()
        ->profile()
        ->GetDefaultStoragePartition()
        ->GetCookieManagerForBrowserProcess()
        ->SetContentSettings(
            ContentSettingsType::COOKIES,
            {
                ContentSettingPatternSource(
                    /*primary_pattern=*/ContentSettingsPattern::
                        FromURLNoWildcard(
                            server()
                                .GetURL(kHostA, "/")
                                .ReplaceComponents(port_replacement)),
                    /*secondary_patttern=*/
                    ContentSettingsPattern::FromURLNoWildcard(
                        server().GetURL(kHostB, "/")),
                    /*setting_value=*/base::Value(CONTENT_SETTING_ALLOW),
                    content_settings::ProviderType::kPrefProvider,
                    /*incognito=*/false,
                    /*metadata=*/content_settings::RuleMetaData()),
            },
            future.GetCallback());
    ASSERT_TRUE(future.Wait());
  }

  ASSERT_TRUE(content::SetCookie(browser()->profile(),
                                 server().GetURL(kHostA, "/"),
                                 "cookie=1; SameSite=None; Secure"));

  content::DOMMessageQueue message_queue(
      browser()->tab_strip_model()->GetActiveWebContents());
  ConnectTo(kHostB, net::test_server::GetWebSocketURL(wss_server_, kHostA,
                                                      "/echo-request-headers"));

  std::string message;
  EXPECT_TRUE(message_queue.WaitForMessage(&message));
  EXPECT_THAT(message, HasSubstr("cookie=1"));
  EXPECT_EQ("PASS", WaitAndGetTitle());
}

}  // namespace
