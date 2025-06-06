// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_FAVICON_HELPER_H_
#define CHROME_BROWSER_ANDROID_FAVICON_HELPER_H_

#include <jni.h>

#include <memory>

#include "base/android/scoped_java_ref.h"
#include "base/gtest_prod_util.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "url/gurl.h"

class Profile;

class FaviconHelper {
 public:
  FaviconHelper();
  void Destroy(JNIEnv* env);

  FaviconHelper(const FaviconHelper&) = delete;
  FaviconHelper& operator=(const FaviconHelper&) = delete;

  jboolean GetLocalFaviconImageForURL(
      JNIEnv* env,
      Profile* profile,
      GURL& page_url,
      jint j_desired_size_in_pixel,
      const base::android::JavaParamRef<jobject>& j_favicon_image_callback);
  jboolean GetForeignFaviconImageForURL(
      JNIEnv* env,
      Profile* profile,
      GURL& page_url,
      jint j_desired_size_in_pixel,
      const base::android::JavaParamRef<jobject>& j_favicon_image_callback);

  void GetLocalFaviconImageForURLInternal(
      favicon::FaviconService* favicon_service,
      GURL url,
      int desired_size_in_pixel,
      favicon_base::FaviconRawBitmapCallback callback_runner);
  void OnJobFinished(int job_id);

 private:
  virtual ~FaviconHelper();

  // This function is expected to be bound to a WeakPtr<FaviconHelper>, so that
  // it won't be run if the FaviconHelper is deleted and
  // |j_favicon_image_callback| isn't executed in that case.
  void OnFaviconBitmapResultAvailable(
      const base::android::JavaRef<jobject>& j_favicon_image_callback,
      const favicon_base::FaviconRawBitmapResult& result);

  std::unique_ptr<base::CancelableTaskTracker> cancelable_task_tracker_;

  base::WeakPtrFactory<FaviconHelper> weak_ptr_factory_{this};
};

#endif  // CHROME_BROWSER_ANDROID_FAVICON_HELPER_H_
