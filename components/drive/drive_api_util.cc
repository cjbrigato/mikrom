// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/drive/drive_api_util.h"

#include <array>
#include <string>
#include <string_view>

#include "base/containers/heap_array.h"
#include "base/files/file.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/atomic_flag.h"
#include "crypto/obsolete/md5.h"
#include "third_party/re2/src/re2/re2.h"

namespace drive {
namespace util {

// Not inside the anonymous namespace so it can be friended by
// crypto/obsolete/md5.
crypto::obsolete::Md5 MakeMd5HasherForDriveApi() {
  return {};
}

namespace {

struct HostedDocumentKind {
  const char* mime_type;
  const char* extension;
};

constexpr auto kHostedDocumentKinds = std::to_array<HostedDocumentKind>({
    {kGoogleDocumentMimeType, ".gdoc"},
    {kGoogleSpreadsheetMimeType, ".gsheet"},
    {kGooglePresentationMimeType, ".gslides"},
    {kGoogleDrawingMimeType, ".gdraw"},
    {kGoogleTableMimeType, ".gtable"},
    {kGoogleFormMimeType, ".gform"},
    {kGoogleMapMimeType, ".gmaps"},
    {kGoogleSiteMimeType, ".gsite"},
    {kEmailLayoutsMimeType, ".gmaillayout"},
});

const char kUnknownHostedDocumentExtension[] = ".glink";

}  // namespace

std::string EscapeQueryStringValue(const std::string& str) {
  std::string result;
  result.reserve(str.size());
  for (size_t i = 0; i < str.size(); ++i) {
    if (str[i] == '\\' || str[i] == '\'') {
      result.push_back('\\');
    }
    result.push_back(str[i]);
  }
  return result;
}

std::string TranslateQuery(const std::string& original_query) {
  // In order to handle non-ascii white spaces correctly, convert to UTF16.
  std::u16string query = base::UTF8ToUTF16(original_query);
  const std::u16string kDelimiter =
      base::StrCat({base::kWhitespaceUTF16, u"\""});

  std::string result;
  for (size_t index = query.find_first_not_of(base::kWhitespaceUTF16);
       index != std::u16string::npos;
       index = query.find_first_not_of(base::kWhitespaceUTF16, index)) {
    bool is_exclusion = (query[index] == '-');
    if (is_exclusion)
      ++index;
    if (index == query.length()) {
      // Here, the token is '-' and it should be ignored.
      continue;
    }

    size_t begin_token = index;
    std::u16string token;
    if (query[begin_token] == '"') {
      // Quoted query.
      ++begin_token;
      size_t end_token = query.find('"', begin_token);
      if (end_token == std::u16string::npos) {
        // This is kind of syntax error, since quoted string isn't finished.
        // However, the query is built by user manually, so here we treat
        // whole remaining string as a token as a fallback, by appending
        // a missing double-quote character.
        end_token = query.length();
        query.push_back('"');
      }

      token = query.substr(begin_token, end_token - begin_token);
      index = end_token + 1;  // Consume last '"', too.
    } else {
      size_t end_token = query.find_first_of(kDelimiter, begin_token);
      if (end_token == std::u16string::npos) {
        end_token = query.length();
      }

      token = query.substr(begin_token, end_token - begin_token);
      index = end_token;
    }

    if (token.empty()) {
      // Just ignore an empty token.
      continue;
    }

    if (!result.empty()) {
      // If there are two or more tokens, need to connect with "and".
      result.append(" and ");
    }

    // The meaning of "fullText" should include title, description and content.
    base::StringAppendF(
        &result,
        "%sfullText contains \'%s\'",
        is_exclusion ? "not " : "",
        EscapeQueryStringValue(base::UTF16ToUTF8(token)).c_str());
  }

  return result;
}

std::string CanonicalizeResourceId(const std::string& resource_id) {
  // If resource ID is in the old WAPI format starting with a prefix like
  // "document:", strip it and return the remaining part.
  std::string stripped_resource_id;
  if (RE2::FullMatch(resource_id, "^[a-z-]+(?::|%3A)([\\w-]+)$",
                     &stripped_resource_id))
    return stripped_resource_id;
  return resource_id;
}

std::string GetMd5Digest(const base::FilePath& file_path) {
  constexpr size_t kMd5DigestBufferSize = 512 * 1024;  // 512 kB.
  base::File file(file_path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid())
    return std::string();

  auto md5 = MakeMd5HasherForDriveApi();

  auto buffer = base::HeapArray<uint8_t>::Uninit(kMd5DigestBufferSize);
  std::optional<size_t> result;
  do {
    result = file.ReadAtCurrentPos(buffer.as_span());
    if (result.has_value()) {
      md5.Update(buffer.as_span().first(*result));
    }
  } while (result.has_value() && result.value() > 0);

  if (!result.has_value()) {
    return std::string();
  }

  return base::ToLowerASCII(base::HexEncode(md5.Finish()));
}

bool IsKnownHostedDocumentMimeType(const std::string& mime_type) {
  for (size_t i = 0; i < std::size(kHostedDocumentKinds); ++i) {
    if (mime_type == kHostedDocumentKinds[i].mime_type)
      return true;
  }
  return false;
}

bool HasHostedDocumentExtension(const base::FilePath& path) {
  const std::string extension = base::FilePath(path.Extension()).AsUTF8Unsafe();
  for (size_t i = 0; i < std::size(kHostedDocumentKinds); ++i) {
    if (extension == kHostedDocumentKinds[i].extension)
      return true;
  }
  return extension == kUnknownHostedDocumentExtension;
}

bool IsEncryptedMimeType(const std::string& mime_type) {
  return base::StartsWith(mime_type, kEncryptedMimeType);
}

}  // namespace util
}  // namespace drive
