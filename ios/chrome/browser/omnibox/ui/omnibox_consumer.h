// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_OMNIBOX_UI_OMNIBOX_CONSUMER_H_
#define IOS_CHROME_BROWSER_OMNIBOX_UI_OMNIBOX_CONSUMER_H_

#import <UIKit/UIKit.h>

#import <string>

@protocol OmniboxConsumer <NSObject>

// Notifies the consumer to update the autocomplete icon for the currently
// highlighted autocomplete result with given accessibility identifier.
- (void)updateAutocompleteIcon:(UIImage*)icon
    withAccessibilityIdentifier:(NSString*)accessibilityIdentifier;

// Notifies the consumer to update after the search-by-image support status
// changes. (This is usually when the default search engine changes).
- (void)updateSearchByImageSupported:(BOOL)searchByImageSupported;

// Notifies the consumer to update after the Lens support status
// changes. (This is usually when the default search engine changes).
- (void)updateLensImageSupported:(BOOL)lensImageSupported;

/// Sets the omnibox placeholder text, which needs to update when the search
/// engine changes.
- (void)setPlaceholderText:(NSString*)searchProviderName;

/// Sets the omnibox placeholder for search-only contexts (when URL cannot be
/// typed). E.g. lens overlay MMSB
- (void)setSearchOnlyPlaceholderText:(NSString*)searchOnlyPlaceholderText;

// Notifies the consumer to set the following image as an image
// in an omnibox with empty text
- (void)setEmptyTextLeadingImage:(UIImage*)icon;

/// Sets the thumbnail image used for image search. Set to`nil` to hide the
/// thumbnail.
- (void)setThumbnailImage:(UIImage*)image;

/// Updates the return key availability.
- (void)updateReturnKeyAvailability;

@end

#endif  // IOS_CHROME_BROWSER_OMNIBOX_UI_OMNIBOX_CONSUMER_H_
