// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox.suggestions;

import org.chromium.build.annotations.NullMarked;
import org.chromium.components.omnibox.AutocompleteInput;
import org.chromium.components.omnibox.AutocompleteMatch;
import org.chromium.ui.modelutil.PropertyModel;

/**
 * A processor of omnibox suggestions. Implementers are provided the opportunity to analyze a
 * suggestion and create a custom model.
 */
@NullMarked
public interface SuggestionProcessor extends DropdownItemProcessor {
    /**
     * @param suggestion The suggestion to process.
     * @param position The position of the suggestion in the list.
     * @return Whether this suggestion processor handles this type of suggestion at this position.
     */
    boolean doesProcessSuggestion(AutocompleteMatch suggestion, int position);

    /**
     * Populate a model for the given suggestion.
     *
     * @param input The input to produce the suggestions
     * @param suggestion The suggestion to populate the model for.
     * @param model The model to populate.
     * @param position The position of the suggestion in the list.
     */
    void populateModel(
            AutocompleteInput input,
            AutocompleteMatch suggestion,
            PropertyModel model,
            int position);
}
