// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WEB_APPLICATIONS_ISOLATED_WEB_APPS_UPDATE_MANIFEST_UPDATE_MANIFEST_FETCHER_H_
#define CHROME_BROWSER_WEB_APPLICATIONS_ISOLATED_WEB_APPS_UPDATE_MANIFEST_UPDATE_MANIFEST_FETCHER_H_

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/types/expected.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "url/gurl.h"

namespace network {
class SimpleURLLoader;
class SharedURLLoaderFactory;
}  // namespace network

namespace web_app {

class UpdateManifest;

// Helper class to download and parse an update manifest of an Isolated Web App.
class UpdateManifestFetcher {
 public:
  enum class Error {
    kDownloadFailed,
    kInvalidJson,
    kInvalidManifest,
  };

  using FetchCallback =
      base::OnceCallback<void(base::expected<UpdateManifest, Error>)>;

  explicit UpdateManifestFetcher(
      GURL url,
      net::PartialNetworkTrafficAnnotationTag partial_traffic_annotation,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);

  ~UpdateManifestFetcher();

  // Starts downloading and parsing. Will `CHECK` if called more than once.
  void FetchUpdateManifest(FetchCallback fetch_callback);

 private:
  void DownloadUpdateManifest();

  void OnUpdateManifestDownloaded(
      std::unique_ptr<std::string> update_manifest_content);

  void ParseUpdateManifest(const std::string& update_manifest_content);

  GURL url_;
  net::PartialNetworkTrafficAnnotationTag partial_traffic_annotation_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  FetchCallback fetch_callback_;

  std::unique_ptr<network::SimpleURLLoader> simple_url_loader_;

  base::WeakPtrFactory<UpdateManifestFetcher> weak_factory_{this};
};

}  // namespace web_app

#endif  // CHROME_BROWSER_WEB_APPLICATIONS_ISOLATED_WEB_APPS_UPDATE_MANIFEST_UPDATE_MANIFEST_FETCHER_H_
