// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_HISTORY_HISTORY_UI_H_
#define CHROME_BROWSER_UI_WEBUI_HISTORY_HISTORY_UI_H_

#include <memory>

#include "base/gtest_prod_util.h"
#include "components/commerce/core/mojom/product_specifications.mojom.h"
#include "components/commerce/core/mojom/shopping_service.mojom.h"
#include "components/page_image_service/mojom/page_image_service.mojom.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/user_education/webui/help_bubble_handler.h"
#include "content/public/browser/webui_config.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "ui/base/resource/resource_scale_factor.h"
#include "ui/webui/mojo_web_ui_controller.h"
#include "ui/webui/resources/cr_components/help_bubble/help_bubble.mojom.h"
#include "ui/webui/resources/cr_components/history/history.mojom-forward.h"
#include "ui/webui/resources/cr_components/history_clusters/history_clusters.mojom-forward.h"
#include "ui/webui/resources/cr_components/history_embeddings/history_embeddings.mojom.h"

namespace base {
class RefCountedMemory;
}

class BrowsingHistoryHandler;

namespace history_clusters {
class HistoryClustersHandler;
}

class HistoryEmbeddingsHandler;

namespace page_image_service {
class ImageServiceHandler;
}

class HistoryUIConfig : public content::WebUIConfig {
 public:
  HistoryUIConfig();
  ~HistoryUIConfig() override;

  // content::WebUIConfig:
  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui,
      const GURL& url) override;
};

class HistoryUI : public ui::MojoWebUIController,
                  public help_bubble::mojom::HelpBubbleHandlerFactory {
 public:
  explicit HistoryUI(content::WebUI* web_ui);
  HistoryUI(const HistoryUI&) = delete;
  HistoryUI& operator=(const HistoryUI&) = delete;
  ~HistoryUI() override;

  static base::RefCountedMemory* GetFaviconResourceBytes(
      ui::ResourceScaleFactor scale_factor);

  // Instantiates the implementors of mojom interfaces.
  void BindInterface(
      mojo::PendingReceiver<history_embeddings::mojom::PageHandler>
          pending_page_handler);
  void BindInterface(
      mojo::PendingReceiver<history::mojom::PageHandler> pending_page_handler);
  void BindInterface(mojo::PendingReceiver<history_clusters::mojom::PageHandler>
                         pending_page_handler);
  void BindInterface(
      mojo::PendingReceiver<page_image_service::mojom::PageImageServiceHandler>
          pending_page_handler);
  void BindInterface(
      mojo::PendingReceiver<
          shopping_service::mojom::ShoppingServiceHandlerFactory> receiver);
  void BindInterface(
      mojo::PendingReceiver<help_bubble::mojom::HelpBubbleHandlerFactory>
          pending_receiver);
  void BindInterface(
      mojo::PendingReceiver<commerce::product_specifications::mojom::
                                ProductSpecificationsHandlerFactory> receiver);

  // For testing only.
  history_clusters::HistoryClustersHandler*
  GetHistoryClustersHandlerForTesting() {
    return history_clusters_handler_.get();
  }

  BrowsingHistoryHandler* GetBrowsingHistoryHandlerForTesting() {
    return browsing_history_handler_.get();
  }

 private:
  // help_bubble::mojom::HelpBubbleHandlerFactory:
  void CreateHelpBubbleHandler(
      mojo::PendingRemote<help_bubble::mojom::HelpBubbleClient> client,
      mojo::PendingReceiver<help_bubble::mojom::HelpBubbleHandler> handler)
      override;
  std::unique_ptr<HistoryEmbeddingsHandler> history_embeddings_handler_;
  std::unique_ptr<history_clusters::HistoryClustersHandler>
      history_clusters_handler_;
  std::unique_ptr<BrowsingHistoryHandler> browsing_history_handler_;
  std::unique_ptr<page_image_service::ImageServiceHandler>
      image_service_handler_;
  PrefChangeRegistrar pref_change_registrar_;
  std::unique_ptr<user_education::HelpBubbleHandler> help_bubble_handler_;
  mojo::Receiver<help_bubble::mojom::HelpBubbleHandlerFactory>
      help_bubble_handler_factory_receiver_{this};

  void UpdateDataSource();

  WEB_UI_CONTROLLER_TYPE_DECL();
};

#endif  // CHROME_BROWSER_UI_WEBUI_HISTORY_HISTORY_UI_H_
