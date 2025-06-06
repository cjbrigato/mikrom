// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/indexed_db/instance/sqlite/backing_store_transaction_impl.h"

#include "base/check.h"
#include "base/notimplemented.h"
#include "base/types/expected_macros.h"
#include "content/browser/indexed_db/indexed_db_value.h"
#include "content/browser/indexed_db/instance/sqlite/database_connection.h"
#include "content/browser/indexed_db/status.h"
#include "sql/transaction.h"

namespace content::indexed_db::sqlite {

BackingStoreTransactionImpl::BackingStoreTransactionImpl(
    base::WeakPtr<DatabaseConnection> db,
    std::unique_ptr<sql::Transaction> transaction,
    blink::mojom::IDBTransactionDurability durability,
    blink::mojom::IDBTransactionMode mode)
    : db_(std::move(db)),
      transaction_(std::move(transaction)),
      durability_(durability),
      mode_(mode) {}

BackingStoreTransactionImpl::~BackingStoreTransactionImpl() = default;

void BackingStoreTransactionImpl::Begin(std::vector<PartitionedLock> locks) {
  // TODO(crbug.com/40253999): How do we surface the error if this call fails?
  CHECK(transaction_->Begin());
  db_->OnTransactionBegin(PassKey(), *this);
}

Status BackingStoreTransactionImpl::CommitPhaseOne(BlobWriteCallback callback) {
  return std::move(callback).Run(
      BlobWriteResult::kRunPhaseTwoAndReturnResult,
      storage::mojom::WriteBlobToFileResult::kSuccess);
}

Status BackingStoreTransactionImpl::CommitPhaseTwo() {
  db_->OnBeforeTransactionCommit(PassKey(), *this);
  transaction_->Commit();
  db_->OnTransactionCommit(PassKey(), *this);
  return Status::OK();
}

void BackingStoreTransactionImpl::Rollback() {
  transaction_->Rollback();
  db_->OnTransactionRollback(PassKey(), *this);
}

Status BackingStoreTransactionImpl::SetDatabaseVersion(int64_t version) {
  return db_->SetDatabaseVersion(PassKey(), version);
}

Status BackingStoreTransactionImpl::CreateObjectStore(
    int64_t object_store_id,
    const std::u16string& name,
    blink::IndexedDBKeyPath key_path,
    bool auto_increment) {
  return db_->CreateObjectStore(PassKey(), object_store_id, name,
                                std::move(key_path), auto_increment);
}

Status BackingStoreTransactionImpl::DeleteObjectStore(int64_t object_store_id) {
  NOTIMPLEMENTED();
  return Status::InvalidArgument("Not implemented");
}

Status BackingStoreTransactionImpl::RenameObjectStore(
    int64_t object_store_id,
    const std::u16string& new_name) {
  NOTIMPLEMENTED();
  return Status::InvalidArgument("Not implemented");
}

Status BackingStoreTransactionImpl::ClearObjectStore(int64_t object_store_id) {
  NOTIMPLEMENTED();
  return Status::InvalidArgument("Not implemented");
}

Status BackingStoreTransactionImpl::CreateIndex(
    int64_t object_store_id,
    blink::IndexedDBIndexMetadata index) {
  NOTIMPLEMENTED();
  return Status::InvalidArgument("Not implemented");
}

Status BackingStoreTransactionImpl::DeleteIndex(int64_t object_store_id,
                                                int64_t index_id) {
  NOTIMPLEMENTED();
  return Status::InvalidArgument("Not implemented");
}

Status BackingStoreTransactionImpl::RenameIndex(
    int64_t object_store_id,
    int64_t index_id,
    const std::u16string& new_name) {
  NOTIMPLEMENTED();
  return Status::InvalidArgument("Not implemented");
}

StatusOr<IndexedDBValue> BackingStoreTransactionImpl::GetRecord(
    int64_t object_store_id,
    const blink::IndexedDBKey& key) {
  return db_->GetValue(PassKey(), object_store_id, key);
}

StatusOr<BackingStore::RecordIdentifier> BackingStoreTransactionImpl::PutRecord(
    int64_t object_store_id,
    const blink::IndexedDBKey& key,
    IndexedDBValue value) {
  return db_->PutRecord(PassKey(), object_store_id, key, std::move(value));
}

Status BackingStoreTransactionImpl::DeleteRange(
    int64_t object_store_id,
    const blink::IndexedDBKeyRange&) {
  NOTIMPLEMENTED();
  return Status::InvalidArgument("Not implemented");
}

StatusOr<int64_t> BackingStoreTransactionImpl::GetKeyGeneratorCurrentNumber(
    int64_t object_store_id) {
  return db_->GetKeyGeneratorCurrentNumber(PassKey(), object_store_id);
}

Status BackingStoreTransactionImpl::MaybeUpdateKeyGeneratorCurrentNumber(
    int64_t object_store_id,
    int64_t new_number,
    bool was_generated) {
  // The `was_generated` hint is not useful for SQLite as the check and update
  // can be made in a single SQL statement.
  return db_->MaybeUpdateKeyGeneratorCurrentNumber(PassKey(), object_store_id,
                                                   new_number);
}

StatusOr<std::optional<BackingStore::RecordIdentifier>>
BackingStoreTransactionImpl::KeyExistsInObjectStore(
    int64_t object_store_id,
    const blink::IndexedDBKey& key) {
  return db_->GetRecordIdentifierIfExists(PassKey(), object_store_id, key);
}

Status BackingStoreTransactionImpl::PutIndexDataForRecord(
    int64_t object_store_id,
    int64_t index_id,
    const blink::IndexedDBKey& key,
    const BackingStore::RecordIdentifier& record) {
  NOTIMPLEMENTED();
  return Status::InvalidArgument("Not implemented");
}

StatusOr<blink::IndexedDBKey>
BackingStoreTransactionImpl::GetPrimaryKeyViaIndex(
    int64_t object_store_id,
    int64_t index_id,
    const blink::IndexedDBKey& key) {
  NOTIMPLEMENTED();
  return base::unexpected(Status::InvalidArgument("not implemented"));
}

StatusOr<blink::IndexedDBKey> BackingStoreTransactionImpl::KeyExistsInIndex(
    int64_t object_store_id,
    int64_t index_id,
    const blink::IndexedDBKey& key) {
  NOTIMPLEMENTED();
  return base::unexpected(Status::InvalidArgument("Not implemented"));
}

StatusOr<uint32_t> BackingStoreTransactionImpl::GetObjectStoreKeyCount(
    int64_t object_store_id,
    blink::IndexedDBKeyRange key_range) {
  return db_->GetObjectStoreKeyCount(PassKey(), object_store_id,
                                     std::move(key_range));
}

StatusOr<uint32_t> BackingStoreTransactionImpl::GetIndexKeyCount(
    int64_t object_store_id,
    int64_t index_id,
    blink::IndexedDBKeyRange key_range) {
  NOTIMPLEMENTED();
  return base::unexpected(Status::InvalidArgument("Not implemented"));
}

StatusOr<std::unique_ptr<BackingStore::Cursor>>
BackingStoreTransactionImpl::OpenObjectStoreKeyCursor(
    int64_t object_store_id,
    const blink::IndexedDBKeyRange& key_range,
    blink::mojom::IDBCursorDirection) {
  NOTIMPLEMENTED();
  return base::unexpected(Status::InvalidArgument("Not implemented"));
}

StatusOr<std::unique_ptr<indexed_db::BackingStore::Cursor>>
BackingStoreTransactionImpl::OpenObjectStoreCursor(
    int64_t object_store_id,
    const blink::IndexedDBKeyRange& key_range,
    blink::mojom::IDBCursorDirection) {
  NOTIMPLEMENTED();
  return base::unexpected(Status::InvalidArgument("Not implemented"));
}

StatusOr<std::unique_ptr<indexed_db::BackingStore::Cursor>>
BackingStoreTransactionImpl::OpenIndexKeyCursor(
    int64_t object_store_id,
    int64_t index_id,
    const blink::IndexedDBKeyRange& key_range,
    blink::mojom::IDBCursorDirection) {
  NOTIMPLEMENTED();
  return base::unexpected(Status::InvalidArgument("Not implemented"));
}

StatusOr<std::unique_ptr<indexed_db::BackingStore::Cursor>>
BackingStoreTransactionImpl::OpenIndexCursor(
    int64_t object_store_id,
    int64_t index_id,
    const blink::IndexedDBKeyRange& key_range,
    blink::mojom::IDBCursorDirection) {
  NOTIMPLEMENTED();
  return base::unexpected(Status::InvalidArgument("Not implemented"));
}

}  // namespace content::indexed_db::sqlite
