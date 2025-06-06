// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/app_list/search/local_image_search/inverted_index_table.h"

#include <memory>

#include "base/files/file_path.h"
#include "base/logging.h"
#include "chrome/browser/ash/app_list/search/local_image_search/search_utils.h"
#include "chrome/browser/ash/app_list/search/local_image_search/sql_database.h"
#include "sql/statement.h"

namespace app_list {

namespace {
// Only uses this function to convert source into the right value.
int ConvertIndexingSourceToInt(IndexingSource indexing_source) {
  switch (indexing_source) {
    case IndexingSource::kOcr:
      return 0;
    case IndexingSource::kIca:
      return 1;
  }
}
}  // namespace

// static
bool InvertedIndexTable::Create(SqlDatabase* db) {
  static constexpr char kQuery[] =
      // clang-format off
      "CREATE TABLE inverted_index("
          "term_id INTEGER NOT NULL,"
          "document_id INTEGER NOT NULL,"
          "source INTEGER NOT NULL,"
          "score REAL,"
          "x REAL,"
          "y REAL,"
          "area REAL,"
          "FOREIGN KEY(term_id) REFERENCES annotations(term_id),"
          "FOREIGN KEY(document_id) REFERENCES documents(document_id),"
          "PRIMARY KEY(term_id, document_id, source)) STRICT";
  // clang-format on

  std::unique_ptr<sql::Statement> statement =
      db->GetStatementForQuery(SQL_FROM_HERE, kQuery);
  if (!statement || !statement->Run()) {
    LOG(ERROR) << "Couldn't execute the statement";
    return false;
  }

  static constexpr char kQuery1[] =
      "CREATE INDEX idx_inverted_term ON inverted_index(term_id)";

  std::unique_ptr<sql::Statement> statement1 =
      db->GetStatementForQuery(SQL_FROM_HERE, kQuery1);
  if (!statement1 || !statement1->Run()) {
    LOG(ERROR) << "Couldn't execute the statement";
    return false;
  }

  static constexpr char kQuery2[] =
      "CREATE INDEX idx_inverted_document ON inverted_index(document_id)";

  std::unique_ptr<sql::Statement> statement2 =
      db->GetStatementForQuery(SQL_FROM_HERE, kQuery2);
  if (!statement2 || !statement2->Run()) {
    LOG(ERROR) << "Couldn't execute the statement";
    return false;
  }

  return true;
}

// static
bool InvertedIndexTable::Drop(SqlDatabase* db) {
  static constexpr char kQuery[] = "DROP TABLE IF EXISTS inverted_index";

  std::unique_ptr<sql::Statement> statement =
      db->GetStatementForQuery(SQL_FROM_HERE, kQuery);
  if (!statement || !statement->Run()) {
    LOG(ERROR) << "Couldn't execute the statement";
    return false;
  }

  return true;
}

// static
bool InvertedIndexTable::Insert(SqlDatabase* db,
                                int64_t term_id,
                                int64_t document_id,
                                IndexingSource indexing_source,
                                std::optional<float> score,
                                std::optional<float> x,
                                std::optional<float> y,
                                std::optional<float> area) {
  static constexpr char kQuery[] =
      // clang-format off
      "INSERT INTO inverted_index(term_id, document_id, "
          "source, score, x, y, area) "
          "VALUES(?,?,?,?,?,?,?)";
  // clang-format on

  std::unique_ptr<sql::Statement> statement =
      db->GetStatementForQuery(SQL_FROM_HERE, kQuery);
  if (!statement) {
    LOG(ERROR) << "Couldn't create the statement";
    return false;
  }

  statement->BindInt64(0, term_id);
  statement->BindInt64(1, document_id);
  statement->BindInt(2, ConvertIndexingSourceToInt(indexing_source));
  if (score.has_value()) {
    statement->BindDouble(3, score.value());
  } else {
    statement->BindNull(3);
  }
  if (x.has_value()) {
    statement->BindDouble(4, x.value());
  } else {
    statement->BindNull(4);
  }
  if (y.has_value()) {
    statement->BindDouble(5, y.value());
  } else {
    statement->BindNull(5);
  }
  if (area.has_value()) {
    statement->BindDouble(6, area.value());
  } else {
    statement->BindNull(6);
  }

  if (!statement->Run()) {
    LOG(ERROR) << "Couldn't execute the statement";
    return false;
  }

  return true;
}

// static
bool InvertedIndexTable::Remove(SqlDatabase* db,
                                const base::FilePath& file_path) {
  static constexpr char kQuery[] =
      // clang-format off
      "DELETE FROM inverted_index "
      "WHERE document_id IN ("
          "SELECT d.document_id "
          "FROM documents AS d "
          "WHERE directory_path=? AND file_name=? )";
  // clang-format on

  std::unique_ptr<sql::Statement> statement =
      db->GetStatementForQuery(SQL_FROM_HERE, kQuery);
  if (!statement) {
    LOG(ERROR) << "Couldn't create the statement";
    return false;
  }

  statement->BindString(0, file_path.DirName().AsUTF8Unsafe());
  statement->BindString(1, file_path.BaseName().AsUTF8Unsafe());
  if (!statement->Run()) {
    LOG(ERROR) << "Failed to remove annotations.";
    return false;
  }

  return true;
}

}  // namespace app_list
