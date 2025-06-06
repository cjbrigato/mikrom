// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/language/core/browser/language_usage_metrics.h"

#include "base/metrics/histogram.h"
#include "base/metrics/histogram_samples.h"
#include "base/metrics/statistics_recorder.h"
#include "components/language/core/browser/url_language_histogram.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::HistogramBase;
using base::HistogramSamples;
using base::SampleCountIterator;
using base::StatisticsRecorder;
using language::UrlLanguageHistogram;

namespace language {

namespace {

class MetricsRecorder {
 public:
  explicit MetricsRecorder(const char* key) : key_(key) {
    HistogramBase* histogram = StatisticsRecorder::FindHistogram(key_);
    if (histogram)
      base_samples_ = histogram->SnapshotSamples();
  }

  MetricsRecorder(const MetricsRecorder&) = delete;
  MetricsRecorder& operator=(const MetricsRecorder&) = delete;

  void CheckTotalCount(int count) {
    Snapshot();
    EXPECT_EQ(count, GetTotalCount());
  }

  void CheckValueCount(HistogramBase::Sample32 value, int count) {
    Snapshot();
    EXPECT_EQ(count, GetCountWithoutSnapshot(value));
  }

 private:
  void Snapshot() {
    HistogramBase* histogram = StatisticsRecorder::FindHistogram(key_);
    if (!histogram)
      return;
    samples_ = histogram->SnapshotSamples();
  }

  HistogramBase::Count32 GetCountWithoutSnapshot(HistogramBase::Sample32 value) {
    if (!samples_)
      return 0;
    HistogramBase::Count32 count = samples_->GetCount(value);
    if (!base_samples_)
      return count;
    return count - base_samples_->GetCount(value);
  }

  HistogramBase::Count32 GetTotalCount() {
    if (!samples_)
      return 0;
    HistogramBase::Count32 count = samples_->TotalCount();
    if (!base_samples_)
      return count;
    return count - base_samples_->TotalCount();
  }

  std::string key_;
  std::unique_ptr<HistogramSamples> base_samples_;
  std::unique_ptr<HistogramSamples> samples_;
};

void RecordPageLanguageVisits(UrlLanguageHistogram& language_histogram,
                              std::string language,
                              int count) {
  for (int i = 0; i < count; i++) {
    language_histogram.OnPageVisited(language);
  }
}

struct LanguageCodeHash {
  LanguageCodeHash() = default;
  LanguageCodeHash(const std::string& code, int hash)
      : code(code), hash(hash) {}
  std::string code;
  int hash;
};

}  // namespace

TEST(LanguageUsageMetricsTest, RecordPageLanguageCounts) {
  const LanguageCodeHash EN("en", 25966);
  const LanguageCodeHash ES("es", 25971);
  const LanguageCodeHash JP("ja", 27233);

  TestingPrefServiceSimple prefs;
  UrlLanguageHistogram::RegisterProfilePrefs(prefs.registry());
  UrlLanguageHistogram url_hist(&prefs);

  // Initialize recorder
  MetricsRecorder recorder("LanguageUsage.MostFrequentPageLanguages");
  recorder.CheckTotalCount(0);

  // Check that nothing is recorded if less than 10 page visits.
  RecordPageLanguageVisits(url_hist, EN.code, 8);
  RecordPageLanguageVisits(url_hist, ES.code, 1);
  LanguageUsageMetrics::RecordPageLanguages(url_hist);
  recorder.CheckTotalCount(0);

  // Check that recording works at 10 page visits.
  RecordPageLanguageVisits(url_hist, EN.code, 1);
  LanguageUsageMetrics::RecordPageLanguages(url_hist);
  recorder.CheckTotalCount(2);
  recorder.CheckValueCount(EN.hash, 1);
  recorder.CheckValueCount(ES.hash, 1);

  // Check that languages with frequency below 0.05 are not recorded.
  RecordPageLanguageVisits(url_hist, EN.code, 28);  // 37/40
  RecordPageLanguageVisits(url_hist, ES.code, 1);   //  2/40 -> exactly 0.05
  RecordPageLanguageVisits(url_hist, JP.code, 1);   //  1/40 -> below 0.05
  LanguageUsageMetrics::RecordPageLanguages(url_hist);
  recorder.CheckTotalCount(4);
  recorder.CheckValueCount(EN.hash, 2);
  recorder.CheckValueCount(ES.hash, 2);
  recorder.CheckValueCount(JP.hash, 0);
}

TEST(LanguageUsageMetricsTest, RecordAcceptLanguages) {
  const LanguageCodeHash EN("en", 25966);
  const LanguageCodeHash ES("es", 25971);
  const LanguageCodeHash JP("ja", 27233);

  // Initialize recorders
  MetricsRecorder recorder("LanguageUsage.AcceptLanguage");
  MetricsRecorder recorder_count("LanguageUsage.AcceptLanguage.Count");
  MetricsRecorder recorder_first_language(
      "LanguageUsage.AcceptLanguage.FirstAcceptLanguage");
  recorder.CheckTotalCount(0);
  recorder_count.CheckTotalCount(0);
  recorder_first_language.CheckTotalCount(0);

  LanguageUsageMetrics::RecordAcceptLanguages("en");
  LanguageUsageMetrics::RecordAcceptLanguages("en");
  recorder.CheckTotalCount(2);
  recorder.CheckValueCount(EN.hash, 2);
  recorder_count.CheckTotalCount(2);
  recorder_count.CheckValueCount(1, 2);
  recorder_first_language.CheckTotalCount(2);
  recorder_first_language.CheckValueCount(EN.hash, 2);

  LanguageUsageMetrics::RecordAcceptLanguages("en,es");
  recorder.CheckTotalCount(4);
  recorder.CheckValueCount(EN.hash, 3);
  recorder.CheckValueCount(ES.hash, 1);
  recorder_count.CheckTotalCount(3);
  recorder_count.CheckValueCount(1, 2);
  recorder_count.CheckValueCount(2, 1);
  recorder_first_language.CheckTotalCount(3);
  recorder_first_language.CheckValueCount(EN.hash, 3);

  LanguageUsageMetrics::RecordAcceptLanguages("en,es,ja-JP");
  recorder.CheckTotalCount(7);
  recorder.CheckTotalCount(7);
  recorder.CheckValueCount(EN.hash, 4);
  recorder.CheckValueCount(ES.hash, 2);
  recorder.CheckValueCount(JP.hash, 1);
  recorder_count.CheckTotalCount(4);
  recorder_count.CheckValueCount(1, 2);
  recorder_count.CheckValueCount(2, 1);
  recorder_count.CheckValueCount(3, 1);
  recorder_first_language.CheckTotalCount(4);
  recorder_first_language.CheckValueCount(EN.hash, 4);
}

TEST(LanguageUsageMetricsTest, ParseAcceptLanguages) {
  std::vector<int> languages;
  std::vector<int>::const_iterator it;

  const int ENGLISH = 25966;
  const int SPANISH = 25971;
  const int JAPANESE = 27233;

  // Basic single language case.
  languages = LanguageUsageMetrics::ParseAcceptLanguages("ja");
  EXPECT_EQ(1U, languages.size());
  EXPECT_EQ(JAPANESE, *languages.begin());

  // Empty language.
  languages = LanguageUsageMetrics::ParseAcceptLanguages(std::string());
  EXPECT_EQ(0U, languages.size());

  // Country code is ignored.
  languages = LanguageUsageMetrics::ParseAcceptLanguages("ja-JP");
  EXPECT_EQ(1U, languages.size());
  EXPECT_EQ(JAPANESE, *languages.begin());

  // Case is ignored.
  languages = LanguageUsageMetrics::ParseAcceptLanguages("Ja-jP");
  EXPECT_EQ(1U, languages.size());
  EXPECT_EQ(JAPANESE, *languages.begin());

  // Underscore as the separator.
  languages = LanguageUsageMetrics::ParseAcceptLanguages("ja_JP");
  EXPECT_EQ(1U, languages.size());
  EXPECT_EQ(JAPANESE, *languages.begin());

  // The result contains a same language code only once.
  languages = LanguageUsageMetrics::ParseAcceptLanguages("ja-JP,ja");
  EXPECT_EQ(1U, languages.size());
  EXPECT_EQ(JAPANESE, *languages.begin());

  // Basic two languages case.
  languages = LanguageUsageMetrics::ParseAcceptLanguages("en,ja");
  EXPECT_EQ(2U, languages.size());
  it = languages.begin();
  EXPECT_EQ(ENGLISH, *it);
  EXPECT_EQ(JAPANESE, *++it);

  // Multiple languages.
  languages =
      LanguageUsageMetrics::ParseAcceptLanguages("ja-JP,en,es,ja,en-US");
  EXPECT_EQ(3U, languages.size());
  it = languages.begin();
  EXPECT_EQ(JAPANESE, *it);
  EXPECT_EQ(ENGLISH, *++it);
  EXPECT_EQ(SPANISH, *++it);

  // Two empty languages.
  languages = LanguageUsageMetrics::ParseAcceptLanguages(",");
  EXPECT_EQ(0U, languages.size());

  // Trailing comma.
  languages = LanguageUsageMetrics::ParseAcceptLanguages("ja,");
  EXPECT_EQ(1U, languages.size());
  EXPECT_EQ(JAPANESE, *languages.begin());

  // Leading comma.
  languages = LanguageUsageMetrics::ParseAcceptLanguages(",es");
  EXPECT_EQ(1U, languages.size());
  EXPECT_EQ(SPANISH, *languages.begin());

  // Combination of invalid and valid.
  languages = LanguageUsageMetrics::ParseAcceptLanguages("1234,en");
  EXPECT_EQ(1U, languages.size());
  it = languages.begin();
  EXPECT_EQ(ENGLISH, *it);
}

TEST(LanguageUsageMetricsTest, ToLanguageCodeHash) {
  const int SPANISH = 25971;
  const int JAPANESE = 27233;

  // Basic case.
  EXPECT_EQ(JAPANESE, LanguageUsageMetrics::ToLanguageCodeHash("ja"));

  // Case is ignored.
  EXPECT_EQ(SPANISH, LanguageUsageMetrics::ToLanguageCodeHash("Es"));

  // Country code is ignored.
  EXPECT_EQ(JAPANESE, LanguageUsageMetrics::ToLanguageCodeHash("ja-JP"));

  // Invalid locales are considered as unknown language.
  EXPECT_EQ(0, LanguageUsageMetrics::ToLanguageCodeHash(std::string()));
  EXPECT_EQ(0, LanguageUsageMetrics::ToLanguageCodeHash("1234"));

  // "xx" is not acceptable because it doesn't exist in ISO 639-1 table.
  // However, LanguageUsageMetrics doesn't tell what code is valid.
  EXPECT_EQ(30840, LanguageUsageMetrics::ToLanguageCodeHash("xx"));
}

}  // namespace language
