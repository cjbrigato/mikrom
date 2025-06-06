// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/browser/file_system/file_system_quota_client.h"

#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "base/barrier_callback.h"
#include "base/barrier_closure.h"
#include "base/check.h"
#include "base/containers/span.h"
#include "base/feature_list.h"
#include "base/files/file.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "components/services/storage/public/cpp/buckets/bucket_locator.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "storage/browser/file_system/file_system_backend.h"
#include "storage/browser/file_system/file_system_context.h"
#include "storage/browser/file_system/file_system_features.h"
#include "storage/common/file_system/file_system_types.h"
#include "storage/common/file_system/file_system_util.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "third_party/blink/public/mojom/quota/quota_types.mojom.h"

namespace storage {

namespace {

static const FileSystemType kTemporaryAndPersistentAndSyncable[] = {
    kFileSystemTypeTemporary,
    kFileSystemTypePersistent,
    kFileSystemTypeSyncable,
};

template <typename T>
std::vector<T> MergeWithoutDuplicates(const std::vector<std::vector<T>>& tss) {
  if (tss.size() == 1) {
    // We assume that each vector contains no duplicates, already.
    return tss[0];
  }
  std::vector<T> merged;
  merged.reserve(std::accumulate(
      tss.begin(), tss.end(), 0U,
      [](size_t acc, const std::vector<T>& ts) { return acc + ts.size(); }));
  for (const auto& ts : tss) {
    merged.insert(merged.end(), ts.begin(), ts.end());
  }
  std::ranges::sort(merged);
  auto repeated = std::ranges::unique(merged);
  merged.erase(repeated.begin(), repeated.end());
  return merged;
}

std::vector<blink::StorageKey> GetDefaultStorageKeysOnFileTaskRunner(
    FileSystemContext* context,
    FileSystemType type) {
  FileSystemQuotaUtil* quota_util = context->GetQuotaUtil(type);
  if (!quota_util)
    return {};
  return quota_util->GetDefaultStorageKeysOnFileTaskRunner(type);
}

blink::mojom::QuotaStatusCode DeleteBucketOnFileTaskRunner(
    FileSystemContext* context,
    const BucketLocator& bucket_locator,
    FileSystemType type) {
  FileSystemBackend* provider = context->GetFileSystemBackend(type);
  if (!provider || !provider->GetQuotaUtil())
    return blink::mojom::QuotaStatusCode::kErrorNotSupported;
  base::File::Error result =
      provider->GetQuotaUtil()->DeleteBucketDataOnFileTaskRunner(
          context, context->quota_manager_proxy().get(), bucket_locator, type);

  // If obfuscated_file_util() was caching this default bucket, it should be
  // deleted as well. If it was not cached, result is a no-op.
  if (bucket_locator.is_default) {
    provider->GetQuotaUtil()->DeleteCachedDefaultBucket(
        bucket_locator.storage_key);
  }

  if (result == base::File::FILE_OK)
    return blink::mojom::QuotaStatusCode::kOk;
  return blink::mojom::QuotaStatusCode::kErrorInvalidModification;
}

void PerformStorageCleanupOnFileTaskRunner(FileSystemContext* context,
                                           FileSystemType type) {
  FileSystemBackend* provider = context->GetFileSystemBackend(type);
  if (!provider || !provider->GetQuotaUtil())
    return;
  provider->GetQuotaUtil()->PerformStorageCleanupOnFileTaskRunner(
      context, context->quota_manager_proxy().get(), type);
}

}  // namespace

FileSystemQuotaClient::FileSystemQuotaClient(
    FileSystemContext* file_system_context)
    : file_system_context_(file_system_context) {
  DCHECK(file_system_context_);
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

FileSystemQuotaClient::~FileSystemQuotaClient() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void FileSystemQuotaClient::GetBucketUsage(const BucketLocator& bucket,
                                           GetBucketUsageCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!callback.is_null());

  auto types = GetFileSystemTypes();
  base::RepeatingCallback<void(int64_t)> barrier =
      base::BarrierCallback<int64_t>(
          types.size(), base::BindOnce([](std::vector<int64_t> usages) {
                          return std::accumulate(usages.begin(), usages.end(),
                                                 0U);
                        }).Then(std::move(callback)));

  for (auto type : types) {
    FileSystemQuotaUtil* quota_util = file_system_context_->GetQuotaUtil(type);
    if (quota_util) {
      file_task_runner()->PostTaskAndReplyWithResult(
          FROM_HERE,
          // It is safe to pass Unretained(quota_util) since context owns it.
          base::BindOnce(&FileSystemQuotaUtil::GetBucketUsageOnFileTaskRunner,
                         base::Unretained(quota_util),
                         base::RetainedRef(file_system_context_.get()), bucket,
                         type),
          barrier);
    } else {
      barrier.Run(0);
    }
  }
}

void FileSystemQuotaClient::GetDefaultStorageKeys(
    GetDefaultStorageKeysCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!callback.is_null());

  auto types = GetFileSystemTypes();
  base::RepeatingCallback<void(std::vector<blink::StorageKey>)> barrier =
      base::BarrierCallback<std::vector<blink::StorageKey>>(
          types.size(),
          base::BindOnce(&MergeWithoutDuplicates<blink::StorageKey>)
              .Then(std::move(callback)));

  for (auto type : types) {
    file_task_runner()->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(&GetDefaultStorageKeysOnFileTaskRunner,
                       base::RetainedRef(file_system_context_.get()), type),
        barrier);
  }
}

void FileSystemQuotaClient::DeleteBucketData(
    const BucketLocator& bucket,
    DeleteBucketDataCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!callback.is_null());

  auto fs_types = GetFileSystemTypes();
  auto wrapped_callback = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(callback), blink::mojom::QuotaStatusCode::kUnknown);
  base::RepeatingCallback<void(blink::mojom::QuotaStatusCode)> barrier =
      base::BarrierCallback<blink::mojom::QuotaStatusCode>(
          fs_types.size(),
          base::BindOnce([](const std::vector<blink::mojom::QuotaStatusCode>&
                                statuses) {
            for (auto status : statuses) {
              if (status != blink::mojom::QuotaStatusCode::kOk) {
                return status;
              }
            }
            return blink::mojom::QuotaStatusCode::kOk;
          }).Then(std::move(wrapped_callback)));

  for (const auto fs_type : fs_types) {
    file_task_runner()->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(&DeleteBucketOnFileTaskRunner,
                       base::RetainedRef(file_system_context_.get()), bucket,
                       fs_type),
        barrier);
  }
}

void FileSystemQuotaClient::PerformStorageCleanup(
    PerformStorageCleanupCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!callback.is_null());

  auto fs_types = GetFileSystemTypes();
  base::RepeatingClosure barrier =
      base::BarrierClosure(fs_types.size(), std::move(callback));

  for (auto fs_type : fs_types) {
    file_task_runner()->PostTaskAndReply(
        FROM_HERE,
        base::BindOnce(&PerformStorageCleanupOnFileTaskRunner,
                       base::RetainedRef(file_system_context_.get()), fs_type),
        barrier);
  }
}

base::SequencedTaskRunner* FileSystemQuotaClient::file_task_runner() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return file_system_context_->default_file_task_runner();
}

std::vector<FileSystemType> FileSystemQuotaClient::GetFileSystemTypes() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<FileSystemType> backend_types =
      file_system_context_->GetFileSystemTypes();
  std::vector<FileSystemType> fs_types;
  for (auto fs_type : kTemporaryAndPersistentAndSyncable) {
    if (base::Contains(backend_types, fs_type)) {
      fs_types.push_back(fs_type);
    }
  }
  return fs_types;
}

}  // namespace storage
