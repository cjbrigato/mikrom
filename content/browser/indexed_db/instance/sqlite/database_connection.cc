// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/indexed_db/instance/sqlite/database_connection.h"

#include <memory>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/memory/ptr_util.h"
#include "base/notimplemented.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/types/expected.h"
#include "content/browser/indexed_db/indexed_db_value.h"
#include "content/browser/indexed_db/instance/backing_store.h"
#include "content/browser/indexed_db/instance/sqlite/backing_store_transaction_impl.h"
#include "content/browser/indexed_db/status.h"
#include "sql/database.h"
#include "sql/meta_table.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "third_party/blink/public/common/indexeddb/indexeddb_key.h"
#include "third_party/blink/public/common/indexeddb/indexeddb_key_path.h"
#include "third_party/blink/public/common/indexeddb/indexeddb_metadata.h"
#include "third_party/blink/public/mojom/indexeddb/indexeddb.mojom.h"

// TODO(crbug.com/40253999): Rename the file to indicate that it contains
// backend-agnostic utils to encode/decode IDB types, and potentially move the
// (Encode/Decode)KeyPath methods below to this file.
#include "content/browser/indexed_db/indexed_db_leveldb_coding.h"

// TODO(crbug.com/40253999): Remove after handling all error cases.
#define TRANSIENT_CHECK(condition) CHECK(condition)

namespace content::indexed_db::sqlite {
namespace {

// The separator used to join the strings when encoding an `IndexedDBKeyPath` of
// type array. Spaces are not allowed in the individual strings, which makes
// this a convenient choice.
constexpr char16_t kKeyPathSeparator[] = u" ";

// Encodes `key_path` into a string. The key path can be either a string or an
// array of strings. If it is an array, the contents are joined with
// `kKeyPathSeparator`.
std::u16string EncodeKeyPath(const blink::IndexedDBKeyPath& key_path) {
  switch (key_path.type()) {
    case blink::mojom::IDBKeyPathType::Null:
      return std::u16string();
    case blink::mojom::IDBKeyPathType::String:
      return key_path.string();
    case blink::mojom::IDBKeyPathType::Array:
      return base::JoinString(key_path.array(), kKeyPathSeparator);
    default:
      NOTREACHED();
  }
}
blink::IndexedDBKeyPath DecodeKeyPath(const std::u16string& encoded) {
  if (encoded.empty()) {
    return blink::IndexedDBKeyPath();
  }
  std::vector<std::u16string> parts = base::SplitString(
      encoded, kKeyPathSeparator, base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (parts.size() > 1) {
    return blink::IndexedDBKeyPath(std::move(parts));
  }
  return blink::IndexedDBKeyPath(std::move(parts.front()));
}

// These are schema versions of our implementation of `sql::Database`; not the
// version supplied by the application for the IndexedDB database.
//
// The version used to initialize the meta table for the first time.
constexpr int kEmptySchemaVersion = 1;
constexpr int kCurrentSchemaVersion = 10;
constexpr int kCompatibleSchemaVersion = kCurrentSchemaVersion;

// Atomically creates the current schema for a new `db`, inserts the initial
// IndexedDB metadata entry with `name`, and sets the current version in
// `meta_table`.
void InitializeNewDatabase(sql::Database* db,
                           const std::u16string& name,
                           sql::MetaTable* meta_table) {
  sql::Transaction transaction(db);
  TRANSIENT_CHECK(transaction.Begin());

  // Create the tables.
  //
  // Note on the schema: The IDB spec defines the "name"
  // (https://www.w3.org/TR/IndexedDB/#name) of the database, object stores and
  // indexes as an arbitrary sequence of 16-bit code units, which implies that
  // the application-supplied name strings need not be valid UTF-16.
  // "key_path"s are always valid UTF-16 since they contain only identifiers
  // (required to be valid UTF-16) and periods.
  // However, to avoid unnecessary conversion from UTF-16 to UTF-8 and back, we
  // store all application-supplied strings as BLOBs.
  //
  // Stores a single row containing the properties of
  // `IndexedDBDatabaseMetadata` for this database.
  TRANSIENT_CHECK(
      db->Execute("CREATE TABLE indexed_db_metadata "
                  "(name BLOB NOT NULL,"
                  " version INTEGER NOT NULL)"));
  TRANSIENT_CHECK(
      db->Execute("CREATE TABLE object_stores "
                  "(id INTEGER PRIMARY KEY,"
                  " name BLOB NOT NULL UNIQUE,"
                  " key_path BLOB,"
                  " auto_increment INTEGER NOT NULL,"
                  " key_generator_current_number INTEGER NOT NULL)"));
  TRANSIENT_CHECK(
      db->Execute("CREATE TABLE records "
                  "(row_id INTEGER PRIMARY KEY AUTOINCREMENT,"
                  " object_store_id INTEGER NOT NULL,"
                  " key BLOB NOT NULL,"
                  " value BLOB NOT NULL,"
                  " UNIQUE (object_store_id, key))"));

  // Insert the initial metadata entry.
  sql::Statement statement(
      db->GetUniqueStatement("INSERT INTO indexed_db_metadata "
                             "(name, version) VALUES (?, ?)"));
  statement.BindBlob(0, name);
  statement.BindInt64(1, blink::IndexedDBDatabaseMetadata::NO_VERSION);
  TRANSIENT_CHECK(statement.Run());

  // Set the current version in the meta table.
  TRANSIENT_CHECK(meta_table->SetVersionNumber(kCurrentSchemaVersion));

  TRANSIENT_CHECK(transaction.Commit());
}

blink::IndexedDBDatabaseMetadata GenerateIndexedDbMetadata(sql::Database* db) {
  blink::IndexedDBDatabaseMetadata metadata;

  // Set the database name and version.
  {
    sql::Statement statement(db->GetReadonlyStatement(
        "SELECT name, version FROM indexed_db_metadata"));
    TRANSIENT_CHECK(statement.Step());
    TRANSIENT_CHECK(statement.ColumnBlobAsString16(0, &metadata.name));
    metadata.version = statement.ColumnInt64(1);
  }

  // Populate object store metadata.
  {
    sql::Statement statement(db->GetReadonlyStatement(
        "SELECT id, name, key_path, auto_increment FROM object_stores"));
    int64_t max_object_store_id = 0;
    while (statement.Step()) {
      blink::IndexedDBObjectStoreMetadata store_metadata;
      store_metadata.id = statement.ColumnInt64(0);
      TRANSIENT_CHECK(statement.ColumnBlobAsString16(1, &store_metadata.name));
      std::u16string encoded_key_path;
      TRANSIENT_CHECK(statement.ColumnBlobAsString16(2, &encoded_key_path));
      store_metadata.key_path = DecodeKeyPath(encoded_key_path);
      store_metadata.auto_increment = statement.ColumnBool(3);
      max_object_store_id = std::max(max_object_store_id, store_metadata.id);
      metadata.object_stores[store_metadata.id] = std::move(store_metadata);
    }
    TRANSIENT_CHECK(statement.Succeeded());
    metadata.max_object_store_id = max_object_store_id;
  }

  return metadata;
}

}  // namespace

// static
StatusOr<std::unique_ptr<DatabaseConnection>> DatabaseConnection::Open(
    const std::u16string& name,
    const base::FilePath& file_path) {
  // TODO(crbug.com/40253999): Create new tag(s) for metrics.
  constexpr sql::Database::Tag kSqlTag = "Test";
  auto db = std::make_unique<sql::Database>(
      sql::DatabaseOptions().set_exclusive_locking(true).set_wal_mode(true),
      kSqlTag);

  // TODO(crbug.com/40253999): Support on-disk databases.
  TRANSIENT_CHECK(db->OpenInMemory());

  auto meta_table = std::make_unique<sql::MetaTable>();
  TRANSIENT_CHECK(meta_table->Init(db.get(), kEmptySchemaVersion,
                                   kCompatibleSchemaVersion));

  switch (meta_table->GetVersionNumber()) {
    case kEmptySchemaVersion:
      InitializeNewDatabase(db.get(), name, meta_table.get());
      break;
    // ...
    // Schema upgrades go here.
    // ...
    case kCurrentSchemaVersion:
      // Already current.
      break;
    default:
      NOTREACHED();
  }

  blink::IndexedDBDatabaseMetadata metadata =
      GenerateIndexedDbMetadata(db.get());
  // Database corruption can cause a mismatch.
  TRANSIENT_CHECK(metadata.name == name);

  return base::WrapUnique(new DatabaseConnection(
      std::move(db), std::move(meta_table), std::move(metadata)));
}

DatabaseConnection::DatabaseConnection(
    std::unique_ptr<sql::Database> db,
    std::unique_ptr<sql::MetaTable> meta_table,
    blink::IndexedDBDatabaseMetadata metadata)
    : db_(std::move(db)),
      meta_table_(std::move(meta_table)),
      metadata_(std::move(metadata)) {}

DatabaseConnection::~DatabaseConnection() = default;

base::WeakPtr<DatabaseConnection> DatabaseConnection::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

std::unique_ptr<BackingStoreTransactionImpl>
DatabaseConnection::CreateTransaction(
    base::PassKey<BackingStoreDatabaseImpl>,
    blink::mojom::IDBTransactionDurability durability,
    blink::mojom::IDBTransactionMode mode) {
  // TODO(crbug.com/40253999): Ensure that `DatabaseConnection` outlives active
  // instances of `BackingStoreTransactionImpl`.
  auto transaction = std::make_unique<sql::Transaction>(db_.get());

  // TODO(crbug.com/40253999): Assert preconditions for `mode`.
  return std::make_unique<BackingStoreTransactionImpl>(
      GetWeakPtr(), std::move(transaction), durability, mode);
}

void DatabaseConnection::OnTransactionBegin(
    base::PassKey<BackingStoreTransactionImpl>,
    const BackingStoreTransactionImpl& transaction) {
  // No other transaction can begin while a version change transaction is
  // active.
  CHECK(!HasActiveVersionChangeTransaction());
  if (transaction.mode() == blink::mojom::IDBTransactionMode::VersionChange) {
    metadata_snapshot_.emplace(metadata_);
  }
}

void DatabaseConnection::OnBeforeTransactionCommit(
    base::PassKey<BackingStoreTransactionImpl>,
    const BackingStoreTransactionImpl& transaction) {
  if (transaction.durability() ==
          blink::mojom::IDBTransactionDurability::Strict &&
      transaction.mode() != blink::mojom::IDBTransactionMode::ReadOnly) {
    // TODO(crbug.com/40253999): Execute `PRAGMA synchronous=FULL`
  }
}

void DatabaseConnection::OnTransactionCommit(
    base::PassKey<BackingStoreTransactionImpl>,
    const BackingStoreTransactionImpl& transaction) {
  // TODO(crbug.com/40253999): Reset the `synchronous` setting.
  if (transaction.mode() == blink::mojom::IDBTransactionMode::VersionChange) {
    CHECK(metadata_snapshot_.has_value());
    metadata_snapshot_.reset();
  }
}

void DatabaseConnection::OnTransactionRollback(
    base::PassKey<BackingStoreTransactionImpl>,
    const BackingStoreTransactionImpl& transaction) {
  // TODO(crbug.com/40253999): Reset the `synchronous` setting.
  if (transaction.mode() == blink::mojom::IDBTransactionMode::VersionChange) {
    CHECK(metadata_snapshot_.has_value());
    metadata_ = std::move(*metadata_snapshot_);
    metadata_snapshot_.reset();
  }
}

Status DatabaseConnection::SetDatabaseVersion(
    base::PassKey<BackingStoreTransactionImpl>,
    int64_t version) {
  CHECK(HasActiveVersionChangeTransaction());
  sql::Statement statement(
      db_->GetUniqueStatement("UPDATE indexed_db_metadata SET version = ?"));
  statement.BindInt64(0, version);
  TRANSIENT_CHECK(statement.Run());
  metadata_.version = version;
  return Status::OK();
}

Status DatabaseConnection::CreateObjectStore(
    base::PassKey<BackingStoreTransactionImpl>,
    int64_t object_store_id,
    std::u16string name,
    blink::IndexedDBKeyPath key_path,
    bool auto_increment) {
  CHECK(HasActiveVersionChangeTransaction());
  if (metadata_.object_stores.contains(object_store_id)) {
    return Status::InvalidArgument("Invalid object_store_id");
  }
  TRANSIENT_CHECK(object_store_id > metadata_.max_object_store_id);

  blink::IndexedDBObjectStoreMetadata metadata(
      std::move(name), object_store_id, std::move(key_path), auto_increment,
      /*max_index_id=*/0);
  sql::Statement statement(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO object_stores "
      "(id, name, key_path, auto_increment, key_generator_current_number) "
      "VALUES (?, ?, ?, ?, ?)"));
  statement.BindInt64(0, metadata.id);
  statement.BindBlob(1, metadata.name);
  statement.BindBlob(2, EncodeKeyPath(metadata.key_path));
  statement.BindBool(3, metadata.auto_increment);
  statement.BindInt64(4, ObjectStoreMetaDataKey::kKeyGeneratorInitialNumber);
  TRANSIENT_CHECK(statement.Run());

  metadata_.object_stores[object_store_id] = std::move(metadata);
  metadata_.max_object_store_id = object_store_id;
  return Status::OK();
}

StatusOr<int64_t> DatabaseConnection::GetKeyGeneratorCurrentNumber(
    base::PassKey<BackingStoreTransactionImpl>,
    int64_t object_store_id) {
  sql::Statement statement(
      db_->GetCachedStatement(SQL_FROM_HERE,
                              "SELECT key_generator_current_number "
                              "FROM object_stores WHERE id = ?"));
  statement.BindInt64(0, object_store_id);
  TRANSIENT_CHECK(statement.Step());
  return statement.ColumnInt64(0);
}

Status DatabaseConnection::MaybeUpdateKeyGeneratorCurrentNumber(
    base::PassKey<BackingStoreTransactionImpl>,
    int64_t object_store_id,
    int64_t new_number) {
  sql::Statement statement(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE object_stores SET key_generator_current_number = ? "
      "WHERE id = ? AND key_generator_current_number < ?"));
  statement.BindInt64(0, new_number);
  statement.BindInt64(1, object_store_id);
  statement.BindInt64(2, new_number);
  TRANSIENT_CHECK(statement.Run());
  return Status::OK();
}

StatusOr<std::optional<BackingStore::RecordIdentifier>>
DatabaseConnection::GetRecordIdentifierIfExists(
    base::PassKey<BackingStoreTransactionImpl>,
    int64_t object_store_id,
    const blink::IndexedDBKey& key) {
  sql::Statement statement(
      db_->GetCachedStatement(SQL_FROM_HERE,
                              "SELECT row_id FROM records "
                              "WHERE object_store_id = ? AND key = ?"));
  std::string encoded_key;
  EncodeSortableIDBKey(key, &encoded_key);
  statement.BindInt64(0, object_store_id);
  statement.BindBlob(1, encoded_key);
  if (statement.Step()) {
    return BackingStore::RecordIdentifier{statement.ColumnInt64(0)};
  }
  TRANSIENT_CHECK(statement.Succeeded());
  return std::nullopt;
}

StatusOr<IndexedDBValue> DatabaseConnection::GetValue(
    base::PassKey<BackingStoreTransactionImpl>,
    int64_t object_store_id,
    const blink::IndexedDBKey& key) {
  sql::Statement statement(
      db_->GetCachedStatement(SQL_FROM_HERE,
                              "SELECT value FROM records "
                              "WHERE object_store_id = ? AND key = ?"));
  std::string encoded_key;
  EncodeSortableIDBKey(key, &encoded_key);
  statement.BindInt64(0, object_store_id);
  statement.BindBlob(1, encoded_key);
  if (statement.Step()) {
    IndexedDBValue value;
    TRANSIENT_CHECK(statement.ColumnBlobAsVector(0, &value.bits));
    return value;
  }
  TRANSIENT_CHECK(statement.Succeeded());
  return IndexedDBValue();
}

StatusOr<BackingStore::RecordIdentifier> DatabaseConnection::PutRecord(
    base::PassKey<BackingStoreTransactionImpl>,
    int64_t object_store_id,
    const blink::IndexedDBKey& key,
    IndexedDBValue value) {
  sql::Statement statement(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT OR REPLACE INTO records "
      "(object_store_id, key, value) VALUES (?, ?, ?)"));
  statement.BindInt64(0, object_store_id);
  std::string encoded_key;
  EncodeSortableIDBKey(key, &encoded_key);
  // TODO(crbug.com/40253999): `move` these into `statement` when
  // crbug.com/419806592 is fixed.
  statement.BindBlob(1, encoded_key);
  statement.BindBlob(2, value.bits);
  TRANSIENT_CHECK(statement.Run());
  return BackingStore::RecordIdentifier{db_->GetLastInsertRowId()};
}

StatusOr<uint32_t> DatabaseConnection::GetObjectStoreKeyCount(
    base::PassKey<BackingStoreTransactionImpl>,
    int64_t object_store_id,
    blink::IndexedDBKeyRange key_range) {
  std::vector<std::string_view> query_pieces{
      "SELECT COUNT() FROM records WHERE object_store_id = ?"};
  std::string lower_encoded;
  std::string upper_encoded;
  if (key_range.lower().IsValid()) {
    EncodeSortableIDBKey(key_range.lower(), &lower_encoded);
    query_pieces.insert(
        query_pieces.end(),
        {" AND key ", key_range.lower_open() ? ">" : ">=", " ?"});
  }
  if (key_range.upper().IsValid()) {
    EncodeSortableIDBKey(key_range.upper(), &upper_encoded);
    query_pieces.insert(
        query_pieces.end(),
        {" AND key ", key_range.upper_open() ? "<" : "<=", " ?"});
  }

  // TODO(crbug.com/40253999): Evaluate performance benefit of using
  // `GetCachedStatement()` instead.
  sql::Statement statement(
      db_->GetReadonlyStatement(base::StrCat(query_pieces)));
  int param_index = 0;
  statement.BindInt64(param_index++, object_store_id);
  if (!lower_encoded.empty()) {
    statement.BindBlob(param_index++, lower_encoded);
  }
  if (!upper_encoded.empty()) {
    statement.BindBlob(param_index++, upper_encoded);
  }
  TRANSIENT_CHECK(statement.Step());
  return statement.ColumnInt(0);
}

}  // namespace content::indexed_db::sqlite
