// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/browser/android_protocol_handler.h"

#include <memory>
#include <utility>

#include "android_webview/common/url_constants.h"
#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/android/jni_weak_ref.h"
#include "components/embedder_support/android/util/input_stream.h"
#include "content/public/common/url_constants.h"
#include "net/base/io_buffer.h"
#include "net/base/mime_util.h"
#include "net/base/net_errors.h"
#include "net/http/http_util.h"
#include "url/android/gurl_android.h"
#include "url/gurl.h"
#include "url/url_constants.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "android_webview/browser_jni_headers/AndroidProtocolHandler_jni.h"

using base::android::AttachCurrentThread;
using base::android::ClearException;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::ScopedJavaGlobalRef;
using base::android::ScopedJavaLocalRef;
using embedder_support::InputStream;

namespace android_webview {

// static
std::unique_ptr<InputStream> CreateInputStream(JNIEnv* env, const GURL& url) {
  DCHECK(url.is_valid());
  DCHECK(env);

  // Open the input stream.
  ScopedJavaLocalRef<jobject> stream =
      android_webview::Java_AndroidProtocolHandler_open(
          env, url::GURLAndroid::FromNativeGURL(env, url));

  if (!stream) {
    DLOG(ERROR) << "Unable to open input stream for Android URL";
    return nullptr;
  }
  return std::make_unique<InputStream>(stream);
}

bool GetInputStreamMimeType(JNIEnv* env,
                            const GURL& url,
                            embedder_support::InputStream* stream,
                            std::string* mime_type) {
  // Query the mime type from the Java side. It is possible for the query to
  // fail, as the mime type cannot be determined for all supported schemes.
  std::string returned_type =
      android_webview::Java_AndroidProtocolHandler_getMimeType(
          env, stream->jobj(), url::GURLAndroid::FromNativeGURL(env, url));
  if (returned_type.empty()) {
    return false;
  }

  *mime_type = returned_type;
  return true;
}

static std::string JNI_AndroidProtocolHandler_GetAndroidAssetPath(JNIEnv* env) {
  return android_webview::kAndroidAssetPath;
}

static std::string JNI_AndroidProtocolHandler_GetAndroidResourcePath(
    JNIEnv* env) {
  return android_webview::kAndroidResourcePath;
}

// Returns the mime type, or returns empty string if a mime type was not found.
static std::string JNI_AndroidProtocolHandler_GetWellKnownMimeType(
    JNIEnv* env,
    std::string& path) {
  std::string mime_type;

  if (net::GetWellKnownMimeTypeFromFile(base::FilePath(path), &mime_type)) {
    return mime_type;
  }

  return "";
}

}  // namespace android_webview
