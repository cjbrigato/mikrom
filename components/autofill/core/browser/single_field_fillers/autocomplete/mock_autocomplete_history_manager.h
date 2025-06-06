// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_SINGLE_FIELD_FILLERS_AUTOCOMPLETE_MOCK_AUTOCOMPLETE_HISTORY_MANAGER_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_SINGLE_FIELD_FILLERS_AUTOCOMPLETE_MOCK_AUTOCOMPLETE_HISTORY_MANAGER_H_

#include "base/memory/weak_ptr.h"
#include "components/autofill/core/browser/single_field_fillers/autocomplete/autocomplete_history_manager.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace autofill {

class MockAutocompleteHistoryManager : public AutocompleteHistoryManager {
 public:
  MockAutocompleteHistoryManager();
  ~MockAutocompleteHistoryManager() override;

  MOCK_METHOD(bool,
              OnGetSingleFieldSuggestions,
              (const FormFieldData& field,
               const AutofillClient& client,
               SingleFieldFillRouter::OnSuggestionsReturnedCallback& callback),
              (override));
  MOCK_METHOD(void,
              OnWillSubmitFormWithFields,
              (const std::vector<FormFieldData>& fields,
               bool is_autocomplete_enabled),
              (override));
  MOCK_METHOD(void, CancelPendingQueries, (), (override));
  MOCK_METHOD(void,
              OnRemoveCurrentSingleFieldSuggestion,
              (const std::u16string&, const std::u16string&, SuggestionType),
              (override));
  MOCK_METHOD(void,
              OnSingleFieldSuggestionSelected,
              (const Suggestion& suggestion),
              (override));
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_SINGLE_FIELD_FILLERS_AUTOCOMPLETE_MOCK_AUTOCOMPLETE_HISTORY_MANAGER_H_
