// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/omnibox/browser/shortcuts_backend.h"

#include <stddef.h>

#include <algorithm>
#include <iterator>
#include <memory>

#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "components/history/core/browser/history_backend.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/test/history_service_test_util.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_provider.h"
#include "components/omnibox/browser/fake_autocomplete_provider.h"
#include "components/omnibox/browser/shortcuts_constants.h"
#include "components/omnibox/browser/shortcuts_database.h"
#include "components/omnibox/common/omnibox_features.h"
#include "components/search_engines/search_engines_test_environment.h"
#include "components/search_engines/search_terms_data.h"
#include "components/search_engines/template_url_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// ShortcutsBackendTest -------------------------------------------------------

class FakeShortcutsBackend : public ShortcutsBackend {
 public:
  FakeShortcutsBackend(TemplateURLService* template_url_service,
                       std::unique_ptr<SearchTermsData> search_terms_data,
                       history::HistoryService* history_service,
                       base::FilePath database_path,
                       bool suppress_db)
      : ShortcutsBackend(template_url_service,
                         std::move(search_terms_data),
                         history_service,
                         database_path,
                         suppress_db) {}

  using ShortcutsBackend::AddShortcut;
  using ShortcutsBackend::DeleteOldShortcuts;
  using ShortcutsBackend::DeleteShortcutsWithDeletedOrInactiveKeywords;
  using ShortcutsBackend::DeleteShortcutsWithIDs;
  using ShortcutsBackend::DeleteShortcutsWithURL;
  using ShortcutsBackend::MatchToMatchCore;
  using ShortcutsBackend::shortcuts_map_;
  using ShortcutsBackend::ShutdownOnUIThread;
  using ShortcutsBackend::UpdateShortcut;

 protected:
  ~FakeShortcutsBackend() override = default;
};

class ShortcutsBackendTest : public testing::Test,
                             public ShortcutsBackend::ShortcutsBackendObserver {
 public:
  ShortcutsBackendTest();
  ShortcutsBackendTest(const ShortcutsBackendTest&) = delete;
  ShortcutsBackendTest& operator=(const ShortcutsBackendTest&) = delete;

  ShortcutsDatabase::Shortcut::MatchCore MatchCoreForTesting(
      const std::string& url,
      const std::string& contents_class = std::string(),
      const std::string& description_class = std::string(),
      AutocompleteMatch::Type type = AutocompleteMatchType::URL_WHAT_YOU_TYPED);
  void SetSearchProvider();

  void SetUp() override;
  void TearDown() override;

  void OnShortcutsLoaded() override;
  void OnShortcutsChanged() override;

  const ShortcutsBackend::ShortcutMap& shortcuts_map() const {
    return backend_->shortcuts_map();
  }
  bool changed_notified() const { return changed_notified_; }
  void set_changed_notified(bool changed_notified) {
    changed_notified_ = changed_notified;
  }

  TemplateURLService* template_url_service() {
    return search_engines_test_environment_.template_url_service();
  }

  void InitBackend();
  bool ShortcutExists(const std::u16string& terms) const;
  std::vector<std::u16string> ShortcutsMapTexts() const;
  void ClearShortcutsMap();
  ShortcutsDatabase::Shortcut::MatchCore MatchToMatchCore(
      const AutocompleteMatch& match) {
    SearchTermsData search_terms_data;
    return FakeShortcutsBackend::MatchToMatchCore(match, template_url_service(),
                                                  &search_terms_data);
  }

  FakeShortcutsBackend* backend() { return backend_.get(); }

  base::test::ScopedFeatureList& scoped_feature_list() {
    return scoped_feature_list_;
  }

 private:
  base::ScopedTempDir profile_dir_;
  // `scoped_feature_list_` needs to be destroyed after TaskEnvironment is
  // destroyed, so that other threads won't access the feature list while it is
  // being destroyed.
  base::test::ScopedFeatureList scoped_feature_list_;
  base::test::TaskEnvironment task_environment_;
  search_engines::SearchEnginesTestEnvironment search_engines_test_environment_;
  std::unique_ptr<history::HistoryService> history_service_;

  scoped_refptr<FakeShortcutsBackend> backend_;

  bool load_notified_ = false;
  bool changed_notified_ = false;
};

ShortcutsBackendTest::ShortcutsBackendTest() = default;

ShortcutsDatabase::Shortcut::MatchCore
ShortcutsBackendTest::MatchCoreForTesting(const std::string& url,
                                          const std::string& contents_class,
                                          const std::string& description_class,
                                          AutocompleteMatch::Type type) {
  AutocompleteMatch match(nullptr, 0, false, type);
  match.destination_url = GURL(url);
  match.contents = u"test";
  match.contents_class =
      AutocompleteMatch::ClassificationsFromString(contents_class);
  match.description_class =
      AutocompleteMatch::ClassificationsFromString(description_class);
  match.search_terms_args =
      std::make_unique<TemplateURLRef::SearchTermsArgs>(match.contents);
  return MatchToMatchCore(match);
}

void ShortcutsBackendTest::SetSearchProvider() {
  TemplateURLData data;
  data.SetURL("http://foo.com/search?bar={searchTerms}");
  data.SetShortName(u"foo");
  data.SetKeyword(u"foo");

  TemplateURL* template_url =
      template_url_service()->Add(std::make_unique<TemplateURL>(data));
  template_url_service()->SetUserSelectedDefaultSearchProvider(template_url);
}

void ShortcutsBackendTest::SetUp() {
  ASSERT_TRUE(profile_dir_.CreateUniqueTempDir());
  history_service_ =
      history::CreateHistoryService(profile_dir_.GetPath(), true);
  ASSERT_TRUE(history_service_);

  base::FilePath shortcuts_database_path =
      profile_dir_.GetPath().Append(kShortcutsDatabaseName);
  backend_ = new FakeShortcutsBackend(
      template_url_service(), std::make_unique<SearchTermsData>(),
      history_service_.get(), shortcuts_database_path, false);
  ASSERT_TRUE(backend_.get());
  backend_->AddObserver(this);
}

void ShortcutsBackendTest::TearDown() {
  backend_->RemoveObserver(this);
  backend_->ShutdownOnUIThread();
  backend_.reset();

  // Explicitly shut down the history service and wait for its backend to be
  // destroyed to prevent resource leaks.
  base::RunLoop run_loop;
  history_service_->SetOnBackendDestroyTask(run_loop.QuitClosure());
  history_service_->Shutdown();
  run_loop.Run();

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(profile_dir_.Delete());
}

void ShortcutsBackendTest::OnShortcutsLoaded() {
  load_notified_ = true;
}

void ShortcutsBackendTest::OnShortcutsChanged() {
  changed_notified_ = true;
}

void ShortcutsBackendTest::InitBackend() {
  ASSERT_TRUE(backend_);
  ASSERT_FALSE(load_notified_);
  ASSERT_FALSE(backend_->initialized());
  backend_->Init();
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(load_notified_);
  EXPECT_TRUE(backend_->initialized());
}

bool ShortcutsBackendTest::ShortcutExists(const std::u16string& terms) const {
  return shortcuts_map().find(terms) != shortcuts_map().end();
}

std::vector<std::u16string> ShortcutsBackendTest::ShortcutsMapTexts() const {
  std::vector<std::u16string> texts;
  std::ranges::transform(shortcuts_map(), std::back_inserter(texts),
                         [](const auto& entry) { return entry.second.text; });
  return texts;
}

void ShortcutsBackendTest::ClearShortcutsMap() {
  backend_->shortcuts_map_.clear();
}

// Actual tests ---------------------------------------------------------------

// Verifies that creating MatchCores strips classifications and sanitizes match
// types.
TEST_F(ShortcutsBackendTest, SanitizeMatchCore) {
  struct Cases {
    std::u16string search_terms;
    std::string input_contents_class;
    std::string input_description_class;
    AutocompleteMatch::Type input_type;
    std::string output_contents_class;
    std::string output_description_class;
    AutocompleteMatch::Type output_type;
  };
  auto cases = std::to_array<Cases>({
      {u"test", "0,1,4,0", "0,3,4,1", AutocompleteMatchType::URL_WHAT_YOU_TYPED,
       "0,1,4,0", "0,1", AutocompleteMatchType::HISTORY_URL},
      {u"test", "0,3,5,1", "0,2,5,0", AutocompleteMatchType::NAVSUGGEST, "0,1",
       "0,0", AutocompleteMatchType::HISTORY_URL},
      {u"test", "0,1", "0,0,11,2,15,0",
       AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED, "", "",
       AutocompleteMatchType::SEARCH_HISTORY},
      {u"test", "0,1", "0,0", AutocompleteMatchType::SEARCH_SUGGEST, "", "",
       AutocompleteMatchType::SEARCH_HISTORY},
      {u"test", "0,1", "0,0", AutocompleteMatchType::SEARCH_SUGGEST_ENTITY, "",
       "", AutocompleteMatchType::SEARCH_HISTORY},
      {u"test", "0,1", "0,0", AutocompleteMatchType::SEARCH_SUGGEST_TAIL, "",
       "", AutocompleteMatchType::SEARCH_HISTORY},
      {u"test", "0,1", "0,0",
       AutocompleteMatchType::SEARCH_SUGGEST_PERSONALIZED, "", "",
       AutocompleteMatchType::SEARCH_HISTORY},
      {u"test", "0,1", "0,0", AutocompleteMatchType::SEARCH_SUGGEST_PROFILE, "",
       "", AutocompleteMatchType::SEARCH_HISTORY},
      {u"", "0,1", "0,0", AutocompleteMatchType::CLIPBOARD_TEXT, "0,1", "0,0",
       AutocompleteMatchType::SEARCH_HISTORY},
  });

  for (size_t i = 0; i < std::size(cases); ++i) {
    AutocompleteMatch match;
    match.contents_class = AutocompleteMatch::ClassificationsFromString(
        cases[i].input_contents_class);
    match.description_class = AutocompleteMatch::ClassificationsFromString(
        cases[i].input_description_class);
    match.type = cases[i].input_type;
    match.search_terms_args =
        cases[i].search_terms.empty()
            ? nullptr
            : std::make_unique<TemplateURLRef::SearchTermsArgs>(
                  cases[i].search_terms);

    ShortcutsDatabase::Shortcut::MatchCore match_core = MatchToMatchCore(match);
    EXPECT_EQ(match_core.contents_class, cases[i].output_contents_class)
        << ":i:" << i << ":type:" << cases[i].input_type;
    EXPECT_EQ(match_core.description_class, cases[i].output_description_class)
        << ":i:" << i << ":type:" << cases[i].input_type;
    EXPECT_EQ(match_core.type, cases[i].output_type)
        << ":i:" << i << ":type:" << cases[i].input_type;
  }
}

// Verifies that creating MatchCores strips keywords and sanitizes match types.
TEST_F(ShortcutsBackendTest, SanitizeMatchCore_Keyword) {
  struct Cases {
    std::u16string search_terms;
    std::u16string input_fill_into_edit;
    AutocompleteMatch::Type input_type;
    std::u16string input_keyword;
    ui::PageTransition input_page_transition;
    std::u16string output_fill_into_edit;
    AutocompleteMatch::Type output_type;
    std::u16string output_keyword;
    ui::PageTransition output_page_transition;
  };
  auto cases = std::to_array<Cases>({
      {u"franklin d roosevelt",
       u"foo http://foo.com/search?bar=franklin+d+roosevelt",
       AutocompleteMatchType::NAVSUGGEST, u"foo", ui::PAGE_TRANSITION_KEYWORD,
       u"http://foo.com/search?bar=franklin+d+roosevelt",
       AutocompleteMatchType::HISTORY_URL, u"", ui::PAGE_TRANSITION_GENERATED},
      {u"franklin d roosevelt",
       u"http://foo.com/search?bar=franklin+d+roosevelt",
       AutocompleteMatchType::NAVSUGGEST, u"foo", ui::PAGE_TRANSITION_GENERATED,
       u"http://foo.com/search?bar=franklin+d+roosevelt",
       AutocompleteMatchType::HISTORY_URL, u"", ui::PAGE_TRANSITION_GENERATED},
      {u"franklin d roosevelt", u"foo franklin d roosevelt",
       AutocompleteMatchType::SEARCH_SUGGEST, u"foo",
       ui::PAGE_TRANSITION_KEYWORD, u"franklin d roosevelt",
       AutocompleteMatchType::SEARCH_HISTORY, u"foo",
       ui::PAGE_TRANSITION_GENERATED},
      {u"franklin d roosevelt", u"franklin d roosevelt",
       AutocompleteMatchType::SEARCH_SUGGEST, u"foo",
       ui::PAGE_TRANSITION_GENERATED, u"franklin d roosevelt",
       AutocompleteMatchType::SEARCH_HISTORY, u"foo",
       ui::PAGE_TRANSITION_GENERATED},
      {u"", u"franklin d roosevelt", AutocompleteMatchType::SEARCH_SUGGEST,
       u"foo", ui::PAGE_TRANSITION_GENERATED, u"franklin d roosevelt",
       AutocompleteMatchType::SEARCH_HISTORY, u"foo",
       ui::PAGE_TRANSITION_GENERATED},
  });

  SetSearchProvider();
  for (size_t i = 0; i < std::size(cases); ++i) {
    AutocompleteMatch match;
    match.keyword = cases[i].input_keyword;
    match.fill_into_edit = cases[i].input_fill_into_edit;
    match.type = cases[i].input_type;
    match.transition = cases[i].input_page_transition;
    match.search_terms_args =
        cases[i].search_terms.empty()
            ? nullptr
            : std::make_unique<TemplateURLRef::SearchTermsArgs>(
                  cases[i].search_terms);

    ShortcutsDatabase::Shortcut::MatchCore match_core = MatchToMatchCore(match);
    EXPECT_EQ(match_core.fill_into_edit, cases[i].output_fill_into_edit)
        << ":i:" << i;
    EXPECT_EQ(match_core.type, cases[i].output_type) << ":i:" << i;
    EXPECT_EQ(match_core.keyword, cases[i].output_keyword) << ":i:" << i;
    EXPECT_TRUE(ui::PageTransitionCoreTypeIs(cases[i].output_page_transition,
                                             match_core.transition))
        << ":i:" << i;
  }
}

TEST_F(ShortcutsBackendTest, SearchSuggestionTest) {
  SetSearchProvider();
  {
    AutocompleteMatch match;
    match.fill_into_edit = u"franklin d roosevelt";
    match.type = AutocompleteMatchType::SEARCH_SUGGEST;
    match.contents = u"franklin d roosevelt";
    match.contents_class =
        AutocompleteMatch::ClassificationsFromString("0,0,5,2");
    match.description = u"";
    match.destination_url =
        GURL("http://www.foo.com/search?bar=franklin+d+roosevelt&gs_ssp=1234");
    match.keyword = u"foo";
    match.from_keyword = false;
    match.transition = ui::PAGE_TRANSITION_GENERATED;
    match.search_terms_args = std::make_unique<TemplateURLRef::SearchTermsArgs>(
        u"franklin d roosevelt");

    ShortcutsDatabase::Shortcut::MatchCore match_core = MatchToMatchCore(match);
    EXPECT_EQ(match_core.destination_url.spec(),
              "http://foo.com/search?bar=franklin+d+roosevelt");
    EXPECT_EQ(match_core.contents, u"franklin d roosevelt");
    EXPECT_EQ(match_core.contents_class, "0,0");
    EXPECT_EQ(match_core.description, std::u16string());
    EXPECT_TRUE(ui::PageTransitionCoreTypeIs(ui::PAGE_TRANSITION_GENERATED,
                                             match_core.transition));
  }
  {
    AutocompleteMatch match;
    match.fill_into_edit = u"foo franklin d roosevelt";
    match.type = AutocompleteMatchType::SEARCH_SUGGEST;
    match.contents = u"franklin d roosevelt";
    match.contents_class =
        AutocompleteMatch::ClassificationsFromString("0,0,5,2");
    match.description = u"";
    match.destination_url =
        GURL("http://www.foo.com/search?bar=franklin+d+roosevelt&gs_ssp=1234");
    match.keyword = u"foo";
    match.from_keyword = true;
    match.transition = ui::PAGE_TRANSITION_KEYWORD;
    match.search_terms_args = std::make_unique<TemplateURLRef::SearchTermsArgs>(
        u"franklin d roosevelt");

    ShortcutsDatabase::Shortcut::MatchCore match_core = MatchToMatchCore(match);
    EXPECT_EQ(match_core.destination_url.spec(),
              "http://foo.com/search?bar=franklin+d+roosevelt");
    EXPECT_EQ(match_core.contents, u"franklin d roosevelt");
    EXPECT_EQ(match_core.contents_class, "0,0");
    EXPECT_EQ(match_core.description, std::u16string());
    EXPECT_TRUE(ui::PageTransitionCoreTypeIs(ui::PAGE_TRANSITION_GENERATED,
                                             match_core.transition));
  }
  {
    AutocompleteMatch match;
    match.fill_into_edit = u"franklin d roosevelt";
    match.type = AutocompleteMatchType::SEARCH_SUGGEST_ENTITY;
    match.contents = u"roosevelt";
    match.contents_class =
        AutocompleteMatch::ClassificationsFromString("0,0,5,2");
    match.description = u"Franklin D. Roosevelt";
    match.description_class =
        AutocompleteMatch::ClassificationsFromString("0,4");
    match.destination_url =
        GURL("http://www.foo.com/search?bar=franklin+d+roosevelt&gs_ssp=1234");
    match.keyword = u"foo";
    match.from_keyword = false;
    match.transition = ui::PAGE_TRANSITION_GENERATED;
    match.search_terms_args = std::make_unique<TemplateURLRef::SearchTermsArgs>(
        u"franklin d roosevelt");

    ShortcutsDatabase::Shortcut::MatchCore match_core = MatchToMatchCore(match);
    EXPECT_EQ(match_core.destination_url.spec(),
              "http://foo.com/search?bar=franklin+d+roosevelt");
    EXPECT_EQ(match_core.contents, u"franklin d roosevelt");
    EXPECT_EQ(match_core.contents_class, "0,0");
    EXPECT_EQ(match_core.description, std::u16string());
    EXPECT_TRUE(ui::PageTransitionCoreTypeIs(ui::PAGE_TRANSITION_GENERATED,
                                             match_core.transition));
  }
  {
    AutocompleteMatch match;
    match.fill_into_edit = u"foo franklin d roosevelt";
    match.type = AutocompleteMatchType::SEARCH_SUGGEST_ENTITY;
    match.contents = u"roosevelt";
    match.contents_class =
        AutocompleteMatch::ClassificationsFromString("0,0,5,2");
    match.description = u"Franklin D. Roosevelt";
    match.description_class =
        AutocompleteMatch::ClassificationsFromString("0,4");
    match.destination_url =
        GURL("http://www.foo.com/search?bar=franklin+d+roosevelt&gs_ssp=1234");
    match.keyword = u"foo";
    match.from_keyword = true;
    match.transition = ui::PAGE_TRANSITION_KEYWORD;
    match.search_terms_args = std::make_unique<TemplateURLRef::SearchTermsArgs>(
        u"franklin d roosevelt");

    ShortcutsDatabase::Shortcut::MatchCore match_core = MatchToMatchCore(match);
    EXPECT_EQ(match_core.destination_url.spec(),
              "http://foo.com/search?bar=franklin+d+roosevelt");
    EXPECT_EQ(match_core.contents, u"franklin d roosevelt");
    EXPECT_EQ(match_core.contents_class, "0,0");
    EXPECT_EQ(match_core.description, std::u16string());
    EXPECT_TRUE(ui::PageTransitionCoreTypeIs(ui::PAGE_TRANSITION_GENERATED,
                                             match_core.transition));
  }
}

TEST_F(ShortcutsBackendTest, MatchCoreDescriptionTest) {
  // When match.description_for_shortcuts is empty, match_core should use
  // match.description.
  {
    AutocompleteMatch match;
    match.type = AutocompleteMatchType::NAVSUGGEST;
    match.description = u"the cat";
    match.description_class =
        AutocompleteMatch::ClassificationsFromString("0,1");

    ShortcutsDatabase::Shortcut::MatchCore match_core = MatchToMatchCore(match);
    EXPECT_EQ(match_core.description, match.description);
    EXPECT_EQ(
        match_core.description_class,
        AutocompleteMatch::ClassificationsToString(match.description_class));
  }

  // When match.description_for_shortcuts is set, match_core should use it
  // instead of match.description.
  {
    AutocompleteMatch match;
    match.type = AutocompleteMatchType::NAVSUGGEST;
    match.description = u"the cat";
    match.description_class =
        AutocompleteMatch::ClassificationsFromString("0,1");
    match.description_for_shortcuts = u"the elephant";
    match.description_class_for_shortcuts =
        AutocompleteMatch::ClassificationsFromString("0,4");

    ShortcutsDatabase::Shortcut::MatchCore match_core = MatchToMatchCore(match);
    EXPECT_EQ(match_core.description, match.description_for_shortcuts);
    EXPECT_EQ(match_core.description_class,
              AutocompleteMatch::ClassificationsToString(
                  match.description_class_for_shortcuts));
  }
}

TEST_F(ShortcutsBackendTest, AddAndUpdateShortcut) {
  InitBackend();
  EXPECT_FALSE(changed_notified());

  ShortcutsDatabase::Shortcut shortcut(
      "BD85DBA2-8C29-49F9-84AE-48E1E90880DF", u"goog",
      MatchCoreForTesting("http://www.google.com"), base::Time::Now(), 100);
  EXPECT_TRUE(backend()->AddShortcut(shortcut));
  EXPECT_TRUE(changed_notified());
  auto shortcut_iter(shortcuts_map().find(shortcut.text));
  ASSERT_TRUE(shortcut_iter != shortcuts_map().end());
  EXPECT_EQ(shortcut_iter->second.id, shortcut.id);
  EXPECT_EQ(shortcut_iter->second.match_core.contents,
            shortcut.match_core.contents);

  set_changed_notified(false);
  shortcut.match_core.contents = u"Google Web Search";
  EXPECT_TRUE(backend()->UpdateShortcut(shortcut));
  EXPECT_TRUE(changed_notified());
  shortcut_iter = shortcuts_map().find(shortcut.text);
  ASSERT_TRUE(shortcut_iter != shortcuts_map().end());
  EXPECT_EQ(shortcut_iter->second.id, shortcut.id);
  EXPECT_EQ(shortcut_iter->second.match_core.contents,
            shortcut.match_core.contents);
}

// Tests that zero suggest matches are not added to the database.
TEST_F(ShortcutsBackendTest, AddAndUpdateShortcut_ZeroSuggest) {
  InitBackend();
  EXPECT_FALSE(changed_notified());

  scoped_refptr<FakeAutocompleteProvider> zero_suggest_provider =
      new FakeAutocompleteProvider(
          AutocompleteProvider::Type::TYPE_ZERO_SUGGEST);
  AutocompleteMatch zero_suggest_match(
      zero_suggest_provider.get(), 400, true,
      AutocompleteMatchType::TILE_MOST_VISITED_SITE);

  backend()->AddOrUpdateShortcut(u"some text", zero_suggest_match);
  EXPECT_FALSE(changed_notified());
}

TEST_F(ShortcutsBackendTest, DeleteShortcuts) {
  InitBackend();
  ShortcutsDatabase::Shortcut shortcut1(
      "BD85DBA2-8C29-49F9-84AE-48E1E90880DF", u"goog",
      MatchCoreForTesting("http://www.google.com"), base::Time::Now(), 100);
  EXPECT_TRUE(backend()->AddShortcut(shortcut1));

  ShortcutsDatabase::Shortcut shortcut2(
      "BD85DBA2-8C29-49F9-84AE-48E1E90880E0", u"gle",
      MatchCoreForTesting("http://www.google.com"), base::Time::Now(), 100);
  EXPECT_TRUE(backend()->AddShortcut(shortcut2));

  ShortcutsDatabase::Shortcut shortcut3(
      "BD85DBA2-8C29-49F9-84AE-48E1E90880E1", u"sp",
      MatchCoreForTesting("http://www.sport.com"), base::Time::Now(), 10);
  EXPECT_TRUE(backend()->AddShortcut(shortcut3));

  ShortcutsDatabase::Shortcut shortcut4(
      "BD85DBA2-8C29-49F9-84AE-48E1E90880E2", u"mov",
      MatchCoreForTesting("http://www.film.com"), base::Time::Now(), 10);
  EXPECT_TRUE(backend()->AddShortcut(shortcut4));

  ASSERT_EQ(shortcuts_map().size(), 4U);
  EXPECT_EQ(shortcuts_map().find(shortcut1.text)->second.id, shortcut1.id);
  EXPECT_EQ(shortcuts_map().find(shortcut2.text)->second.id, shortcut2.id);
  EXPECT_EQ(shortcuts_map().find(shortcut3.text)->second.id, shortcut3.id);
  EXPECT_EQ(shortcuts_map().find(shortcut4.text)->second.id, shortcut4.id);

  EXPECT_TRUE(
      backend()->DeleteShortcutsWithURL(shortcut1.match_core.destination_url));

  ASSERT_EQ(shortcuts_map().size(), 2U);
  EXPECT_FALSE(ShortcutExists(shortcut1.text));
  EXPECT_FALSE(ShortcutExists(shortcut2.text));
  EXPECT_TRUE(ShortcutExists(shortcut3.text));
  EXPECT_TRUE(ShortcutExists(shortcut4.text));

  ShortcutsDatabase::ShortcutIDs deleted_ids;
  deleted_ids.push_back(shortcut3.id);
  deleted_ids.push_back(shortcut4.id);
  EXPECT_TRUE(backend()->DeleteShortcutsWithIDs(deleted_ids));

  ASSERT_EQ(shortcuts_map().size(), 0U);
}

TEST_F(ShortcutsBackendTest, DeleteOldShortcuts) {
  InitBackend();

  // Add an inactive keyword.
  TemplateURLData data_inactive;
  data_inactive.SetShortName(u"inactive");
  data_inactive.SetKeyword(u"inactive");
  data_inactive.SetURL("http://www.inactive.com/{searchTerms}");
  template_url_service()->Add(std::make_unique<TemplateURL>(data_inactive));

  // Define shortcuts that are 1, 10, 100 and 1000 days old.
  ShortcutsDatabase::Shortcut shortcut1(
      "BD85DBA2-8C29-49F9-84AE-48E1E90880DF", u"google",
      MatchCoreForTesting("http://www.google.com"),
      base::Time::Now() - base::Days(1), 100);
  EXPECT_TRUE(backend()->AddShortcut(shortcut1));

  ShortcutsDatabase::Shortcut shortcut2(
      "BD85DBA2-8C29-49F9-84AE-48E1E90880E0", u"yahoo",
      MatchCoreForTesting("http://www.yahoo.com"),
      base::Time::Now() - base::Days(10), 10);
  EXPECT_TRUE(backend()->AddShortcut(shortcut2));

  ShortcutsDatabase::Shortcut shortcut3(
      "BD85DBA2-8C29-49F9-84AE-48E1E90880E1", u"baidu",
      MatchCoreForTesting("http://www.baidu.com"),
      base::Time::Now() - base::Days(100), 1000);
  EXPECT_TRUE(backend()->AddShortcut(shortcut3));

  ShortcutsDatabase::Shortcut shortcut4(
      "BD85DBA2-8C29-49F9-84AE-48E1E90880E2", u"bing",
      MatchCoreForTesting("http://www.bing.com"),
      base::Time::Now() - base::Days(1000), 1);
  EXPECT_TRUE(backend()->AddShortcut(shortcut4));

  ShortcutsDatabase::Shortcut shortcut5(
      "BD85DBA2-8C29-49F9-84AE-48E1E90880E3", u"deleted",
      MatchCoreForTesting("http://www.deleted.com"),
      base::Time::Now() - base::Days(10), 1);
  shortcut5.match_core.keyword = u"deleted";
  EXPECT_TRUE(backend()->AddShortcut(shortcut5));

  ShortcutsDatabase::Shortcut shortcut6(
      "BD85DBA2-8C29-49F9-84AE-48E1E90880E4", u"inactive",
      MatchCoreForTesting("http://www.inactive.com"),
      base::Time::Now() - base::Days(10), 1);
  shortcut6.match_core.keyword = u"inactive";
  EXPECT_TRUE(backend()->AddShortcut(shortcut6));

  ASSERT_EQ(shortcuts_map().size(), 6U);
  EXPECT_EQ(shortcuts_map().find(shortcut1.text)->second.id, shortcut1.id);
  EXPECT_EQ(shortcuts_map().find(shortcut2.text)->second.id, shortcut2.id);
  EXPECT_EQ(shortcuts_map().find(shortcut3.text)->second.id, shortcut3.id);
  EXPECT_EQ(shortcuts_map().find(shortcut4.text)->second.id, shortcut4.id);
  EXPECT_EQ(shortcuts_map().find(shortcut5.text)->second.id, shortcut5.id);
  EXPECT_EQ(shortcuts_map().find(shortcut6.text)->second.id, shortcut6.id);

  EXPECT_TRUE(backend()->DeleteOldShortcuts());

  // After deleting old shortcuts, the two that are more than 90 days old should
  // no longer be present. The one with a deleted keyword should no longer be
  // present.
  ASSERT_EQ(shortcuts_map().size(), 2U);
  EXPECT_TRUE(ShortcutExists(shortcut1.text));
  EXPECT_TRUE(ShortcutExists(shortcut2.text));
  EXPECT_FALSE(ShortcutExists(shortcut3.text));
  EXPECT_FALSE(ShortcutExists(shortcut4.text));
  EXPECT_FALSE(ShortcutExists(shortcut5.text));
  EXPECT_FALSE(ShortcutExists(shortcut6.text));
}

TEST_F(ShortcutsBackendTest, DeleteShortcutsWithDeletedOrInactiveKeywords) {
  InitBackend();

  // Add a keyword with `is_active` set to `kUnspecified` (default).
  TemplateURLData data;
  data.SetShortName(u"inactive");
  data.SetKeyword(u"inactive");
  data.SetURL("http://www.inactive.com/{searchTerms}");
  template_url_service()->Add(std::make_unique<TemplateURL>(data));

  // Add a prepopulated keyword with `is_active` set to `kUnspecified`
  // (default).
  TemplateURLData data_prepopulated;
  data_prepopulated.SetShortName(u"prepopulated");
  data_prepopulated.SetKeyword(u"prepopulated");
  data_prepopulated.SetURL("http://www.prepopulated.com/{searchTerms}");
  data_prepopulated.prepopulate_id = 1;
  template_url_service()->Add(std::make_unique<TemplateURL>(data_prepopulated));
  template_url_service()->GetTemplateURLForKeyword(u"inactive_prepopulated");

  ShortcutsDatabase::Shortcut shortcut1(
      "BD85DBA2-8C29-49F9-84AE-48E1E90880DF", u"goog",
      MatchCoreForTesting("http://www.google.com"), base::Time::Now(), 100);
  EXPECT_TRUE(backend()->AddShortcut(shortcut1));

  ShortcutsDatabase::Shortcut shortcut2(
      "BD85DBA2-8C29-49F9-84AE-48E1E90880E0", u"deleted query",
      MatchCoreForTesting("http://www.deleted.com"), base::Time::Now(), 100);
  shortcut2.match_core.keyword = u"deleted";
  EXPECT_TRUE(backend()->AddShortcut(shortcut2));

  ShortcutsDatabase::Shortcut shortcut3(
      "BD85DBA2-8C29-49F9-84AE-48E1E90880E1", u"inactive query",
      MatchCoreForTesting("http://www.inactive.com"), base::Time::Now(), 100);
  shortcut3.match_core.keyword = u"inactive";
  EXPECT_TRUE(backend()->AddShortcut(shortcut3));

  ShortcutsDatabase::Shortcut shortcut4(
      "BD85DBA2-8C29-49F9-84AE-48E1E90880E2", u"sp",
      MatchCoreForTesting("http://www.sport.com"), base::Time::Now(), 10);
  EXPECT_TRUE(backend()->AddShortcut(shortcut4));

  ShortcutsDatabase::Shortcut shortcut5(
      "BD85DBA2-8C29-49F9-84AE-48E1E90880E3", u"prepopulated query",
      MatchCoreForTesting("http://www.inactive_prepopulated.com"),
      base::Time::Now(), 100);
  shortcut5.match_core.keyword = u"prepopulated";
  EXPECT_TRUE(backend()->AddShortcut(shortcut5));

  ASSERT_EQ(shortcuts_map().size(), 5U);
  EXPECT_EQ(shortcuts_map().find(shortcut1.text)->second.id, shortcut1.id);
  EXPECT_EQ(shortcuts_map().find(shortcut2.text)->second.id, shortcut2.id);
  EXPECT_EQ(shortcuts_map().find(shortcut3.text)->second.id, shortcut3.id);
  EXPECT_EQ(shortcuts_map().find(shortcut4.text)->second.id, shortcut4.id);
  EXPECT_EQ(shortcuts_map().find(shortcut5.text)->second.id, shortcut5.id);

  // Delete shortcuts with deleted or inactive keywords.
  backend()->DeleteShortcutsWithDeletedOrInactiveKeywords();

  ASSERT_EQ(shortcuts_map().size(), 3U);
  EXPECT_TRUE(ShortcutExists(shortcut1.text));
  EXPECT_FALSE(ShortcutExists(shortcut2.text));
  EXPECT_FALSE(ShortcutExists(shortcut3.text));
  EXPECT_TRUE(ShortcutExists(shortcut4.text));
  EXPECT_TRUE(ShortcutExists(shortcut5.text));
}

TEST_F(ShortcutsBackendTest, AddOrUpdateShortcut_3CharShortening) {
  InitBackend();

  AutocompleteMatch match;
  match.type = AutocompleteMatchType::NAVSUGGEST;
  match.destination_url = GURL("https://www.google.com");

  // Should not have a shortcut initially.
  EXPECT_EQ(shortcuts_map().size(), 0u);
  EXPECT_FALSE(ShortcutExists(u"we need very long text for this test"));

  // Should have shortcut after shortcut is added to a match.
  backend()->AddOrUpdateShortcut(u"we need very long text for this test",
                                 match);
  EXPECT_EQ(shortcuts_map().size(), 1u);
  EXPECT_TRUE(ShortcutExists(u"we need very long text for this test"));

  // Should not shorten shortcut when a slightly shorter input (less than 3
  // chars shorter) is used for the match.
  backend()->AddOrUpdateShortcut(u"we need very long text for this t", match);
  EXPECT_EQ(shortcuts_map().size(), 1u);
  EXPECT_FALSE(ShortcutExists(u"we need very long text for this t"));
  EXPECT_TRUE(ShortcutExists(u"we need very long text for this test"));

  // Should shorten shortcut when a shorter input is used for the match.
  backend()->AddOrUpdateShortcut(u"we need very long", match);
  EXPECT_EQ(shortcuts_map().size(), 1u);
  EXPECT_TRUE(ShortcutExists(u"we need very long text"));
  EXPECT_FALSE(ShortcutExists(u"we need very long text for this test"));

  // Should add new shortcut when a longer input is used for the match.
  backend()->AddOrUpdateShortcut(u"we need very long text for this test",
                                 match);
  EXPECT_EQ(shortcuts_map().size(), 2u);
  EXPECT_TRUE(ShortcutExists(u"we need very long text"));
  EXPECT_TRUE(ShortcutExists(u"we need very long text for this test"));

  // Should shorten shortcut when a shorter input is used for the match. The
  // shorter shortcut to the same match should remain.
  backend()->AddOrUpdateShortcut(u"we need very long text fo", match);
  EXPECT_EQ(shortcuts_map().size(), 2u);
  EXPECT_TRUE(ShortcutExists(u"we need very long text"));
  EXPECT_TRUE(ShortcutExists(u"we need very long text for this"));
  EXPECT_FALSE(ShortcutExists(u"we need very long text for this test"));

  // Should only touch the shortest shortcut. The longer shortcut to the same
  // match should remain.
  backend()->AddOrUpdateShortcut(u"we", match);
  EXPECT_EQ(shortcuts_map().size(), 2u);
  EXPECT_TRUE(ShortcutExists(u"we need"));
  EXPECT_FALSE(ShortcutExists(u"we need very long text"));
  EXPECT_TRUE(ShortcutExists(u"we need very long text for this"));
}

TEST_F(ShortcutsBackendTest, AddOrUpdateShortcut_Expanding) {
  InitBackend();

  AutocompleteMatch match;
  match.type = AutocompleteMatchType::NAVSUGGEST;
  match.destination_url = GURL("https://www.host-sharedB.com/path");
  match.description = u"https://www.description.com";
  match.contents = u"a an app apple i it word ZaZaaZZ symbols(╯°□°）╯ sharedA";
  match.contents_class.emplace_back(0, 0);
  match.description_class.emplace_back(0, 0);

  // Should not have a shortcut initially.
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre());

  // Should expand last word when creating shortcuts.
  backend()->AddOrUpdateShortcut(u"w w", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"w word"));

  // Should expand last word when updating shortcuts.
  backend()->AddOrUpdateShortcut(u"w w", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"w word"));
  ClearShortcutsMap();

  // Should not expand other words when the last word is already expanded.
  backend()->AddOrUpdateShortcut(u"w word", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"w word"));
  ClearShortcutsMap();

  // Should prefer to expand to words at least 3 chars long. Should pick words
  // that come first, if there are multiple matches at least 3 chars long.
  backend()->AddOrUpdateShortcut(u"a", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"app"));
  ClearShortcutsMap();

  // Should prefer to expand to words that come first if all matches are shorter
  // than 3 chars long.
  backend()->AddOrUpdateShortcut(u"i", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"i"));
  ClearShortcutsMap();

  // Should not expand to words in the `description`.
  backend()->AddOrUpdateShortcut(u"d", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"d"));
  ClearShortcutsMap();

  // Should not expand to words in the URL path.
  backend()->AddOrUpdateShortcut(u"p", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"p"));
  ClearShortcutsMap();

  // Should expand to words in the URL host.
  backend()->AddOrUpdateShortcut(u"h", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"host"));
  ClearShortcutsMap();

  // Should prefer expanding to words in the `contents`.
  backend()->AddOrUpdateShortcut(u"shar", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"sharedA"));
  ClearShortcutsMap();

  // When updating, should expand the last word after appending up to 3 chars.
  backend()->AddOrUpdateShortcut(u"an apple a", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"an apple app"));
  // 'an appl[e a]' should be expanded to 'an apple app'.
  backend()->AddOrUpdateShortcut(u"an appl", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"an apple app"));
  // But 'an app[le ]' should be expanded to 'an apple', removing the word 'app'
  // and trailing whitespace.
  backend()->AddOrUpdateShortcut(u"an app", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"an apple"));
  ClearShortcutsMap();

  // Should be case-insensitive when matching, but preserve case when creating
  // shortcut text. The match word is 'ZaZaaZZ'.
  // '[zAz][aaZZ]': the matched shortcut text should preserve the case of the
  // input, while the expanded shortcut text should preserve the case of the
  // match title.
  backend()->AddOrUpdateShortcut(u"zAz", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"zAzaaZZ"));

  // When updating, '[Z][Az][aaZZ]': the matched shortcut text should preserve
  // the case of the input, while the expanded shortcut text should preserve the
  // case of the previous shortcut text rather than the match title.
  backend()->AddOrUpdateShortcut(u"Z", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"ZAzaaZZ"));
  ClearShortcutsMap();

  // Should match inconsistent trailing whitespace and expand the last word
  // correctly.
  backend()->AddOrUpdateShortcut(u"appl   ", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"apple"));
  // Likewise for updating shortcuts.
  backend()->AddOrUpdateShortcut(u"appl   ", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"apple"));
  ClearShortcutsMap();

  // Should neither crash nor expand texts containing no words. Should still add
  // the text if it contains symbols, but not if it contains only whitespace.
  backend()->AddOrUpdateShortcut(u"(╯°□°", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"(╯°□°"));
  ClearShortcutsMap();
  backend()->AddOrUpdateShortcut(u"    ", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre());
  ClearShortcutsMap();

  // Should not expand words with trailing word-breaking symbols.
  backend()->AddOrUpdateShortcut(u"symb°□°", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"symb°□°"));
  ClearShortcutsMap();

  // Should expand words following symbols.
  backend()->AddOrUpdateShortcut(u"(╯°□°）╯symb", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"(╯°□°）╯symbols"));
  ClearShortcutsMap();

  // Should neither crash nor add a shortcut when text is empty.
  backend()->AddOrUpdateShortcut(u"", match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre());

  // Should not expand when match contents is empty.
  AutocompleteMatch match_without_contents;
  match_without_contents.type = AutocompleteMatchType::NAVSUGGEST;
  match_without_contents.destination_url = GURL("https://www.host.com/google");
  match_without_contents.description = u"google";
  match_without_contents.description_class.emplace_back(0, 0);
  backend()->AddOrUpdateShortcut(u"goo", match_without_contents);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"goo"));
  ClearShortcutsMap();

  // Should expand with description when `swap_contents_and_description` is
  // true.
  AutocompleteMatch swapped_match;
  swapped_match.type = AutocompleteMatchType::NAVSUGGEST;
  swapped_match.swap_contents_and_description = true;
  swapped_match.destination_url = GURL("https://www.google.com");
  swapped_match.contents = u"https://www.googlecontents.com";
  swapped_match.description = u"googledescription";
  swapped_match.contents_class.emplace_back(0, 0);
  swapped_match.description_class.emplace_back(0, 0);
  backend()->AddOrUpdateShortcut(u"goo", swapped_match);
  EXPECT_THAT(ShortcutsMapTexts(), testing::ElementsAre(u"googledescription"));
  ClearShortcutsMap();
}

TEST_F(ShortcutsBackendTest, AddOrUpdateShortcut_Expanding_Prefix) {
  // Test `ExpandToFullWord(), specifically focussing on detecting and handling
  // the cases when `text` is a prefix of `match_text`.

  InitBackend();

  const auto test = [&](const std::string& text, const std::string& match_text,
                        const std::string& expected_expanded_text) {
    SCOPED_TRACE("Text: " + text + ", match_text: " + match_text);
    AutocompleteMatch match;
    match.type = AutocompleteMatchType::NAVSUGGEST;
    match.contents = base::UTF8ToUTF16(match_text);
    match.contents_class.emplace_back(0, 0);

    // Should expand last word when creating shortcuts.
    backend()->AddOrUpdateShortcut(base::UTF8ToUTF16(text), match);
    EXPECT_THAT(
        ShortcutsMapTexts(),
        testing::ElementsAre(base::UTF8ToUTF16(expected_expanded_text)));

    ClearShortcutsMap();
  };

  // When `text` does prefix `match_text`, should expand to the next word in
  // `match_text` instead of the 1st matching word.
  test("x", "xA xB", "xA");
  test("xA x", "xA xB", "xA xB");

  // When `text` doesn't prefix `match_text`, should expand to the 1st matching
  // word in `match_text`.
  test("xB x", "xA xB", "xB xA");
  // Even if that produces repeated words. (It'd be too complicated to avoid
  // this without introducing even greater edge cases).
  test("xA x", "xA y xB", "xA xA");

  // When prefix matching, should use the next word even if its short.
  test("xA x", "xA y xB xyz", "xA xyz");
  // When not prefix matching, should use the 1st word at least 3 chars long if
  // available.
  test("xA x", "xA xB xyz", "xA xB");

  // Both prefix and non prefix matching should handle trailing whitespace.
  // Trailing whitespace should not prompt expansion to the next `match_text`.
  test("xA ", "xA xB", "xA");
  // Trailing whitespace should not prevent expansion of the last `text` word.
  test("xA xy ", "xA xyz", "xA xyz");
  test("xA xyz ", "xA xyz", "xA xyz");
}

TEST_F(ShortcutsBackendTest, AddOrUpdateShortcut_Expanding_Case) {
  // Test `ExpandToFullWord(), specifically focussing on correct upper v lower
  // case.

  InitBackend();

  const auto test = [&](const std::string& text, const std::string& match_text,
                        const std::string& expected_expanded_text) {
    SCOPED_TRACE("Text: " + text + ", match_text: " + match_text);
    AutocompleteMatch match;
    match.type = AutocompleteMatchType::NAVSUGGEST;
    match.contents = base::UTF8ToUTF16(match_text);
    match.contents_class.emplace_back(0, 0);

    // Should expand last word when creating shortcuts.
    backend()->AddOrUpdateShortcut(base::UTF8ToUTF16(text), match);
    EXPECT_THAT(
        ShortcutsMapTexts(),
        testing::ElementsAre(base::UTF8ToUTF16(expected_expanded_text)));

    ClearShortcutsMap();
  };

  // Should preserve input case for the typed portion. Should preserve match
  // case for the expanded portion.
  test("lowerUPPER", "LOWERupperlowerUPPER", "lowerUPPERlowerUPPER");

  // Should not crash when the input or shortcut contain characters that change
  // length when their case changes. E.g. the dotted i 'İ' is 1 character long
  // in upper case, but 'i̇' is 2 characters (i and ̇) in lower case.

  // Upper case 'İ' in input - should preserve input case.
  test("xİx", "xİxy", "xİxy");
  test("xİx", "xi̇xy", "xİxy");
  // Lower case 'i̇' in input - should preserve input case.
  test("xi̇x", "xİxy", "xi̇xy");
  test("xi̇x", "xi̇xy", "xi̇xy");
  // 'İ' not present in input - should not preserve match case; should force
  // lowercase.
  test("x", "xİxy", "xi̇xy");
  test("x", "xi̇xy", "xi̇xy");

  // Also test updating existing shortcuts, which involves an additional
  // case-sensitive operation when append 3 chars to the input.
  const auto test_update = [&](const std::string& text,
                               const std::string& match_text,
                               const std::string& expected_expanded_text) {
    SCOPED_TRACE("Text: " + text + ", match_text: " + match_text);
    AutocompleteMatch match;
    match.type = AutocompleteMatchType::NAVSUGGEST;
    match.contents = base::UTF8ToUTF16(match_text);
    match.contents_class.emplace_back(0, 0);
    match.destination_url = GURL("http://www.url.com");

    backend()->AddOrUpdateShortcut(match.contents, match);

    // Should expand last word when creating shortcuts.
    backend()->AddOrUpdateShortcut(base::UTF8ToUTF16(text), match);
    EXPECT_THAT(
        ShortcutsMapTexts(),
        testing::ElementsAre(base::UTF8ToUTF16(expected_expanded_text)));

    ClearShortcutsMap();
  };

  // Upper case 'İ' in input.
  test_update("xİx", "xİxy", "xİxy");
  test_update("xİx", "xi̇xy", "xİxy");
  // Lower case 'i̇' in input.
  test_update("xi̇x", "xİxy", "xi̇xy");
  test_update("xi̇x", "xi̇xy", "xi̇xy");
  // 'İ' not present in input.
  test_update("x", "xİxy", "xi̇xy");
  test_update("x", "xi̇xy", "xi̇xy");
}
