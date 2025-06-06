// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_LINK_HEADER_UTIL_LINK_HEADER_UTIL_H_
#define COMPONENTS_LINK_HEADER_UTIL_LINK_HEADER_UTIL_H_

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace link_header_util {

using StringIteratorPair =
    std::pair<std::string::const_iterator, std::string::const_iterator>;

// Split a Link: header in its individual values. A single Link: header can
// contain multiple values, which are comma separated. This method splits the
// entire string into iterator pairs for the individual link values.
// This is very similar to what net::HttpUtil::ValuesIterator does, except it
// takes the special syntax of <> enclosed URLs into account.
std::vector<StringIteratorPair> SplitLinkHeader(const std::string& header);

// Parse an individual link header in its URL and parameters. `begin` and `end`
// indicate the string to parse. If it is successfully parsed as a link header
// value this method returns the URL part of the link header value and adds the
// parameters from the link header value to `params`. All keys of `params` are
// lower cased. If any error occurs parsing, this returns nullops (but might
// have also modified `params`).
std::optional<std::string> ParseLinkHeaderValue(
    std::string_view header,
    std::unordered_map<std::string, std::optional<std::string>>& params);

// Overload that allows consumers not to care about the type returned by
// SplitLinkHeader, to facilitate switching to making SplitLinkHeader returning
// a vector of string_views.
std::optional<std::string> ParseLinkHeaderValue(
    const StringIteratorPair& string_iterator_pair,
    std::unordered_map<std::string, std::optional<std::string>>& params);

}  // namespace link_header_util

#endif  // COMPONENTS_LINK_HEADER_UTIL_LINK_HEADER_UTIL_H_
