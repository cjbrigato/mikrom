// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_OMNIBOX_MODEL_PEDAL_SECTION_EXTRACTOR_H_
#define IOS_CHROME_BROWSER_OMNIBOX_MODEL_PEDAL_SECTION_EXTRACTOR_H_

#import "ios/chrome/browser/omnibox/model/autocomplete_suggestion.h"

@class AutocompleteMatchFormatter;

/// Delegate for PedalSectionExtractor.
@protocol PedalSectionExtractorDelegate <NSObject>

/// Removes the pedal group from suggestions. Pedal are removed from suggestions
/// with a debouce timer in `PedalSectionExtractor`. When the timer ends the
/// pedal group is removed.
- (void)invalidatePedals;

@end

/// Extract pedal from AutocompleteSuggestion and wrap them in new
/// AutocompleteSuggestion.
@interface PedalSectionExtractor : NSObject

@property(nonatomic, weak) id<PedalSectionExtractorDelegate> delegate;

- (id<AutocompleteSuggestionGroup>)extractPedals:
    (NSArray<AutocompleteMatchFormatter*>*)suggestions;

/// Returns whether the object stores pedals in cache. Used in tests.
- (BOOL)hasCachedPedals;

@end

#endif  // IOS_CHROME_BROWSER_OMNIBOX_MODEL_PEDAL_SECTION_EXTRACTOR_H_
