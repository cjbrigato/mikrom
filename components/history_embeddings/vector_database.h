// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_HISTORY_EMBEDDINGS_VECTOR_DATABASE_H_
#define COMPONENTS_HISTORY_EMBEDDINGS_VECTOR_DATABASE_H_

#include <optional>
#include <unordered_set>
#include <vector>

#include "base/time/time.h"
#include "components/history/core/browser/history_types.h"
#include "components/history_embeddings/proto/history_embeddings.pb.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/passage_embeddings/passage_embeddings_types.h"

namespace history_embeddings {

// Hash function used for query filtering.
uint32_t HashString(std::string_view str);

struct ScoredUrl {
  ScoredUrl(history::URLID url_id,
            history::VisitID visit_id,
            base::Time visit_time,
            float score,
            float word_match_score);
  ~ScoredUrl();
  ScoredUrl(ScoredUrl&&);
  ScoredUrl& operator=(ScoredUrl&&);
  ScoredUrl(const ScoredUrl&);
  ScoredUrl& operator=(const ScoredUrl&);

  // Basic data about the found URL/visit.
  history::URLID url_id;
  history::VisitID visit_id;
  base::Time visit_time;

  // A measure of how closely the query matched the found data. This includes
  // the single best embedding score plus a word match boost from text search
  // across all passages.
  float score;

  // This is the score computed by word match text search. It's included in
  // the total `score`, but is also kept separate for second-chance word
  // match result filling.
  float word_match_score;
};

struct SearchParams {
  SearchParams();
  SearchParams(const SearchParams&);
  SearchParams(SearchParams&&);
  ~SearchParams();
  SearchParams& operator=(const SearchParams&);

  // Portions of lower-cased query representing terms usable for text search.
  // Owned std::string instances are used instead of std::string_view into
  // an owned query instance because this struct can move, and view data
  // pointers are not guaranteed valid after source string moves.
  std::vector<std::string> query_terms;

  // Embedding similarity score below which no word matching takes place.
  float word_match_minimum_embedding_score = 0.0f;

  // Raw score boost, applied per word.
  float word_match_score_boost_factor = 0.2f;

  // Divides and caps a word match boost. Finding the word more than this many
  // times won't increase the boost for the word.
  size_t word_match_limit = 5;

  // Used as a term in final score boost divide to normalize for long queries.
  size_t word_match_smoothing_factor = 1;

  // Maximum number of terms a query may have. When term count exceeds this
  // limit, no text search for the terms occurs.
  size_t word_match_max_term_count = 3;

  // Makes the total word match boost zero when the ratio of terms matched to
  // total query terms is less than this required value. A term is considered
  // matched if there's at least one instance found in all passages.
  // Stop words are not considered query terms and are not counted.
  float word_match_required_term_ratio = 1.0f;

  // If true, any non-ASCII characters in queries or passages will be erased
  // instead of ignoring such queries or passages entirely.
  bool erase_non_ascii_characters = false;

  // If true, word match text search can still be applied for passages with
  // non-ASCII characters; similar to `erase_non_ascii_characters` but for word
  // match text search only.
  bool word_match_search_non_ascii_passages = false;

  // If true, answering step will be skipped even if the query is answerable.
  bool skip_answering = false;
};

struct SearchInfo {
  SearchInfo();
  SearchInfo(SearchInfo&&);
  ~SearchInfo();

  // Result of the search, the best scored URLs considering total score.
  std::vector<ScoredUrl> scored_urls;

  // Secondary results of the search, the best scored URLs considering
  // word match text search score.
  std::vector<ScoredUrl> word_match_scored_urls;

  // The number of URLs searched to find this result.
  size_t searched_url_count = 0u;

  // The number of embeddings searched to find this result.
  size_t searched_embedding_count = 0u;

  // The number of embeddings scored zero due to having a source passage
  // containing non-ASCII characters.
  size_t skipped_nonascii_passage_count = 0u;

  // The number of source passages modified by erasing non-ASCII characters.
  size_t modified_nonascii_passage_count = 0u;

  // Whether the search completed without interruption. Starting a new search
  // may cause a search to halt, and in that case this member will be false.
  bool completed = false;

  // Time breakdown for metrics: total > scoring > passage_scanning as each
  // succeeding time value is a portion of the last.
  base::TimeDelta total_search_time;
  base::TimeDelta scoring_time;
  base::TimeDelta passage_scanning_time;
};

struct UrlScore {
  float score;
  float word_match_score;
};

struct UrlData {
  UrlData(history::URLID url_id,
          history::VisitID visit_id,
          base::Time visit_time);
  UrlData(const UrlData&);
  UrlData(UrlData&&);
  UrlData& operator=(const UrlData&);
  UrlData& operator=(UrlData&&);
  ~UrlData();

  bool operator==(const UrlData&) const;

  // Finds score of embedding nearest to query, also taking passages
  // into consideration since some should be skipped. The passages
  // correspond to the embeddings 1:1 by index.
  UrlScore BestScoreWith(SearchInfo& search_info,
                         const SearchParams& search_params,
                         const passage_embeddings::Embedding& query_embedding,
                         size_t search_minimum_word_count) const;

  history::URLID url_id;
  history::VisitID visit_id;
  base::Time visit_time;
  proto::PassagesValue passages;
  std::vector<passage_embeddings::Embedding> embeddings;
};

// This base class decouples storage classes and inverts the dependency so that
// a vector database can work with a SQLite database, simple in-memory storage,
// flat files, or whatever kinds of storage will work efficiently.
class VectorDatabase {
 public:
  struct UrlDataIterator {
    virtual ~UrlDataIterator() = default;

    // Returns nullptr if none remain; otherwise advances the iterator
    // and returns a pointer to the next instance (which may be owned
    // by the iterator itself).
    virtual const UrlData* Next() = 0;
  };

  virtual ~VectorDatabase() = default;

  // Returns the expected number of dimensions for an embedding.
  virtual size_t GetEmbeddingDimensions() const = 0;

  // Insert or update all embeddings for a URL's full set of passages.
  // Returns true on success.
  virtual bool AddUrlData(UrlData url_data) = 0;

  // Create an iterator that steps through database items.
  // Null may be returned if there are none.
  virtual std::unique_ptr<UrlDataIterator> MakeUrlDataIterator(
      std::optional<base::Time> time_range_start) = 0;

  // Searches the database for embeddings near given `query` and returns
  // information about where they were found and how nearly the query matched.
  SearchInfo FindNearest(std::optional<base::Time> time_range_start,
                         size_t count,
                         const SearchParams& search_params,
                         const passage_embeddings::Embedding& query_embedding,
                         base::RepeatingCallback<bool()> is_search_halted);
};

// This is an in-memory vector store that supports searching and saving to
// another persistent backing store.
class VectorDatabaseInMemory : public VectorDatabase {
 public:
  VectorDatabaseInMemory();
  ~VectorDatabaseInMemory() override;

  // Save this store's data to another given store. Most implementations don't
  // need this, but it's useful for an in-memory store to work with a separate
  // backing database on a worker sequence.
  void SaveTo(VectorDatabase* database);

  // VectorDatabase:
  size_t GetEmbeddingDimensions() const override;
  bool AddUrlData(UrlData url_data) override;
  std::unique_ptr<UrlDataIterator> MakeUrlDataIterator(
      std::optional<base::Time> time_range_start) override;

 private:
  std::vector<UrlData> data_;
};

// Utility method to split a query into separate query terms for search.
std::vector<std::string> SplitQueryToTerms(
    const std::unordered_set<uint32_t>& stop_words_hashes,
    std::string_view raw_query,
    size_t min_term_length);

// Destructively removes non-ASCII characters from single or many passages.
void EraseNonAsciiCharacters(std::string& passage);
void EraseNonAsciiCharacters(std::vector<std::string>& passages);

}  // namespace history_embeddings

#endif  // COMPONENTS_HISTORY_EMBEDDINGS_VECTOR_DATABASE_H_
