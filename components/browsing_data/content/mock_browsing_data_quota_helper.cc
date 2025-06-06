// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/browsing_data/content/mock_browsing_data_quota_helper.h"

#include "content/public/browser/browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::BrowserThread;

MockBrowsingDataQuotaHelper::MockBrowsingDataQuotaHelper() = default;

MockBrowsingDataQuotaHelper::~MockBrowsingDataQuotaHelper() = default;

void MockBrowsingDataQuotaHelper::StartFetching(FetchResultCallback callback) {
  ASSERT_FALSE(callback.is_null());
  ASSERT_TRUE(callback_.is_null());
  callback_ = std::move(callback);
}

void MockBrowsingDataQuotaHelper::DeleteStorageKeyData(
    const blink::StorageKey& storage_key,
    base::OnceClosure completed) {}

void MockBrowsingDataQuotaHelper::AddHost(const blink::StorageKey& storage_key,
                                          int64_t usage) {
  response_.emplace_back(storage_key, usage);
}

void MockBrowsingDataQuotaHelper::AddQuotaSamples() {
  AddHost(blink::StorageKey::CreateFromStringForTesting("http://quotahost1"),
          2);
  AddHost(blink::StorageKey::CreateFromStringForTesting("https://quotahost2"),
          20);
}

void MockBrowsingDataQuotaHelper::Notify() {
  std::move(callback_).Run(response_);
  response_.clear();
}
