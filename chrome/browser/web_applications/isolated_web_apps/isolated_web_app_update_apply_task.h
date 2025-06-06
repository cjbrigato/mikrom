// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WEB_APPLICATIONS_ISOLATED_WEB_APPS_ISOLATED_WEB_APP_UPDATE_APPLY_TASK_H_
#define CHROME_BROWSER_WEB_APPLICATIONS_ISOLATED_WEB_APPS_ISOLATED_WEB_APP_UPDATE_APPLY_TASK_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "base/version.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/web_applications/isolated_web_apps/isolated_web_app_url_info.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/web_applications/isolated_web_apps/commands/copy_bundle_to_cache_command.h"
#include "chrome/browser/web_applications/isolated_web_apps/policy/isolated_web_app_cache_client.h"
#endif  // BUILDFLAG(IS_CHROMEOS)

class ScopedKeepAlive;
class ScopedProfileKeepAlive;

namespace web_app {

struct IsolatedWebAppApplyUpdateCommandError;
class IsolatedWebAppApplyUpdateCommandSuccess;
class WebAppCommandScheduler;

// This task is responsible for applying a pending Isolated Web App update by
// calling `WebAppCommandScheduler::ApplyPendingIsolatedWebAppUpdate`.
class IsolatedWebAppUpdateApplyTask {
 public:
  using CompletionStatus =
      base::expected<IsolatedWebAppApplyUpdateCommandSuccess,
                     IsolatedWebAppApplyUpdateCommandError>;
  using CompletionCallback = base::OnceCallback<void(CompletionStatus status)>;

  IsolatedWebAppUpdateApplyTask(
      IsolatedWebAppUrlInfo url_info,
      std::unique_ptr<ScopedKeepAlive> optional_keep_alive,
      std::unique_ptr<ScopedProfileKeepAlive> optional_profile_keep_alive,
      WebAppCommandScheduler& command_scheduler,
      Profile* profile);
  ~IsolatedWebAppUpdateApplyTask();

  IsolatedWebAppUpdateApplyTask(const IsolatedWebAppUpdateApplyTask&) = delete;
  IsolatedWebAppUpdateApplyTask& operator=(
      const IsolatedWebAppUpdateApplyTask&) = delete;

  void Start(CompletionCallback callback);

  bool has_started() const { return has_started_; }

  const IsolatedWebAppUrlInfo& url_info() const { return url_info_; }

  base::Value AsDebugValue() const;

#if BUILDFLAG(IS_CHROMEOS)
  static constexpr char kCopyToCacheFailedMessage[] =
      "Failed to cache the bundle update";
#endif  // BUILDFLAG(IS_CHROMEOS)

 private:
  void OnUpdateApplied(CompletionStatus result);

#if BUILDFLAG(IS_CHROMEOS)
  void CopyUpdatedBundleToCache(
      const IsolatedWebAppApplyUpdateCommandSuccess& apply_success_result);

  void OnBundleCopiedToCache(
      const IsolatedWebAppApplyUpdateCommandSuccess& apply_success_result,
      CopyBundleToCacheResult result);
#endif  // BUILDFLAG(IS_CHROMEOS)

  IsolatedWebAppUrlInfo url_info_;
  std::unique_ptr<ScopedKeepAlive> optional_keep_alive_;
  std::unique_ptr<ScopedProfileKeepAlive> optional_profile_keep_alive_;
  raw_ref<WebAppCommandScheduler> command_scheduler_;
  raw_ref<Profile> profile_;

  base::Value::Dict debug_log_;
  bool has_started_ = false;
  CompletionCallback callback_;

#if BUILDFLAG(IS_CHROMEOS)
  // `cache_client_` is created only when `IsIwaBundleCacheEnabled()` is true.
  std::unique_ptr<IwaCacheClient> cache_client_;
#endif  // BUILDFLAG(IS_CHROMEOS)

  base::WeakPtrFactory<IsolatedWebAppUpdateApplyTask> weak_factory_{this};
};

}  // namespace web_app

#endif  // CHROME_BROWSER_WEB_APPLICATIONS_ISOLATED_WEB_APPS_ISOLATED_WEB_APP_UPDATE_APPLY_TASK_H_
