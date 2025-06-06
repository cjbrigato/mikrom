// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40285824): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include "ash/webui/media_app_ui/media_app_guest_ui.h"

#include <memory>

#include "ash/constants/ash_features.h"
#include "ash/webui/grit/ash_media_app_resources.h"
#include "ash/webui/media_app_ui/url_constants.h"
#include "ash/webui/web_applications/webui_test_prod_util.h"
#include "base/containers/span.h"
#include "base/feature_list.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/ref_counted_memory.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "chromeos/ash/components/mantis/media_app/mantis_untrusted_service_manager.h"
#include "chromeos/ash/components/specialized_features/feature_access_checker.h"
#include "chromeos/grit/chromeos_media_app_bundle_resources.h"
#include "chromeos/grit/chromeos_media_app_bundle_resources_map.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/mojom/autoplay/autoplay.mojom.h"
#include "ui/file_manager/grit/file_manager_resources.h"

namespace ash {

namespace {

constexpr base::FilePath::CharType kFontsRoot[] =
    FILE_PATH_LITERAL("/usr/share/fonts");
constexpr char kFontRequestPrefix[] = "fonts/";

int g_media_app_window_count = 0;

// Helper class to populate MediaApp metrics for UMA and for Happiness Tracking
// surveys. Manages its own lifetime; tracking whether at least one MediaApp
// WebUI instance is still running.
class MediaAppMetricsHelper {
 public:
  static MediaAppUserActions actions;

  static void OnUiFirstNavigated() {
    // Record the number of other media app windows that currently exist when a
    // new one is created. Counts windows open with any supported file type, or
    // in the "zero state" (with no open file). Pick 50 as a sensible maximum
    // (additional windows will be recorded in the 51 bucket).
    constexpr int kMaxExpectedWindowCount = 50;
    UMA_HISTOGRAM_EXACT_LINEAR("Apps.MediaApp.Load.OtherOpenWindowCount",
                               g_media_app_window_count,
                               kMaxExpectedWindowCount);
    if (g_media_app_window_count++ == 0) {
      DCHECK(!instance);
      instance = new MediaAppMetricsHelper();
    }
  }

  static void OnUiDestroyedAfterNavigation() {
    if (--g_media_app_window_count == 0) {
      delete instance;
      instance = nullptr;
    }
  }

  MediaAppMetricsHelper(const MediaAppMetricsHelper&) = delete;
  MediaAppMetricsHelper& operator=(const MediaAppMetricsHelper&) = delete;

 private:
  MediaAppMetricsHelper() { base::AddActionCallback(callback_); }
  ~MediaAppMetricsHelper() { base::RemoveActionCallback(callback_); }

  static void OnAction(const std::string& user_action,
                       base::TimeTicks action_time) {
    actions.clicked_edit_image_in_photos =
        actions.clicked_edit_image_in_photos ||
        user_action == "MediaApp.Image.Tool.EditInPhotos";
    actions.clicked_edit_video_in_photos =
        actions.clicked_edit_video_in_photos ||
        user_action == "MediaApp.Video.Tool.EditInPhotos";
  }

  base::ActionCallback callback_ =
      base::BindRepeating(&MediaAppMetricsHelper::OnAction);

  static MediaAppMetricsHelper* instance;
};
MediaAppUserActions MediaAppMetricsHelper::actions = {false, false};
MediaAppMetricsHelper* MediaAppMetricsHelper::instance = nullptr;

bool IsFontRequest(const std::string& path) {
  return base::StartsWith(path, kFontRequestPrefix);
}

void FontLoaded(content::WebUIDataSource::GotDataCallback got_data_callback,
                std::unique_ptr<std::string> font_data,
                bool did_load_file) {
  if (font_data->size() && did_load_file) {
    std::move(got_data_callback)
        .Run(new base::RefCountedBytes(base::as_byte_span(*font_data)));
  } else {
    std::move(got_data_callback).Run(nullptr);
  }
}

content::WebUIDataSource* CreateAndAddMediaAppUntrustedDataSource(
    content::WebUI* web_ui,
    MediaAppGuestUIDelegate* delegate) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      web_ui->GetWebContents()->GetBrowserContext(), kChromeUIMediaAppGuestURL);
  // Add resources from ash_media_app_resources.pak.
  source->AddResourcePath("app.html", IDR_MEDIA_APP_APP_HTML);
  source->AddResourcePath("receiver.js", IDR_MEDIA_APP_RECEIVER_JS);
  source->AddResourcePath("piex_module.js", IDR_MEDIA_APP_PIEX_MODULE_JS);

  // Add shared resources from ash_file_manager_resources.pak.
  source->AddResourcePath("piex/piex.js.wasm", IDR_IMAGE_LOADER_PIEX_WASM_JS);
  source->AddResourcePath("piex/piex.out.wasm", IDR_IMAGE_LOADER_PIEX_WASM);

  // Add resources from chromeos_media_app_bundle_resources.pak that are also
  // needed for mocks. If enable_cros_media_app = true, then these calls will
  // happen a second time with the same parameters. When false, we need these to
  // specify what routes are mocked by files in ./resources/mock/js. The loop is
  // irrelevant in that case.
  source->AddResourcePath("js/app_main.js", IDR_MEDIA_APP_APP_MAIN_JS);
  source->AddResourcePath("js/app_image_handler_module.js",
                          IDR_MEDIA_APP_APP_IMAGE_HANDLER_MODULE_JS);

  // Add all resources from chromeos_media_app_bundle_resources.pak.
  source->AddResourcePaths(kChromeosMediaAppBundleResources);

  // Note: go/bbsrc/flags.ts processes this.
  delegate->PopulateLoadTimeData(web_ui, source);
  source->UseStringsJs();

  source->AddFrameAncestor(GURL(kChromeUIMediaAppURL));
  // By default, prevent all network access.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::DefaultSrc,
      "default-src blob: 'self';");
  // Need to explicitly set |worker-src| because CSP falls back to |child-src|
  // which is none.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::WorkerSrc, "worker-src 'self';");
  // Allow images to also handle data urls.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ImgSrc, "img-src blob: data: 'self';");
  // Allow styles to include inline styling needed for Polymer elements and
  // the material 3 dynamic palette.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::StyleSrc,
      "style-src 'self' 'unsafe-inline' chrome-untrusted://theme;");
  // Allow loading PDFs as blob URLs.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ObjectSrc, "object-src blob:;");
  // Required to successfully load PDFs in the `<embed>` element.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::FrameSrc, "frame-src blob:;");
  // Allow wasm and mojo.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ScriptSrc,
      "script-src 'self' 'wasm-eval' chrome-untrusted://resources;");
  // Allow calls to Maps reverse geocoding API for loading metadata.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ConnectSrc,
      "connect-src 'self' https://maps.googleapis.com/maps/api/geocode/json;");

  // Allow use of SharedArrayBuffer (required by the wasm).
  source->OverrideCrossOriginOpenerPolicy("same-origin");
  source->OverrideCrossOriginEmbedderPolicy("require-corp");
  // chrome://media-app and chrome-untrusted://media-app are different origins,
  // so allow resources in the guest to be loaded cross-origin.
  source->OverrideCrossOriginResourcePolicy("cross-origin");

  // TODO(crbug.com/40137141): Trusted Type remaining WebUI.
  source->DisableTrustedTypesCSP();
  return source;
}

}  // namespace

MediaAppGuestUI::MediaAppGuestUI(
    content::WebUI* web_ui,
    std::unique_ptr<MediaAppGuestUIDelegate> delegate)
    : UntrustedWebUIController(web_ui),
      WebContentsObserver(web_ui->GetWebContents()),
      delegate_(std::move(delegate)) {
  task_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN});

  content::WebUIDataSource* untrusted_source =
      CreateAndAddMediaAppUntrustedDataSource(web_ui, delegate_.get());

  MaybeConfigureTestableDataSource(
      untrusted_source, "media_app/untrusted",
      base::BindRepeating(&IsFontRequest),
      base::BindRepeating(&MediaAppGuestUI::StartFontDataRequest,
                          weak_factory_.GetWeakPtr()));
}

MediaAppGuestUI::~MediaAppGuestUI() {
  if (app_navigation_committed_) {
    MediaAppMetricsHelper::OnUiDestroyedAfterNavigation();
  }
}

void MediaAppGuestUI::ReadyToCommitNavigation(
    content::NavigationHandle* handle) {
  // Force-enable autoplay support.
  const std::string allowed_resource = "app.html";
  if (handle->GetURL() != GURL(kChromeUIMediaAppGuestURL + allowed_resource)) {
    return;
  }

  if (!app_navigation_committed_) {
    app_navigation_committed_ = true;
    MediaAppMetricsHelper::OnUiFirstNavigated();
  }

  mojo::AssociatedRemote<blink::mojom::AutoplayConfigurationClient> client;
  handle->GetRenderFrameHost()->GetRemoteAssociatedInterfaces()->GetInterface(
      &client);
  client->AddAutoplayFlags(url::Origin::Create(handle->GetURL()),
                           blink::mojom::kAutoplayFlagForceAllow);
}

void MediaAppGuestUI::StartFontDataRequest(
    const std::string& request_path,
    content::WebUIDataSource::GotDataCallback got_data_callback) {
  CHECK(IsFontRequest(request_path));
  const std::string path = request_path.substr(sizeof(kFontRequestPrefix) - 1);
  const base::FilePath font_path = base::FilePath(kFontsRoot).AppendASCII(path);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&base::PathExists, font_path),
      base::BindOnce(&MediaAppGuestUI::StartFontDataRequestAfterPathExists,
                     weak_factory_.GetWeakPtr(), font_path,
                     std::move(got_data_callback)));
}

void MediaAppGuestUI::StartFontDataRequestAfterPathExists(
    const base::FilePath& font_path,
    content::WebUIDataSource::GotDataCallback got_data_callback,
    bool path_exists) {
  if (path_exists) {
    auto font_data = std::make_unique<std::string>();
    std::string* data = font_data.get();
    task_runner_->PostTaskAndReplyWithResult(
        FROM_HERE, base::BindOnce(&base::ReadFileToString, font_path, data),
        base::BindOnce(&FontLoaded, std::move(got_data_callback),
                       std::move(font_data)));

  } else {
    std::move(got_data_callback).Run(nullptr);
  }
}

void MediaAppGuestUI::BindInterface(
    mojo::PendingReceiver<color_change_listener::mojom::PageHandler> receiver) {
  color_provider_handler_ = std::make_unique<ui::ColorChangeHandler>(
      web_ui()->GetWebContents(), std::move(receiver));
}

void MediaAppGuestUI::BindInterface(
    mojo::PendingReceiver<media_app_ui::mojom::UntrustedServiceFactory>
        receiver) {
  if (untrusted_service_factory_.is_bound()) {
    untrusted_service_factory_.reset();
  }
  untrusted_service_factory_.Bind(std::move(receiver));
}

void MediaAppGuestUI::CreateOcrUntrustedService(
    mojo::PendingReceiver<media_app_ui::mojom::OcrUntrustedService> receiver,
    mojo::PendingRemote<media_app_ui::mojom::OcrUntrustedPage> page) {
  delegate_->CreateAndBindOcrUntrustedService(
      *web_ui()->GetWebContents()->GetBrowserContext(),
      web_ui()->GetWebContents()->GetTopLevelNativeWindow(),
      std::move(receiver), std::move(page));
}

void MediaAppGuestUI::CreateMahiUntrustedService(
    mojo::PendingReceiver<media_app_ui::mojom::MahiUntrustedService> receiver,
    mojo::PendingRemote<media_app_ui::mojom::MahiUntrustedPage> page,
    const std::string& file_name) {
  delegate_->CreateAndBindMahiUntrustedService(
      std::move(receiver), std::move(page), file_name,
      web_ui()->GetWebContents()->GetTopLevelNativeWindow());
}

void MediaAppGuestUI::OnMantisAvailableDone(IsMantisAvailableCallback callback,
                                            bool result) {
  is_mantis_available_ = result;
  std::move(callback).Run(result);
}

void MediaAppGuestUI::IsMantisAvailable(IsMantisAvailableCallback callback) {
  // Mantis does not live in //chrome, no need to use delegate.
  if (mantis_untrusted_service_manager_ == nullptr) {
    mantis_untrusted_service_manager_ =
        std::make_unique<MantisUntrustedServiceManager>(
            delegate_->GetFeatureAccessChecker(
                MantisUntrustedServiceManager::GetFeatureAccessConfig(),
                web_ui()));
  }

  mantis_untrusted_service_manager_->IsAvailable(
      delegate_->GetPrefService(web_ui()),
      base::BindOnce(&MediaAppGuestUI::OnMantisAvailableDone,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MediaAppGuestUI::CreateMantisUntrustedService(
    mojo::PendingRemote<media_app_ui::mojom::MantisUntrustedPage> page,
    const std::optional<base::Uuid>& dlc_uuid,
    CreateMantisUntrustedServiceCallback callback) {
  if (!is_mantis_available_.value_or(false)) {
    untrusted_service_factory_.ReportBadMessage(
        "Trying to bind interface when feature is not available.");
    return;
  }
  mantis_untrusted_service_manager_->Create(std::move(page), dlc_uuid,
                                            std::move(callback));
}

MediaAppUserActions GetMediaAppUserActionsForHappinessTracking() {
  return MediaAppMetricsHelper::actions;
}

WEB_UI_CONTROLLER_TYPE_IMPL(MediaAppGuestUI)

}  // namespace ash
