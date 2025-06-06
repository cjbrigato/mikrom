// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/fileapi/diversion_backend_delegate.h"

#include "chrome/browser/ash/fileapi/copy_from_fd.h"
#include "content/public/browser/browser_thread.h"
#include "storage/browser/file_system/file_system_url.h"
#include "storage/common/file_system/file_system_util.h"

namespace ash {

namespace {

constexpr auto kDiversionFileIdleTimeout = base::Seconds(5);

DiversionBackendDelegate::Policy ShouldDivert(
    const storage::FileSystemURL& url) {
  // For now (January 2024), we'll be conservative and only apply diversion to
  // "crdownload" files (from browser downloads) or "crswap" files (as
  // generated by File System Access code) that are freshly created. Later, we
  // could roll out diversion more widely.
  //
  // If we were to divert previously-existing files (not only diverting freshly
  // created files), as kDivertMingled (not kDivertIsolated), then we'd need to
  // initialize the Diversion File's contents (via a FileStreamWriter), just
  // after the StartDiverting call, before running the
  // EnsureFileExistsCallback. There would be the usual trade-offs here between
  // lazy and eager initialization of those file contents.
  const std::string& extension = url.virtual_path().FinalExtension();
  if ((extension == ".crdownload") || (extension == ".crswap")) {
    return DiversionBackendDelegate::Policy::kDivertIsolated;
  } else if (extension == ".cros_divert_mingled_test") {
    return DiversionBackendDelegate::Policy::kDivertMingled;
  }
  return DiversionBackendDelegate::Policy::kDoNotDivert;
}

// Duplicates a FileSystemOperationContext, returning something (a unique_ptr)
// with independent ownership.
std::unique_ptr<storage::FileSystemOperationContext>
DuplicateFileSystemOperationContext(
    const storage::FileSystemOperationContext& original) {
  return std::make_unique<storage::FileSystemOperationContext>(
      original.file_system_context(), original.task_runner());
}

}  // namespace

DiversionBackendDelegate::DiversionBackendDelegate(
    std::unique_ptr<FileSystemBackendDelegate> wrappee)
    : wrappee_(std::move(wrappee)),
      diversion_file_manager_(base::MakeRefCounted<DiversionFileManager>()) {
  CHECK(wrappee_);
}

DiversionBackendDelegate::~DiversionBackendDelegate() = default;

storage::AsyncFileUtil* DiversionBackendDelegate::GetAsyncFileUtil(
    storage::FileSystemType type) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  return this;
}

std::unique_ptr<storage::FileStreamReader>
DiversionBackendDelegate::CreateFileStreamReader(
    const storage::FileSystemURL& url,
    int64_t offset,
    int64_t max_bytes_to_read,
    const base::Time& expected_modification_time,
    storage::FileSystemContext* context) {
  // TODO: honor max_bytes_to_read. On the other hand,
  // storage::FileStreamReader::CreateForLocalFile also doesn't honor it.
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  if (std::unique_ptr<storage::FileStreamReader> fs_reader =
          diversion_file_manager_->CreateDivertedFileStreamReader(url,
                                                                  offset)) {
    return fs_reader;
  }
  return wrappee_->CreateFileStreamReader(url, offset, max_bytes_to_read,
                                          expected_modification_time, context);
}

std::unique_ptr<storage::FileStreamWriter>
DiversionBackendDelegate::CreateFileStreamWriter(
    const storage::FileSystemURL& url,
    int64_t offset,
    storage::FileSystemContext* context) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  if (std::unique_ptr<storage::FileStreamWriter> fs_writer =
          diversion_file_manager_->CreateDivertedFileStreamWriter(url,
                                                                  offset)) {
    return fs_writer;
  }
  return wrappee_->CreateFileStreamWriter(url, offset, context);
}

storage::WatcherManager* DiversionBackendDelegate::GetWatcherManager(
    storage::FileSystemType type) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  return wrappee_->GetWatcherManager(type);
}

void DiversionBackendDelegate::CreateOrOpen(
    std::unique_ptr<storage::FileSystemOperationContext> context,
    const storage::FileSystemURL& url,
    uint32_t file_flags,
    CreateOrOpenCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  storage::AsyncFileUtil* af_util = wrappee_->GetAsyncFileUtil(url.type());
  af_util->CreateOrOpen(std::move(context), url, file_flags,
                        std::move(callback));
}

void DiversionBackendDelegate::EnsureFileExists(
    std::unique_ptr<storage::FileSystemOperationContext> context,
    const storage::FileSystemURL& url,
    EnsureFileExistsCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  storage::AsyncFileUtil* af_util = wrappee_->GetAsyncFileUtil(url.type());

  if (!url.is_valid()) {
    std::move(callback).Run(base::File::FILE_ERROR_INVALID_URL,
                            /*created=*/false);
    return;
  }
  const Policy policy = ShouldDivert(url);
  if (policy == Policy::kDoNotDivert) {
    af_util->EnsureFileExists(std::move(context), url, std::move(callback));
    return;
  } else if (diversion_file_manager_->IsDiverting(url)) {
    std::move(callback).Run(base::File::FILE_OK, /*created=*/false);
    return;
  } else if (policy == Policy::kDivertIsolated) {
    auto sd_result = diversion_file_manager_->StartDiverting(
        url, kDiversionFileIdleTimeout,
        base::BindOnce(&DiversionBackendDelegate::OnDiversionFinished,
                       weak_ptr_factory_.GetWeakPtr(),
                       OnDiversionFinishedCallSite::kEnsureFileExists,
                       std::move(context), url,
                       storage::AsyncFileUtil::StatusCallback()));
    bool created = sd_result == DiversionFileManager::StartDivertingResult::kOK;
    std::move(callback).Run(base::File::FILE_OK, created);
    return;
  }

  static constexpr auto on_get_file_info =
      [](base::WeakPtr<DiversionBackendDelegate> weak_ptr,
         scoped_refptr<DiversionFileManager> diversion_file_manager,
         std::unique_ptr<storage::FileSystemOperationContext>
             duplicated_context,
         const storage::FileSystemURL& url, EnsureFileExistsCallback callback,
         base::File::Error gfi_result, const base::File::Info& file_info) {
        DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
        bool created = false;
        if (gfi_result == base::File::FILE_OK) {
          std::move(callback).Run(file_info.is_directory
                                      ? base::File::FILE_ERROR_NOT_A_FILE
                                      : base::File::FILE_OK,
                                  created);
        } else if (gfi_result == base::File::FILE_ERROR_NOT_FOUND) {
          auto sd_result = diversion_file_manager->StartDiverting(
              url, kDiversionFileIdleTimeout,
              base::BindOnce(&DiversionBackendDelegate::OnDiversionFinished,
                             std::move(weak_ptr),
                             OnDiversionFinishedCallSite::kEnsureFileExists,
                             std::move(duplicated_context), url,
                             storage::AsyncFileUtil::StatusCallback()));
          created =
              sd_result == DiversionFileManager::StartDivertingResult::kOK;
          std::move(callback).Run(base::File::FILE_OK, created);
        } else {
          std::move(callback).Run(gfi_result, created);
        }
      };

  std::unique_ptr<storage::FileSystemOperationContext> fsoc0 =
      DuplicateFileSystemOperationContext(*context);
  std::unique_ptr<storage::FileSystemOperationContext> fsoc1 =
      std::move(context);

  af_util->GetFileInfo(
      std::move(fsoc0), url,
      {storage::FileSystemOperation::GetMetadataField::kIsDirectory},
      base::BindOnce(on_get_file_info, weak_ptr_factory_.GetWeakPtr(),
                     diversion_file_manager_, std::move(fsoc1), url,
                     std::move(callback)));
}

void DiversionBackendDelegate::CreateDirectory(
    std::unique_ptr<storage::FileSystemOperationContext> context,
    const storage::FileSystemURL& url,
    bool exclusive,
    bool recursive,
    StatusCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  storage::AsyncFileUtil* af_util = wrappee_->GetAsyncFileUtil(url.type());
  af_util->CreateDirectory(std::move(context), url, exclusive, recursive,
                           std::move(callback));
}

void DiversionBackendDelegate::GetFileInfo(
    std::unique_ptr<storage::FileSystemOperationContext> context,
    const storage::FileSystemURL& url,
    GetMetadataFieldSet fields,
    GetFileInfoCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  if (diversion_file_manager_->IsDiverting(url)) {
    diversion_file_manager_->GetDivertedFileInfo(url, fields,
                                                 std::move(callback));
    return;
  } else if (ShouldDivert(url) == Policy::kDivertIsolated) {
    std::move(callback).Run(base::File::FILE_ERROR_NOT_FOUND,
                            base::File::Info());
    return;
  }
  storage::AsyncFileUtil* af_util = wrappee_->GetAsyncFileUtil(url.type());
  af_util->GetFileInfo(std::move(context), url, fields, std::move(callback));
}

void DiversionBackendDelegate::ReadDirectory(
    std::unique_ptr<storage::FileSystemOperationContext> context,
    const storage::FileSystemURL& url,
    ReadDirectoryCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  storage::AsyncFileUtil* af_util = wrappee_->GetAsyncFileUtil(url.type());
  af_util->ReadDirectory(std::move(context), url, std::move(callback));
}

void DiversionBackendDelegate::Touch(
    std::unique_ptr<storage::FileSystemOperationContext> context,
    const storage::FileSystemURL& url,
    const base::Time& last_access_time,
    const base::Time& last_modified_time,
    StatusCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  if (diversion_file_manager_->IsDiverting(url)) {
    // TODO: touch the O_TMPFILE file.
  } else if (ShouldDivert(url) == Policy::kDivertIsolated) {
    std::move(callback).Run(base::File::FILE_ERROR_NOT_FOUND);
    return;
  }
  storage::AsyncFileUtil* af_util = wrappee_->GetAsyncFileUtil(url.type());
  af_util->Touch(std::move(context), url, last_access_time, last_modified_time,
                 std::move(callback));
}

void DiversionBackendDelegate::Truncate(
    std::unique_ptr<storage::FileSystemOperationContext> context,
    const storage::FileSystemURL& url,
    int64_t length,
    StatusCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  if (diversion_file_manager_->IsDiverting(url)) {
    diversion_file_manager_->TruncateDivertedFile(url, length,
                                                  std::move(callback));
    return;
  } else if (ShouldDivert(url) == Policy::kDivertIsolated) {
    std::move(callback).Run(base::File::FILE_ERROR_NOT_FOUND);
    return;
  }
  storage::AsyncFileUtil* af_util = wrappee_->GetAsyncFileUtil(url.type());
  af_util->Truncate(std::move(context), url, length, std::move(callback));
}

void DiversionBackendDelegate::CopyFileLocal(
    std::unique_ptr<storage::FileSystemOperationContext> context,
    const storage::FileSystemURL& src_url,
    const storage::FileSystemURL& dest_url,
    CopyOrMoveOptionSet options,
    CopyFileProgressCallback progress_callback,
    StatusCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  if (!src_url.is_valid() || !dest_url.is_valid()) {
    std::move(callback).Run(base::File::FILE_ERROR_INVALID_URL);
    return;
  } else if (src_url == dest_url) {
    std::move(callback).Run(base::File::FILE_OK);
    return;
  }

  if (diversion_file_manager_->IsDiverting(dest_url)) {
    // Passing a null DiversionFileManager::Callback deletes the diversion file.
    diversion_file_manager_->FinishDiverting(dest_url,
                                             DiversionFileManager::Callback());
  }

  if (diversion_file_manager_->IsDiverting(src_url)) {
    diversion_file_manager_->FinishDiverting(
        src_url,
        base::BindOnce(&DiversionBackendDelegate::OnDiversionFinished,
                       weak_ptr_factory_.GetWeakPtr(),
                       OnDiversionFinishedCallSite::kCopyFileLocal,
                       std::move(context), dest_url, std::move(callback)));
    return;
  } else if (ShouldDivert(src_url) == Policy::kDivertIsolated) {
    std::move(callback).Run(base::File::FILE_ERROR_NOT_FOUND);
    return;
  }

  storage::AsyncFileUtil* af_util = wrappee_->GetAsyncFileUtil(dest_url.type());
  af_util->CopyFileLocal(std::move(context), src_url, dest_url,
                         std::move(options), std::move(progress_callback),
                         std::move(callback));
}

void DiversionBackendDelegate::MoveFileLocal(
    std::unique_ptr<storage::FileSystemOperationContext> context,
    const storage::FileSystemURL& src_url,
    const storage::FileSystemURL& dest_url,
    CopyOrMoveOptionSet options,
    StatusCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  if (!src_url.is_valid() || !dest_url.is_valid()) {
    std::move(callback).Run(base::File::FILE_ERROR_INVALID_URL);
    return;
  } else if (src_url == dest_url) {
    std::move(callback).Run(base::File::FILE_OK);
    return;
  }

  if (diversion_file_manager_->IsDiverting(src_url)) {
    diversion_file_manager_->FinishDiverting(
        src_url,
        base::BindOnce(&DiversionBackendDelegate::OnDiversionFinished,
                       weak_ptr_factory_.GetWeakPtr(),
                       OnDiversionFinishedCallSite::kMoveFileLocal,
                       std::move(context), dest_url, std::move(callback)));
    return;
  } else if (ShouldDivert(src_url) == Policy::kDivertIsolated) {
    std::move(callback).Run(base::File::FILE_ERROR_NOT_FOUND);
    return;
  } else if (diversion_file_manager_->IsDiverting(dest_url)) {
    // Passing a null DiversionFileManager::Callback deletes the diversion file.
    diversion_file_manager_->FinishDiverting(dest_url,
                                             DiversionFileManager::Callback());
  }

  storage::AsyncFileUtil* af_util = wrappee_->GetAsyncFileUtil(dest_url.type());
  af_util->MoveFileLocal(std::move(context), src_url, dest_url,
                         std::move(options), std::move(callback));
}

void DiversionBackendDelegate::CopyInForeignFile(
    std::unique_ptr<storage::FileSystemOperationContext> context,
    const base::FilePath& src_file_path,
    const storage::FileSystemURL& dest_url,
    StatusCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  storage::AsyncFileUtil* af_util = wrappee_->GetAsyncFileUtil(dest_url.type());
  af_util->CopyInForeignFile(std::move(context), src_file_path, dest_url,
                             std::move(callback));
}

void DiversionBackendDelegate::DeleteFile(
    std::unique_ptr<storage::FileSystemOperationContext> context,
    const storage::FileSystemURL& url,
    StatusCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  if (diversion_file_manager_->IsDiverting(url)) {
    // Passing a null DiversionFileManager::Callback deletes the diversion file.
    diversion_file_manager_->FinishDiverting(url,
                                             DiversionFileManager::Callback());
    if (callback) {
      callback = base::BindOnce(
          [](StatusCallback inner_callback, base::File::Error result) {
            if (result == base::File::FILE_ERROR_NOT_FOUND) {
              result = base::File::FILE_OK;
            }
            std::move(inner_callback).Run(result);
          },
          std::move(callback));
    }
  } else if (ShouldDivert(url) == Policy::kDivertIsolated) {
    std::move(callback).Run(base::File::FILE_ERROR_NOT_FOUND);
    return;
  }
  storage::AsyncFileUtil* af_util = wrappee_->GetAsyncFileUtil(url.type());
  af_util->DeleteFile(std::move(context), url, std::move(callback));
}

void DiversionBackendDelegate::DeleteDirectory(
    std::unique_ptr<storage::FileSystemOperationContext> context,
    const storage::FileSystemURL& url,
    StatusCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  storage::AsyncFileUtil* af_util = wrappee_->GetAsyncFileUtil(url.type());
  af_util->DeleteDirectory(std::move(context), url, std::move(callback));
}

void DiversionBackendDelegate::DeleteRecursively(
    std::unique_ptr<storage::FileSystemOperationContext> context,
    const storage::FileSystemURL& url,
    StatusCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  // TODO: this could be tricky if a diverted-file is a descendent of the "url
  // to be deleted recursively".
  storage::AsyncFileUtil* af_util = wrappee_->GetAsyncFileUtil(url.type());
  af_util->DeleteRecursively(std::move(context), url, std::move(callback));
}

void DiversionBackendDelegate::CreateSnapshotFile(
    std::unique_ptr<storage::FileSystemOperationContext> context,
    const storage::FileSystemURL& url,
    CreateSnapshotFileCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  // TODO: do we need to do anything here??
  storage::AsyncFileUtil* af_util = wrappee_->GetAsyncFileUtil(url.type());
  af_util->CreateSnapshotFile(std::move(context), url, std::move(callback));
}

void DiversionBackendDelegate::OverrideTmpfileDirForTesting(
    const base::FilePath& tmpfile_dir) {
  diversion_file_manager_->OverrideTmpfileDirForTesting(  // IN-TEST
      tmpfile_dir);
}

// static
DiversionBackendDelegate::Policy
DiversionBackendDelegate::ShouldDivertForTesting(
    const storage::FileSystemURL& url) {
  return ShouldDivert(url);
}

// static
base::TimeDelta DiversionBackendDelegate::IdleTimeoutForTesting() {
  return kDiversionFileIdleTimeout;
}

// static
void DiversionBackendDelegate::OnDiversionFinished(
    base::WeakPtr<DiversionBackendDelegate> weak_ptr,
    OnDiversionFinishedCallSite call_site,
    std::unique_ptr<storage::FileSystemOperationContext> context,
    const storage::FileSystemURL& dest_url,
    storage::AsyncFileUtil::StatusCallback callback,
    DiversionFileManager::StoppedReason stopped_reason,
    const storage::FileSystemURL& src_url,
    base::ScopedFD scoped_fd,
    int64_t file_size,
    base::File::Error error) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  CHECK(dest_url.is_valid());
  CHECK(src_url.is_valid());

  if (error != base::File::FILE_OK) {
    if (callback) {
      std::move(callback).Run(error);
    }
    return;
  } else if (!weak_ptr || !context || !scoped_fd.is_valid()) {
    if (callback) {
      std::move(callback).Run(base::File::FILE_ERROR_FAILED);
    }
    return;
  }

  // TODO(b/289322939): when wrapping a FileSystemProvider backend, can it
  // accept a Blob instead of using CopyFromFileDescriptor? The latter might
  // need the FileSystemProvider JS implementation to buffer the entire file's
  // contents in memory at once, which will obviously fail if those contents
  // are larger than the amount of available RAM.

  static constexpr auto ignore_file_error_not_found =
      [](storage::AsyncFileUtil::StatusCallback callback,
         base::File::Error file_error) {
        if (file_error == base::File::FILE_ERROR_NOT_FOUND) {
          file_error = base::File::FILE_OK;
        }
        if (callback) {
          std::move(callback).Run(file_error);
        }
      };

  static constexpr auto on_copy_complete =
      [](base::WeakPtr<DiversionBackendDelegate> weak_ptr,
         OnDiversionFinishedCallSite call_site,
         std::unique_ptr<storage::FileSystemOperationContext> context,
         const storage::FileSystemURL& src_url,
         const storage::FileSystemURL& dest_url,
         storage::AsyncFileUtil::StatusCallback callback,
         DiversionFileManager::StoppedReason stopped_reason, int64_t file_size,
         base::ScopedFD scoped_fd,
         std::unique_ptr<storage::FileStreamWriter> fs_writer,
         net::Error net_error) {
        DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

        if (src_url == dest_url) {
          if (callback) {
            std::move(callback).Run(storage::NetErrorToFileError(net_error));
          }
          return;
        }
        CHECK_NE(call_site, OnDiversionFinishedCallSite::kEnsureFileExists);

        if (callback && (net_error != net::OK)) {
          base::File::Error first_error =
              storage::NetErrorToFileError(net_error);
          callback = base::BindOnce(
              [](storage::AsyncFileUtil::StatusCallback inner_callback,
                 base::File::Error first_error_is_passed_on,
                 base::File::Error second_error_is_ignored) {
                std::move(inner_callback).Run(first_error_is_passed_on);
              },
              std::move(callback), first_error);
        }

        if (!weak_ptr) {
          if (callback) {
            std::move(callback).Run(base::File::FILE_ERROR_FAILED);
          }
          return;
        }

        switch (call_site) {
          case OnDiversionFinishedCallSite::kEnsureFileExists:
            NOTREACHED();

          case OnDiversionFinishedCallSite::kCopyFileLocal: {
            if (!scoped_fd.is_valid()) {
              std::move(callback).Run(base::File::FILE_ERROR_FAILED);
              return;
            }
            // Copy from that scoped_fd again, but this time to src_url instead
            // of to dest_url.
            lseek(scoped_fd.get(), 0, SEEK_SET);
            weak_ptr->OnDiversionFinished(
                weak_ptr, call_site, std::move(context), src_url,
                std::move(callback), stopped_reason, src_url,
                std::move(scoped_fd), file_size, base::File::FILE_OK);
            break;
          }

          case OnDiversionFinishedCallSite::kMoveFileLocal:
            if (ShouldDivert(src_url) == Policy::kDivertIsolated) {
              std::move(callback).Run(base::File::FILE_OK);
            } else {
              weak_ptr->wrappee_->GetAsyncFileUtil(src_url.type())
                  ->DeleteFile(std::move(context), src_url,
                               base::BindOnce(ignore_file_error_not_found,
                                              std::move(callback)));
            }
            break;
        }
      };

  static constexpr auto on_truncated =
      [](base::WeakPtr<DiversionBackendDelegate> weak_ptr,
         OnDiversionFinishedCallSite call_site,
         std::unique_ptr<storage::FileSystemOperationContext> context,
         base::ScopedFD scoped_fd, const storage::FileSystemURL& src_url,
         const storage::FileSystemURL& dest_url,
         storage::AsyncFileUtil::StatusCallback callback,
         DiversionFileManager::StoppedReason stopped_reason, int64_t file_size,
         base::File::Error result) {
        if (result != base::File::FILE_OK) {
          if (callback) {
            std::move(callback).Run(result);
          }
          return;
        } else if (!weak_ptr) {
          if (callback) {
            std::move(callback).Run(base::File::FILE_ERROR_FAILED);
          }
          return;
        }

        // TODO: for now, we are assuming that "write to foo.dat" is atomic:
        // other clients reading "foo.dat" will either see the complete old
        // version (if it existed) or the complete new version but not a
        // partial prefix of the new version. Specifically for ODFS+FSP, file
        // upload is indeed atomic.
        //
        // More generally, we may have to take multiple steps (the Google Drive
        // team call this a "File Dance"), first writing to a temporary sibling
        // file and then moving the sibling over the ultimate destination. But
        // this is obviously more complicated and has more failure states.
        //
        // Ideally, the wrappee FileSystemBackendDelegate or AsyncFileUtil
        // would be able to say whether it is capable of atomic upload (see
        // also b/289322939). Absent that, it's simplest to assume that it can.
        std::unique_ptr<storage::FileStreamWriter> fs_writer =
            weak_ptr->wrappee_->CreateFileStreamWriter(
                dest_url, 0, context->file_system_context());

        CopyFromFileDescriptor(
            std::move(scoped_fd), std::move(fs_writer),
            dest_url.mount_option().flush_policy(),
            base::BindOnce(on_copy_complete, std::move(weak_ptr), call_site,
                           std::move(context), src_url, dest_url,
                           std::move(callback), stopped_reason, file_size));
      };

  static constexpr auto on_ensure_file_exists =
      [](base::WeakPtr<DiversionBackendDelegate> weak_ptr,
         OnDiversionFinishedCallSite call_site,
         std::unique_ptr<storage::FileSystemOperationContext> context,
         base::ScopedFD scoped_fd, const storage::FileSystemURL& src_url,
         const storage::FileSystemURL& dest_url,
         storage::AsyncFileUtil::StatusCallback callback,
         DiversionFileManager::StoppedReason stopped_reason, int64_t file_size,
         base::File::Error result, bool created) {
        if (result != base::File::FILE_OK) {
          if (callback) {
            std::move(callback).Run(result);
          }
          return;
        } else if (!weak_ptr) {
          if (callback) {
            std::move(callback).Run(base::File::FILE_ERROR_FAILED);
          }
          return;
        } else if (created) {
          on_truncated(std::move(weak_ptr), call_site, std::move(context),
                       std::move(scoped_fd), src_url, dest_url,
                       std::move(callback), stopped_reason, file_size,
                       base::File::FILE_OK);
          return;
        }

        std::unique_ptr<storage::FileSystemOperationContext> fsoc0 =
            DuplicateFileSystemOperationContext(*context);
        std::unique_ptr<storage::FileSystemOperationContext> fsoc1 =
            std::move(context);

        FileSystemBackendDelegate* wrappee = weak_ptr->wrappee_.get();
        wrappee->GetAsyncFileUtil(dest_url.type())
            ->Truncate(
                std::move(fsoc0), dest_url, 0,
                base::BindOnce(on_truncated, std::move(weak_ptr), call_site,
                               std::move(fsoc1), std::move(scoped_fd), src_url,
                               dest_url, std::move(callback), stopped_reason,
                               file_size));
      };

  static constexpr auto on_get_file_info =
      [](base::WeakPtr<DiversionBackendDelegate> weak_ptr,
         OnDiversionFinishedCallSite call_site,
         std::unique_ptr<storage::FileSystemOperationContext> context,
         base::ScopedFD scoped_fd, const storage::FileSystemURL& src_url,
         const storage::FileSystemURL& dest_url,
         storage::AsyncFileUtil::StatusCallback callback,
         DiversionFileManager::StoppedReason stopped_reason, int64_t file_size,
         base::File::Error result, const base::File::Info& file_info) {
        if (result == base::File::FILE_ERROR_NOT_FOUND) {
          // No-op. Every other if-else branch returns.
        } else if (result != base::File::FILE_OK) {
          if (callback) {
            std::move(callback).Run(result);
          }
          return;
        } else if (file_info.is_directory) {
          if (callback) {
            std::move(callback).Run(base::File::FILE_ERROR_NOT_A_FILE);
          }
          return;
        } else if (file_info.size != 0) {
          on_ensure_file_exists(std::move(weak_ptr), call_site,
                                std::move(context), std::move(scoped_fd),
                                src_url, dest_url, std::move(callback),
                                stopped_reason, file_size, base::File::FILE_OK,
                                /*created=*/false);
          return;
        } else {
          on_truncated(std::move(weak_ptr), call_site, std::move(context),
                       std::move(scoped_fd), src_url, dest_url,
                       std::move(callback), stopped_reason, file_size,
                       base::File::FILE_OK);
          return;
        }

        std::unique_ptr<storage::FileSystemOperationContext> fsoc0 =
            DuplicateFileSystemOperationContext(*context);
        std::unique_ptr<storage::FileSystemOperationContext> fsoc1 =
            std::move(context);

        FileSystemBackendDelegate* wrappee = weak_ptr->wrappee_.get();
        wrappee->GetAsyncFileUtil(dest_url.type())
            ->EnsureFileExists(
                std::move(fsoc0), dest_url,
                base::BindOnce(on_ensure_file_exists, std::move(weak_ptr),
                               call_site, std::move(fsoc1),
                               std::move(scoped_fd), src_url, dest_url,
                               std::move(callback), stopped_reason, file_size));
      };

  std::unique_ptr<storage::FileSystemOperationContext> fsoc0 =
      DuplicateFileSystemOperationContext(*context);
  std::unique_ptr<storage::FileSystemOperationContext> fsoc1 =
      std::move(context);

  FileSystemBackendDelegate* wrappee = weak_ptr->wrappee_.get();
  wrappee->GetAsyncFileUtil(dest_url.type())
      ->GetFileInfo(
          std::move(fsoc0), dest_url,
          {storage::FileSystemOperation::GetMetadataField::kSize,
           storage::FileSystemOperation::GetMetadataField::kIsDirectory},
          base::BindOnce(on_get_file_info, std::move(weak_ptr), call_site,
                         std::move(fsoc1), std::move(scoped_fd), src_url,
                         dest_url, std::move(callback), stopped_reason,
                         file_size));
}

}  // namespace ash
