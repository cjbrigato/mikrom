// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_response_headers.h"

#include <stdint.h>

#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_set>

#include "base/pickle.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/types/optional_util.h"
#include "base/values.h"
#include "net/base/cronet_buildflags.h"
#include "net/base/tracing.h"
#include "net/http/http_byte_range.h"
#include "net/http/http_response_headers_test_util.h"
#include "net/http/http_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/perfetto/include/perfetto/test/traced_value_test_support.h"

namespace net {

namespace {

struct TestData {
  const char* raw_headers;
  const char* expected_headers;
  HttpVersion expected_version;
  int expected_response_code;
  const char* expected_status_text;
};

class HttpResponseHeadersTest : public testing::Test {
};

// Transform "normal"-looking headers (\n-separated) to the appropriate
// input format for ParseRawHeaders (\0-separated).
void HeadersToRaw(std::string* headers) {
  std::replace(headers->begin(), headers->end(), '\n', '\0');
  if (!headers->empty())
    *headers += '\0';
}

class HttpResponseHeadersCacheControlTest : public HttpResponseHeadersTest {
 protected:
  // Make tests less verbose.
  typedef base::TimeDelta TimeDelta;

  // Initilise the headers() value with a Cache-Control header set to
  // |cache_control|. |cache_control| is copied and so can safely be a
  // temporary.
  void InitializeHeadersWithCacheControl(const char* cache_control) {
    std::string raw_headers("HTTP/1.1 200 OK\n");
    raw_headers += "Cache-Control: ";
    raw_headers += cache_control;
    raw_headers += "\n";
    HeadersToRaw(&raw_headers);
    headers_ = base::MakeRefCounted<HttpResponseHeaders>(raw_headers);
  }

  const scoped_refptr<HttpResponseHeaders>& headers() { return headers_; }

  // Get the max-age value. This should only be used in tests where a valid
  // max-age parameter is expected to be present.
  TimeDelta GetMaxAgeValue() {
    DCHECK(headers_.get()) << "Call InitializeHeadersWithCacheControl() first";
    std::optional<TimeDelta> max_age_value = headers()->GetMaxAgeValue();
    EXPECT_TRUE(max_age_value);
    return max_age_value.value();
  }

  // Get the stale-while-revalidate value. This should only be used in tests
  // where a valid max-age parameter is expected to be present.
  TimeDelta GetStaleWhileRevalidateValue() {
    DCHECK(headers_.get()) << "Call InitializeHeadersWithCacheControl() first";
    std::optional<TimeDelta> stale_while_revalidate_value =
        headers()->GetStaleWhileRevalidateValue();
    EXPECT_TRUE(stale_while_revalidate_value);
    return stale_while_revalidate_value.value();
  }

 private:
  scoped_refptr<HttpResponseHeaders> headers_;
};

class CommonHttpResponseHeadersTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<TestData> {
};

constexpr auto ToSimpleString = test::HttpResponseHeadersToSimpleString;

// Transform to readable output format (so it's easier to see diffs).
void EscapeForPrinting(std::string* s) {
  std::replace(s->begin(), s->end(), ' ', '_');
  std::replace(s->begin(), s->end(), '\n', '\\');
}

TEST_P(CommonHttpResponseHeadersTest, TestCommon) {
  const TestData test = GetParam();

  std::string raw_headers(test.raw_headers);
  HeadersToRaw(&raw_headers);
  std::string expected_headers(test.expected_headers);

  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(raw_headers);
  std::string headers = ToSimpleString(parsed);

  EscapeForPrinting(&headers);
  EscapeForPrinting(&expected_headers);

  EXPECT_EQ(expected_headers, headers);

  SCOPED_TRACE(test.raw_headers);

  EXPECT_TRUE(test.expected_version == parsed->GetHttpVersion());
  EXPECT_EQ(test.expected_response_code, parsed->response_code());
  EXPECT_EQ(test.expected_status_text, parsed->GetStatusText());
}

TestData response_headers_tests[] = {
    {// Normalize whitespace.
     "HTTP/1.1    202   Accepted  \n"
     "Content-TYPE  : text/html; charset=utf-8  \n"
     "Set-Cookie: a \n"
     "Set-Cookie:   b \n",

     "HTTP/1.1 202 Accepted\n"
     "Content-TYPE: text/html; charset=utf-8\n"
     "Set-Cookie: a\n"
     "Set-Cookie: b\n",

     HttpVersion(1, 1), 202, "Accepted"},
    {// Normalize leading whitespace.
     "HTTP/1.1    202   Accepted  \n"
     // Starts with space -- will be skipped as invalid.
     "  Content-TYPE  : text/html; charset=utf-8  \n"
     "Set-Cookie: a \n"
     "Set-Cookie:   b \n",

     "HTTP/1.1 202 Accepted\n"
     "Set-Cookie: a\n"
     "Set-Cookie: b\n",

     HttpVersion(1, 1), 202, "Accepted"},
    {// Keep whitespace within status text.
     "HTTP/1.0 404 Not   found  \n",

     "HTTP/1.0 404 Not   found\n",

     HttpVersion(1, 0), 404, "Not   found"},
    {// Normalize blank headers.
     "HTTP/1.1 200 OK\n"
     "Header1 :          \n"
     "Header2: \n"
     "Header3:\n"
     "Header4\n"
     "Header5    :\n",

     "HTTP/1.1 200 OK\n"
     "Header1: \n"
     "Header2: \n"
     "Header3: \n"
     "Header5: \n",

     HttpVersion(1, 1), 200, "OK"},
    {// Don't believe the http/0.9 version if there are headers!
     "hTtP/0.9 201\n"
     "Content-TYPE: text/html; charset=utf-8\n",

     "HTTP/1.0 201\n"
     "Content-TYPE: text/html; charset=utf-8\n",

     HttpVersion(1, 0), 201, ""},
    {// Accept the HTTP/0.9 version number if there are no headers.
     // This is how HTTP/0.9 responses get constructed from
     // HttpNetworkTransaction.
     "hTtP/0.9 200 OK\n",

     "HTTP/0.9 200 OK\n",

     HttpVersion(0, 9), 200, "OK"},
    {// Do not add missing status text.
     "HTTP/1.1 201\n"
     "Content-TYPE: text/html; charset=utf-8\n",

     "HTTP/1.1 201\n"
     "Content-TYPE: text/html; charset=utf-8\n",

     HttpVersion(1, 1), 201, ""},
    {// Normalize bad status line.
     "SCREWED_UP_STATUS_LINE\n"
     "Content-TYPE: text/html; charset=utf-8\n",

     "HTTP/1.0 200 OK\n"
     "Content-TYPE: text/html; charset=utf-8\n",

     HttpVersion(1, 0), 200, "OK"},
    {// Normalize bad status line.
     "Foo bar.",

     "HTTP/1.0 200\n",

     HttpVersion(1, 0), 200, ""},
    {// Normalize invalid status code.
     "HTTP/1.1 -1  Unknown\n",

     "HTTP/1.1 200\n",

     HttpVersion(1, 1), 200, ""},
    {// Normalize empty header.
     "",

     "HTTP/1.0 200 OK\n",

     HttpVersion(1, 0), 200, "OK"},
    {// Normalize headers that start with a colon.
     "HTTP/1.1    202   Accepted  \n"
     "foo: bar\n"
     ": a \n"
     " : b\n"
     "baz: blat \n",

     "HTTP/1.1 202 Accepted\n"
     "foo: bar\n"
     "baz: blat\n",

     HttpVersion(1, 1), 202, "Accepted"},
    {// Normalize headers that end with a colon.
     "HTTP/1.1    202   Accepted  \n"
     "foo:   \n"
     "bar:\n"
     "baz: blat \n"
     "zip:\n",

     "HTTP/1.1 202 Accepted\n"
     "foo: \n"
     "bar: \n"
     "baz: blat\n"
     "zip: \n",

     HttpVersion(1, 1), 202, "Accepted"},
    {// Normalize whitespace headers.
     "\n   \n",

     "HTTP/1.0 200 OK\n",

     HttpVersion(1, 0), 200, "OK"},
    {// Has multiple Set-Cookie headers.
     "HTTP/1.1 200 OK\n"
     "Set-Cookie: x=1\n"
     "Set-Cookie: y=2\n",

     "HTTP/1.1 200 OK\n"
     "Set-Cookie: x=1\n"
     "Set-Cookie: y=2\n",

     HttpVersion(1, 1), 200, "OK"},
    {// Has multiple cache-control headers.
     "HTTP/1.1 200 OK\n"
     "Cache-control: private\n"
     "cache-Control: no-store\n",

     "HTTP/1.1 200 OK\n"
     "Cache-control: private\n"
     "cache-Control: no-store\n",

     HttpVersion(1, 1), 200, "OK"},
    {// Has multiple-value cache-control header.
     "HTTP/1.1 200 OK\n"
     "Cache-Control: private, no-store\n",

     "HTTP/1.1 200 OK\n"
     "Cache-Control: private, no-store\n",

     HttpVersion(1, 1), 200, "OK"},
    {// Missing HTTP.
     " 200 Yes\n",

     "HTTP/1.0 200 Yes\n",

     HttpVersion(1, 0), 200, "Yes"},
    {// Only HTTP.
     "HTTP\n",

     "HTTP/1.0 200 OK\n",

     HttpVersion(1, 0), 200, "OK"},
    {// Missing HTTP version.
     "HTTP 404 No\n",

     "HTTP/1.0 404 No\n",

     HttpVersion(1, 0), 404, "No"},
    {// Missing dot in HTTP version.
     "HTTP/1 304 Not Friday\n",

     "HTTP/1.0 304 Not Friday\n",

     HttpVersion(1, 0), 304, "Not Friday"},
    {// Multi-digit HTTP version (our error detection is bad).
     "HTTP/234.01 204 Nothing here\n",

     "HTTP/2.0 204 Nothing here\n",

     HttpVersion(2, 0), 204, "Nothing here"},
    {// HTTP minor version attached to response code (pretty bad parsing).
     "HTTP/1 302.1 Bad parse\n",

     "HTTP/1.1 302 .1 Bad parse\n",

     HttpVersion(1, 1), 302, ".1 Bad parse"},
    {// HTTP minor version inside the status text (bad parsing).
     "HTTP/1 410 Gone in 0.1 seconds\n",

     "HTTP/1.1 410 Gone in 0.1 seconds\n",

     HttpVersion(1, 1), 410, "Gone in 0.1 seconds"},
    {// Status text smushed into response code.
     "HTTP/1.1 426Smush\n",

     "HTTP/1.1 426 Smush\n",

     HttpVersion(1, 1), 426, "Smush"},
    {// Tab not recognised as separator (this is standard compliant).
     "HTTP/1.1\t500 204 Bad\n",

     "HTTP/1.1 204 Bad\n",

     HttpVersion(1, 1), 204, "Bad"},
    {// Junk after HTTP version is ignored.
     "HTTP/1.1ignored 201 Not ignored\n",

     "HTTP/1.1 201 Not ignored\n",

     HttpVersion(1, 1), 201, "Not ignored"},
    {// Tab gets included in status text.
     "HTTP/1.1 501\tStatus\t\n",

     "HTTP/1.1 501 \tStatus\t\n",

     HttpVersion(1, 1), 501, "\tStatus\t"},
    {// Zero response code.
     "HTTP/1.1 0 Zero\n",

     "HTTP/1.1 0 Zero\n",

     HttpVersion(1, 1), 0, "Zero"},
    {// Oversize response code.
     "HTTP/1.1 20230904 Monday\n",

     "HTTP/1.1 20230904 Monday\n",

     HttpVersion(1, 1), 20230904, "Monday"},
    {// Overflowing response code.
     "HTTP/1.1 9123456789 Overflow\n",

     "HTTP/1.1 9123456789 Overflow\n",

     HttpVersion(1, 1), 2147483647, "Overflow"},
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         CommonHttpResponseHeadersTest,
                         testing::ValuesIn(response_headers_tests));

struct PersistData {
  HttpResponseHeaders::PersistOptions options;
  const char* raw_headers;
  const char* expected_headers;
};

class PersistenceTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<PersistData> {
};

TEST_P(PersistenceTest, Persist) {
  const PersistData test = GetParam();

  std::string headers = test.raw_headers;
  HeadersToRaw(&headers);
  auto parsed1 = base::MakeRefCounted<HttpResponseHeaders>(headers);

  base::Pickle pickle;
  parsed1->Persist(&pickle, test.options);

  base::PickleIterator iter(pickle);
  auto parsed2 = base::MakeRefCounted<HttpResponseHeaders>(&iter);

  EXPECT_EQ(std::string(test.expected_headers), ToSimpleString(parsed2));
}

const struct PersistData persistence_tests[] = {
    {HttpResponseHeaders::PERSIST_ALL,
     "HTTP/1.1 200 OK\n"
     "Cache-control:private\n"
     "cache-Control:no-store\n",

     "HTTP/1.1 200 OK\n"
     "Cache-control: private\n"
     "cache-Control: no-store\n"},
    {HttpResponseHeaders::PERSIST_SANS_HOP_BY_HOP,
     "HTTP/1.1 200 OK\n"
     "connection: keep-alive\n"
     "server: blah\n",

     "HTTP/1.1 200 OK\n"
     "server: blah\n"},
    {HttpResponseHeaders::PERSIST_SANS_NON_CACHEABLE |
         HttpResponseHeaders::PERSIST_SANS_HOP_BY_HOP,
     "HTTP/1.1 200 OK\n"
     "fOo: 1\n"
     "Foo: 2\n"
     "Transfer-Encoding: chunked\n"
     "CoNnection: keep-alive\n"
     "cache-control: private, no-cache=\"foo\"\n",

     "HTTP/1.1 200 OK\n"
     "cache-control: private, no-cache=\"foo\"\n"},
    {HttpResponseHeaders::PERSIST_SANS_NON_CACHEABLE,
     "HTTP/1.1 200 OK\n"
     "Foo: 2\n"
     "Cache-Control: private,no-cache=\"foo, bar\"\n"
     "bar",

     "HTTP/1.1 200 OK\n"
     "Cache-Control: private,no-cache=\"foo, bar\"\n"},
    // Ignore bogus no-cache value.
    {HttpResponseHeaders::PERSIST_SANS_NON_CACHEABLE,
     "HTTP/1.1 200 OK\n"
     "Foo: 2\n"
     "Cache-Control: private,no-cache=foo\n",

     "HTTP/1.1 200 OK\n"
     "Foo: 2\n"
     "Cache-Control: private,no-cache=foo\n"},
    // Ignore bogus no-cache value.
    {HttpResponseHeaders::PERSIST_SANS_NON_CACHEABLE,
     "HTTP/1.1 200 OK\n"
     "Foo: 2\n"
     "Cache-Control: private, no-cache=\n",

     "HTTP/1.1 200 OK\n"
     "Foo: 2\n"
     "Cache-Control: private, no-cache=\n"},
    // Ignore empty no-cache value.
    {HttpResponseHeaders::PERSIST_SANS_NON_CACHEABLE,
     "HTTP/1.1 200 OK\n"
     "Foo: 2\n"
     "Cache-Control: private, no-cache=\"\"\n",

     "HTTP/1.1 200 OK\n"
     "Foo: 2\n"
     "Cache-Control: private, no-cache=\"\"\n"},
    // Ignore wrong quotes no-cache value.
    {HttpResponseHeaders::PERSIST_SANS_NON_CACHEABLE,
     "HTTP/1.1 200 OK\n"
     "Foo: 2\n"
     "Cache-Control: private, no-cache=\'foo\'\n",

     "HTTP/1.1 200 OK\n"
     "Foo: 2\n"
     "Cache-Control: private, no-cache=\'foo\'\n"},
    // Ignore unterminated quotes no-cache value.
    {HttpResponseHeaders::PERSIST_SANS_NON_CACHEABLE,
     "HTTP/1.1 200 OK\n"
     "Foo: 2\n"
     "Cache-Control: private, no-cache=\"foo\n",

     "HTTP/1.1 200 OK\n"
     "Foo: 2\n"
     "Cache-Control: private, no-cache=\"foo\n"},
    // Accept sloppy LWS.
    {HttpResponseHeaders::PERSIST_SANS_NON_CACHEABLE,
     "HTTP/1.1 200 OK\n"
     "Foo: 2\n"
     "Cache-Control: private, no-cache=\" foo\t, bar\"\n",

     "HTTP/1.1 200 OK\n"
     "Cache-Control: private, no-cache=\" foo\t, bar\"\n"},
    // Header name appears twice, separated by another header.
    {HttpResponseHeaders::PERSIST_ALL,
     "HTTP/1.1 200 OK\n"
     "Foo: 1\n"
     "Bar: 2\n"
     "Foo: 3\n",

     "HTTP/1.1 200 OK\n"
     "Foo: 1\n"
     "Bar: 2\n"
     "Foo: 3\n"},
    // Header name appears twice, separated by another header (type 2).
    {HttpResponseHeaders::PERSIST_ALL,
     "HTTP/1.1 200 OK\n"
     "Foo: 1, 3\n"
     "Bar: 2\n"
     "Foo: 4\n",

     "HTTP/1.1 200 OK\n"
     "Foo: 1, 3\n"
     "Bar: 2\n"
     "Foo: 4\n"},
    // Test filtering of cookie headers.
    {HttpResponseHeaders::PERSIST_SANS_COOKIES,
     "HTTP/1.1 200 OK\n"
     "Set-Cookie: foo=bar; httponly\n"
     "Set-Cookie: bar=foo\n"
     "Bar: 1\n"
     "Set-Cookie2: bar2=foo2\n",

     "HTTP/1.1 200 OK\n"
     "Bar: 1\n"},
    {HttpResponseHeaders::PERSIST_SANS_COOKIES,
     "HTTP/1.1 200 OK\n"
     "Set-Cookie: foo=bar\n"
     "Foo: 2\n"
     "Clear-Site-Data: \"cookies\"\n"
     "Bar: 3\n",

     "HTTP/1.1 200 OK\n"
     "Foo: 2\n"
     "Bar: 3\n"},
    // Test LWS at the end of a header.
    {HttpResponseHeaders::PERSIST_ALL,
     "HTTP/1.1 200 OK\n"
     "Content-Length: 450   \n"
     "Content-Encoding: gzip\n",

     "HTTP/1.1 200 OK\n"
     "Content-Length: 450\n"
     "Content-Encoding: gzip\n"},
    // Test LWS at the end of a header.
    {HttpResponseHeaders::PERSIST_RAW,
     "HTTP/1.1 200 OK\n"
     "Content-Length: 450   \n"
     "Content-Encoding: gzip\n",

     "HTTP/1.1 200 OK\n"
     "Content-Length: 450\n"
     "Content-Encoding: gzip\n"},
    // Test filtering of transport security state headers.
    {HttpResponseHeaders::PERSIST_SANS_SECURITY_STATE,
     "HTTP/1.1 200 OK\n"
     "Strict-Transport-Security: max-age=1576800\n"
     "Bar: 1\n",

     "HTTP/1.1 200 OK\n"
     "Bar: 1\n"},
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         PersistenceTest,
                         testing::ValuesIn(persistence_tests));

TEST(HttpResponseHeadersTest, EnumerateHeader_Coalesced) {
  // Ensure that commas in quoted strings are not regarded as value separators.
  // Ensure that whitespace following a value is trimmed properly.
  std::string headers =
      "HTTP/1.1 200 OK\n"
      "Cache-control:,,private , no-cache=\"set-cookie,server\",\n"
      "cache-Control: no-store\n"
      "cache-Control:\n";
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);

  size_t iter = 0;
  EXPECT_EQ("", parsed->EnumerateHeader(&iter, "cache-control"));
  EXPECT_EQ("", parsed->EnumerateHeader(&iter, "cache-control"));
  EXPECT_EQ("private", parsed->EnumerateHeader(&iter, "cache-control"));
  EXPECT_EQ("no-cache=\"set-cookie,server\"",
            parsed->EnumerateHeader(&iter, "cache-control"));
  EXPECT_EQ("", parsed->EnumerateHeader(&iter, "cache-control"));
  EXPECT_EQ("no-store", parsed->EnumerateHeader(&iter, "cache-control"));
  EXPECT_EQ("", parsed->EnumerateHeader(&iter, "cache-control"));
  EXPECT_FALSE(parsed->EnumerateHeader(&iter, "cache-control"));

  // Test the deprecated overload that returns values as std::strings.
  iter = 0;
  std::string value;
  ASSERT_TRUE(parsed->EnumerateHeader(&iter, "cache-control", &value));
  EXPECT_EQ("", value);
  ASSERT_TRUE(parsed->EnumerateHeader(&iter, "cache-control", &value));
  EXPECT_EQ("", value);
  ASSERT_TRUE(parsed->EnumerateHeader(&iter, "cache-control", &value));
  EXPECT_EQ("private", value);
  ASSERT_TRUE(parsed->EnumerateHeader(&iter, "cache-control", &value));
  EXPECT_EQ("no-cache=\"set-cookie,server\"", value);
  ASSERT_TRUE(parsed->EnumerateHeader(&iter, "cache-control", &value));
  EXPECT_EQ("", value);
  ASSERT_TRUE(parsed->EnumerateHeader(&iter, "cache-control", &value));
  EXPECT_EQ("no-store", value);
  ASSERT_TRUE(parsed->EnumerateHeader(&iter, "cache-control", &value));
  EXPECT_EQ("", value);
  EXPECT_FALSE(parsed->EnumerateHeader(&iter, "cache-control", &value));
}

TEST(HttpResponseHeadersTest, EnumerateHeader_Challenge) {
  // Even though WWW-Authenticate has commas, it should not be treated as
  // coalesced values.
  std::string headers =
      "HTTP/1.1 401 OK\n"
      "WWW-Authenticate:Digest realm=foobar, nonce=x, domain=y\n"
      "WWW-Authenticate:Basic realm=quatar\n";
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);

  size_t iter = 0;
  EXPECT_EQ("Digest realm=foobar, nonce=x, domain=y",
            parsed->EnumerateHeader(&iter, "WWW-Authenticate"));
  EXPECT_EQ("Basic realm=quatar",
            parsed->EnumerateHeader(&iter, "WWW-Authenticate"));
  EXPECT_FALSE(parsed->EnumerateHeader(&iter, "WWW-Authenticate"));

  // Test the deprecated overload that returns values as std::strings.
  iter = 0;
  std::string value;
  EXPECT_TRUE(parsed->EnumerateHeader(&iter, "WWW-Authenticate", &value));
  EXPECT_EQ("Digest realm=foobar, nonce=x, domain=y", value);
  EXPECT_TRUE(parsed->EnumerateHeader(&iter, "WWW-Authenticate", &value));
  EXPECT_EQ("Basic realm=quatar", value);
  EXPECT_FALSE(parsed->EnumerateHeader(&iter, "WWW-Authenticate", &value));
}

TEST(HttpResponseHeadersTest, EnumerateHeader_DateValued) {
  // The comma in a date valued header should not be treated as a
  // field-value separator.
  std::string headers =
      "HTTP/1.1 200 OK\n"
      "Date: Tue, 07 Aug 2007 23:10:55 GMT\n"
      "Last-Modified: Wed, 01 Aug 2007 23:23:45 GMT\n";
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);

  EXPECT_EQ("Tue, 07 Aug 2007 23:10:55 GMT",
            parsed->EnumerateHeader(nullptr, "date"));
  EXPECT_EQ("Wed, 01 Aug 2007 23:23:45 GMT",
            parsed->EnumerateHeader(nullptr, "last-modified"));

  // Test the deprecated overload that returns values as std::strings.
  std::string value;
  EXPECT_TRUE(parsed->EnumerateHeader(nullptr, "date", &value));
  EXPECT_EQ("Tue, 07 Aug 2007 23:10:55 GMT", value);
  EXPECT_TRUE(parsed->EnumerateHeader(nullptr, "last-modified", &value));
  EXPECT_EQ("Wed, 01 Aug 2007 23:23:45 GMT", value);
}

TEST(HttpResponseHeadersTest, DefaultDateToGMT) {
  // Verify we make the best interpretation when parsing dates that incorrectly
  // do not end in "GMT" as RFC2616 requires.
  std::string headers =
      "HTTP/1.1 200 OK\n"
      "Date: Tue, 07 Aug 2007 23:10:55\n"
      "Last-Modified: Tue, 07 Aug 2007 19:10:55 EDT\n"
      "Expires: Tue, 07 Aug 2007 23:10:55 UTC\n";
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);
  base::Time expected_value;
  ASSERT_TRUE(base::Time::FromString("Tue, 07 Aug 2007 23:10:55 GMT",
                                     &expected_value));

  // When the timezone is missing, GMT is a good guess as its what RFC2616
  // requires.
  EXPECT_EQ(expected_value, parsed->GetDateValue());
  // If GMT is missing but an RFC822-conforming one is present, use that.
  EXPECT_EQ(expected_value, parsed->GetLastModifiedValue());
  // If an unknown timezone is present, treat like a missing timezone and
  // default to GMT.  The only example of a web server not specifying "GMT"
  // used "UTC" which is equivalent to GMT.
  EXPECT_THAT(parsed->GetExpiresValue(),
              testing::AnyOf(std::nullopt, expected_value));
}

TEST(HttpResponseHeadersTest, GetAgeValue10) {
  std::string headers =
      "HTTP/1.1 200 OK\n"
      "Age: 10\n";
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);
  EXPECT_EQ(base::Seconds(10), parsed->GetAgeValue());
}

TEST(HttpResponseHeadersTest, GetAgeValue0) {
  std::string headers =
      "HTTP/1.1 200 OK\n"
      "Age: 0\n";
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);
  EXPECT_EQ(base::TimeDelta(), parsed->GetAgeValue());
}

TEST(HttpResponseHeadersTest, GetAgeValueBogus) {
  std::string headers =
      "HTTP/1.1 200 OK\n"
      "Age: donkey\n";
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);
  EXPECT_FALSE(parsed->GetAgeValue());
}

TEST(HttpResponseHeadersTest, GetAgeValueNegative) {
  std::string headers =
      "HTTP/1.1 200 OK\n"
      "Age: -10\n";
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);
  EXPECT_FALSE(parsed->GetAgeValue());
}

TEST(HttpResponseHeadersTest, GetAgeValueLeadingPlus) {
  std::string headers =
      "HTTP/1.1 200 OK\n"
      "Age: +10\n";
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);
  EXPECT_FALSE(parsed->GetAgeValue());
}

TEST(HttpResponseHeadersTest, GetAgeValueOverflow) {
  std::string headers =
      "HTTP/1.1 200 OK\n"
      "Age: 999999999999999999999999999999999999999999\n";
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);

  // Should have saturated to 2^32 - 1.
  EXPECT_EQ(base::Seconds(static_cast<int64_t>(0xFFFFFFFFL)),
            parsed->GetAgeValue());
}

struct ContentTypeTestData {
  const std::string raw_headers;
  const std::string mime_type;
  const bool has_mimetype;
  const std::string charset;
  const bool has_charset;
  const std::string all_content_type;
};

class ContentTypeTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<ContentTypeTestData> {
};

TEST_P(ContentTypeTest, GetMimeType) {
  const ContentTypeTestData test = GetParam();

  std::string headers(test.raw_headers);
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);

  std::string value;
  EXPECT_EQ(test.has_mimetype, parsed->GetMimeType(&value));
  EXPECT_EQ(test.mime_type, value);
  value.clear();
  EXPECT_EQ(test.has_charset, parsed->GetCharset(&value));
  EXPECT_EQ(test.charset, value);
  EXPECT_EQ(parsed->GetNormalizedHeader("content-type"), test.all_content_type);
}

// clang-format off
const ContentTypeTestData mimetype_tests[] = {
  { "HTTP/1.1 200 OK\n"
    "Content-type: text/html\n",
    "text/html", true,
    "", false,
    "text/html" },
  // Multiple content-type headers should give us the last one.
  { "HTTP/1.1 200 OK\n"
    "Content-type: text/html\n"
    "Content-type: text/html\n",
    "text/html", true,
    "", false,
    "text/html, text/html" },
  { "HTTP/1.1 200 OK\n"
    "Content-type: text/plain\n"
    "Content-type: text/html\n"
    "Content-type: text/plain\n"
    "Content-type: text/html\n",
    "text/html", true,
    "", false,
    "text/plain, text/html, text/plain, text/html" },
  // Test charset parsing.
  { "HTTP/1.1 200 OK\n"
    "Content-type: text/html\n"
    "Content-type: text/html; charset=ISO-8859-1\n",
    "text/html", true,
    "iso-8859-1", true,
    "text/html, text/html; charset=ISO-8859-1" },
  // Test charset in double quotes.
  { "HTTP/1.1 200 OK\n"
    "Content-type: text/html\n"
    "Content-type: text/html; charset=\"ISO-8859-1\"\n",
    "text/html", true,
    "iso-8859-1", true,
    "text/html, text/html; charset=\"ISO-8859-1\"" },
  // If there are multiple matching content-type headers, we carry
  // over the charset value.
  { "HTTP/1.1 200 OK\n"
    "Content-type: text/html;charset=utf-8\n"
    "Content-type: text/html\n",
    "text/html", true,
    "utf-8", true,
    "text/html;charset=utf-8, text/html" },
  // Regression test for https://crbug.com/772350:
  // Single quotes are not delimiters but must be treated as part of charset.
  { "HTTP/1.1 200 OK\n"
    "Content-type: text/html;charset='utf-8'\n"
    "Content-type: text/html\n",
    "text/html", true,
    "'utf-8'", true,
    "text/html;charset='utf-8', text/html" },
  // First charset wins if matching content-type.
  { "HTTP/1.1 200 OK\n"
    "Content-type: text/html;charset=utf-8\n"
    "Content-type: text/html;charset=iso-8859-1\n",
    "text/html", true,
    "iso-8859-1", true,
    "text/html;charset=utf-8, text/html;charset=iso-8859-1" },
  // Charset is ignored if the content types change.
  { "HTTP/1.1 200 OK\n"
    "Content-type: text/plain;charset=utf-8\n"
    "Content-type: text/html\n",
    "text/html", true,
    "", false,
    "text/plain;charset=utf-8, text/html" },
  // Empty content-type.
  { "HTTP/1.1 200 OK\n"
    "Content-type: \n",
    "", false,
    "", false,
    "" },
  // Emtpy charset.
  { "HTTP/1.1 200 OK\n"
    "Content-type: text/html;charset=\n",
    "text/html", true,
    "", false,
    "text/html;charset=" },
  // Multiple charsets, first one wins.
  { "HTTP/1.1 200 OK\n"
    "Content-type: text/html;charset=utf-8; charset=iso-8859-1\n",
    "text/html", true,
    "utf-8", true,
    "text/html;charset=utf-8; charset=iso-8859-1" },
  // Multiple params.
  { "HTTP/1.1 200 OK\n"
    "Content-type: text/html; foo=utf-8; charset=iso-8859-1\n",
    "text/html", true,
    "iso-8859-1", true,
    "text/html; foo=utf-8; charset=iso-8859-1" },
  { "HTTP/1.1 200 OK\n"
    "Content-type: text/html ; charset=utf-8 ; bar=iso-8859-1\n",
    "text/html", true,
    "utf-8", true,
    "text/html ; charset=utf-8 ; bar=iso-8859-1" },
  // Comma embeded in quotes.
  { "HTTP/1.1 200 OK\n"
    "Content-type: text/html ; charset=\"utf-8,text/plain\" ;\n",
    "text/html", true,
    "utf-8,text/plain", true,
    "text/html ; charset=\"utf-8,text/plain\" ;" },
  // Charset with leading spaces.
  { "HTTP/1.1 200 OK\n"
    "Content-type: text/html ; charset= \"utf-8\" ;\n",
    "text/html", true,
    "utf-8", true,
    "text/html ; charset= \"utf-8\" ;" },
  // Media type comments in mime-type.
  { "HTTP/1.1 200 OK\n"
    "Content-type: text/html (html)\n",
    "text/html", true,
    "", false,
   "text/html (html)" },
  // Incomplete charset= param.
  { "HTTP/1.1 200 OK\n"
    "Content-type: text/html; char=\n",
    "text/html", true,
    "", false,
    "text/html; char=" },
  // Invalid media type: no slash.
  { "HTTP/1.1 200 OK\n"
    "Content-type: texthtml\n",
    "", false,
    "", false,
    "texthtml" },
  // Invalid media type: "*/*".
  { "HTTP/1.1 200 OK\n"
    "Content-type: */*\n",
    "", false,
    "", false,
    "*/*" },
};
// clang-format on

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         ContentTypeTest,
                         testing::ValuesIn(mimetype_tests));

struct RequiresValidationTestData {
  const char* headers;
  ValidationType validation_type;
};

class RequiresValidationTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<RequiresValidationTestData> {
};

TEST_P(RequiresValidationTest, RequiresValidation) {
  const RequiresValidationTestData test = GetParam();

  base::Time request_time, response_time, current_time;
  ASSERT_TRUE(
      base::Time::FromString("Wed, 28 Nov 2007 00:40:09 GMT", &request_time));
  ASSERT_TRUE(
      base::Time::FromString("Wed, 28 Nov 2007 00:40:12 GMT", &response_time));
  ASSERT_TRUE(
      base::Time::FromString("Wed, 28 Nov 2007 00:45:20 GMT", &current_time));

  std::string headers(test.headers);
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);

  ValidationType validation_type =
      parsed->RequiresValidation(request_time, response_time, current_time);
  EXPECT_EQ(test.validation_type, validation_type);
}

const struct RequiresValidationTestData requires_validation_tests[] = {
    // No expiry info: expires immediately.
    {"HTTP/1.1 200 OK\n"
     "\n",
     VALIDATION_SYNCHRONOUS},
    // No expiry info: expires immediately.
    {"HTTP/1.1 200 OK\n"
     "\n",
     VALIDATION_SYNCHRONOUS},
    // Valid for a little while.
    {"HTTP/1.1 200 OK\n"
     "cache-control: max-age=10000\n"
     "\n",
     VALIDATION_NONE},
    // Expires in the future.
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "expires: Wed, 28 Nov 2007 01:00:00 GMT\n"
     "\n",
     VALIDATION_NONE},
    // Already expired.
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "expires: Wed, 28 Nov 2007 00:00:00 GMT\n"
     "\n",
     VALIDATION_SYNCHRONOUS},
    // Max-age trumps expires.
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "expires: Wed, 28 Nov 2007 00:00:00 GMT\n"
     "cache-control: max-age=10000\n"
     "\n",
     VALIDATION_NONE},
    // Last-modified heuristic: modified a while ago.
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "last-modified: Wed, 27 Nov 2007 08:00:00 GMT\n"
     "\n",
     VALIDATION_NONE},
    {"HTTP/1.1 203 Non-Authoritative Information\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "last-modified: Wed, 27 Nov 2007 08:00:00 GMT\n"
     "\n",
     VALIDATION_NONE},
    {"HTTP/1.1 206 Partial Content\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "last-modified: Wed, 27 Nov 2007 08:00:00 GMT\n"
     "\n",
     VALIDATION_NONE},
    // Last-modified heuristic: modified a while ago and it's VALIDATION_NONE
    // (fresh) like above but VALIDATION_SYNCHRONOUS if expires header value is
    // "0".
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "last-modified: Tue, 27 Nov 2007 08:00:00 GMT\n"
     "expires: 0\n"
     "\n",
     VALIDATION_SYNCHRONOUS},
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "last-modified: Tue, 27 Nov 2007 08:00:00 GMT\n"
     "expires:  0 \n"
     "\n",
     VALIDATION_SYNCHRONOUS},
    // The cache is fresh if the expires header value is an invalid date string
    // except for "0"
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "last-modified: Tue, 27 Nov 2007 08:00:00 GMT\n"
     "expires: banana \n"
     "\n",
     VALIDATION_NONE},
    // Last-modified heuristic: modified recently.
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "last-modified: Wed, 28 Nov 2007 00:40:10 GMT\n"
     "\n",
     VALIDATION_SYNCHRONOUS},
    {"HTTP/1.1 203 Non-Authoritative Information\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "last-modified: Wed, 28 Nov 2007 00:40:10 GMT\n"
     "\n",
     VALIDATION_SYNCHRONOUS},
    {"HTTP/1.1 206 Partial Content\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "last-modified: Wed, 28 Nov 2007 00:40:10 GMT\n"
     "\n",
     VALIDATION_SYNCHRONOUS},
    // Cached permanent redirect.
    {"HTTP/1.1 301 Moved Permanently\n"
     "\n",
     VALIDATION_NONE},
    // Another cached permanent redirect.
    {"HTTP/1.1 308 Permanent Redirect\n"
     "\n",
     VALIDATION_NONE},
    // Cached redirect: not reusable even though by default it would be.
    {"HTTP/1.1 300 Multiple Choices\n"
     "Cache-Control: no-cache\n"
     "\n",
     VALIDATION_SYNCHRONOUS},
    // Cached forever by default.
    {"HTTP/1.1 410 Gone\n"
     "\n",
     VALIDATION_NONE},
    // Cached temporary redirect: not reusable.
    {"HTTP/1.1 302 Found\n"
     "\n",
     VALIDATION_SYNCHRONOUS},
    // Cached temporary redirect: reusable.
    {"HTTP/1.1 302 Found\n"
     "cache-control: max-age=10000\n"
     "\n",
     VALIDATION_NONE},
    // Cache-control: max-age=N overrides expires: date in the past.
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "expires: Wed, 28 Nov 2007 00:20:11 GMT\n"
     "cache-control: max-age=10000\n"
     "\n",
     VALIDATION_NONE},
    // Cache-control: no-store overrides expires: in the future.
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "expires: Wed, 29 Nov 2007 00:40:11 GMT\n"
     "cache-control: no-store,private,no-cache=\"foo\"\n"
     "\n",
     VALIDATION_SYNCHRONOUS},
    // Pragma: no-cache overrides last-modified heuristic.
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "last-modified: Wed, 27 Nov 2007 08:00:00 GMT\n"
     "pragma: no-cache\n"
     "\n",
     VALIDATION_SYNCHRONOUS},
    // max-age has expired, needs synchronous revalidation
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "cache-control: max-age=300\n"
     "\n",
     VALIDATION_SYNCHRONOUS},
    // max-age has expired, stale-while-revalidate has not, eligible for
    // asynchronous revalidation
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "cache-control: max-age=300, stale-while-revalidate=3600\n"
     "\n",
     VALIDATION_ASYNCHRONOUS},
    // max-age and stale-while-revalidate have expired, needs synchronous
    // revalidation
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "cache-control: max-age=300, stale-while-revalidate=5\n"
     "\n",
     VALIDATION_SYNCHRONOUS},
    // max-age is 0, stale-while-revalidate is large enough to permit
    // asynchronous revalidation
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "cache-control: max-age=0, stale-while-revalidate=360\n"
     "\n",
     VALIDATION_ASYNCHRONOUS},
    // stale-while-revalidate must not override no-cache or similar directives.
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "cache-control: no-cache, stale-while-revalidate=360\n"
     "\n",
     VALIDATION_SYNCHRONOUS},
    // max-age has not expired, so no revalidation is needed.
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "cache-control: max-age=3600, stale-while-revalidate=3600\n"
     "\n",
     VALIDATION_NONE},
    // must-revalidate overrides stale-while-revalidate, so synchronous
    // validation
    // is needed.
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "cache-control: must-revalidate, max-age=300, "
     "stale-while-revalidate=3600\n"
     "\n",
     VALIDATION_SYNCHRONOUS},
    // must-revalidate overrides stale-while-revalidate, so synchronous
    // validation is needed.
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "cache-control: must-revalidate, max-age=300, "
     "stale-while-revalidate=3600\n"
     "\n",
     VALIDATION_SYNCHRONOUS},

    // must-revalidate overrides stale-while-revalidate even when they appear
    // in reverse order in the header
    {"HTTP/1.1 200 OK\n"
     "date: Wed, 28 Nov 2007 00:40:11 GMT\n"
     "cache-control: stale-while-revalidate=30, must-revalidate\n"
     "\n",
     VALIDATION_SYNCHRONOUS},
    // TODO(darin): Add many many more tests here.
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         RequiresValidationTest,
                         testing::ValuesIn(requires_validation_tests));

struct UpdateTestData {
  const char* orig_headers;
  const char* new_headers;
  const char* expected_headers;
};

class UpdateTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<UpdateTestData> {
};

TEST_P(UpdateTest, Update) {
  const UpdateTestData test = GetParam();

  std::string orig_headers(test.orig_headers);
  HeadersToRaw(&orig_headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(orig_headers);

  std::string new_headers(test.new_headers);
  HeadersToRaw(&new_headers);
  auto new_parsed = base::MakeRefCounted<HttpResponseHeaders>(new_headers);

  parsed->Update(*new_parsed.get());

  EXPECT_EQ(std::string(test.expected_headers), ToSimpleString(parsed));
}

const UpdateTestData update_tests[] = {
    {"HTTP/1.1 200 OK\n",

     "HTTP/1/1 304 Not Modified\n"
     "connection: keep-alive\n"
     "Cache-control: max-age=10000\n",

     "HTTP/1.1 200 OK\n"
     "Cache-control: max-age=10000\n"},
    {"HTTP/1.1 200 OK\n"
     "Foo: 1\n"
     "Cache-control: private\n",

     "HTTP/1/1 304 Not Modified\n"
     "connection: keep-alive\n"
     "Cache-control: max-age=10000\n",

     "HTTP/1.1 200 OK\n"
     "Cache-control: max-age=10000\n"
     "Foo: 1\n"},
    {"HTTP/1.1 200 OK\n"
     "Foo: 1\n"
     "Cache-control: private\n",

     "HTTP/1/1 304 Not Modified\n"
     "connection: keep-alive\n"
     "Cache-CONTROL: max-age=10000\n",

     "HTTP/1.1 200 OK\n"
     "Cache-CONTROL: max-age=10000\n"
     "Foo: 1\n"},
    {"HTTP/1.1 200 OK\n"
     "Content-Length: 450\n",

     "HTTP/1/1 304 Not Modified\n"
     "connection: keep-alive\n"
     "Cache-control:      max-age=10001   \n",

     "HTTP/1.1 200 OK\n"
     "Cache-control: max-age=10001\n"
     "Content-Length: 450\n"},
    {
        "HTTP/1.1 200 OK\n"
        "X-Frame-Options: DENY\n",

        "HTTP/1/1 304 Not Modified\n"
        "X-Frame-Options: ALLOW\n",

        "HTTP/1.1 200 OK\n"
        "X-Frame-Options: DENY\n",
    },
    {
        "HTTP/1.1 200 OK\n"
        "X-WebKit-CSP: default-src 'none'\n",

        "HTTP/1/1 304 Not Modified\n"
        "X-WebKit-CSP: default-src *\n",

        "HTTP/1.1 200 OK\n"
        "X-WebKit-CSP: default-src 'none'\n",
    },
    {
        "HTTP/1.1 200 OK\n"
        "X-XSS-Protection: 1\n",

        "HTTP/1/1 304 Not Modified\n"
        "X-XSS-Protection: 0\n",

        "HTTP/1.1 200 OK\n"
        "X-XSS-Protection: 1\n",
    },
    {"HTTP/1.1 200 OK\n",

     "HTTP/1/1 304 Not Modified\n"
     "X-Content-Type-Options: nosniff\n",

     "HTTP/1.1 200 OK\n"},
    {"HTTP/1.1 200 OK\n"
     "Content-Encoding: identity\n"
     "Content-Length: 100\n"
     "Content-Type: text/html\n"
     "Content-Security-Policy: default-src 'none'\n",

     "HTTP/1/1 304 Not Modified\n"
     "Content-Encoding: gzip\n"
     "Content-Length: 200\n"
     "Content-Type: text/xml\n"
     "Content-Security-Policy: default-src 'self'\n",

     "HTTP/1.1 200 OK\n"
     "Content-Security-Policy: default-src 'self'\n"
     "Content-Encoding: identity\n"
     "Content-Length: 100\n"
     "Content-Type: text/html\n"},
    {"HTTP/1.1 200 OK\n"
     "Content-Location: /example_page.html\n",

     "HTTP/1/1 304 Not Modified\n"
     "Content-Location: /not_example_page.html\n",

     "HTTP/1.1 200 OK\n"
     "Content-Location: /example_page.html\n"},
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         UpdateTest,
                         testing::ValuesIn(update_tests));

struct EnumerateHeaderTestData {
  const char* headers;
  const char* expected_lines;
};

class EnumerateHeaderLinesTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<EnumerateHeaderTestData> {
};

TEST_P(EnumerateHeaderLinesTest, EnumerateHeaderLines) {
  const EnumerateHeaderTestData test = GetParam();

  std::string headers(test.headers);
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);

  std::string name, value, lines;

  size_t iter = 0;
  while (parsed->EnumerateHeaderLines(&iter, &name, &value)) {
    lines.append(name);
    lines.append(": ");
    lines.append(value);
    lines.append("\n");
  }

  EXPECT_EQ(std::string(test.expected_lines), lines);
}

const EnumerateHeaderTestData enumerate_header_tests[] = {
    {"HTTP/1.1 200 OK\n",

     ""},
    {"HTTP/1.1 200 OK\n"
     "Foo: 1\n",

     "Foo: 1\n"},
    {"HTTP/1.1 200 OK\n"
     "Foo: 1\n"
     "Bar: 2\n"
     "Foo: 3\n",

     "Foo: 1\nBar: 2\nFoo: 3\n"},
    {"HTTP/1.1 200 OK\n"
     "Foo: 1, 2, 3\n",

     "Foo: 1, 2, 3\n"},
    {"HTTP/1.1 200 OK\n"
     "Foo: ,, 1,, 2, 3,, \n",

     "Foo: ,, 1,, 2, 3,,\n"},
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         EnumerateHeaderLinesTest,
                         testing::ValuesIn(enumerate_header_tests));

struct IsRedirectTestData {
  const char* headers;
  const char* location;
  bool is_redirect;
};

class IsRedirectTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<IsRedirectTestData> {
};

TEST_P(IsRedirectTest, IsRedirect) {
  const IsRedirectTestData test = GetParam();

  std::string headers(test.headers);
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);

  std::string location;
  EXPECT_EQ(parsed->IsRedirect(&location), test.is_redirect);
  EXPECT_EQ(location, test.location);
}

const IsRedirectTestData is_redirect_tests[] = {
  { "HTTP/1.1 200 OK\n",
    "",
    false
  },
  { "HTTP/1.1 301 Moved\n"
    "Location: http://foopy/\n",
    "http://foopy/",
    true
  },
  { "HTTP/1.1 301 Moved\n"
    "Location: \t \n",
    "",
    false
  },
  // We use the first location header as the target of the redirect.
  { "HTTP/1.1 301 Moved\n"
    "Location: http://foo/\n"
    "Location: http://bar/\n",
    "http://foo/",
    true
  },
  // We use the first _valid_ location header as the target of the redirect.
  { "HTTP/1.1 301 Moved\n"
    "Location: \n"
    "Location: http://bar/\n",
    "http://bar/",
    true
  },
  // Bug 1050541 (location header with an unescaped comma).
  { "HTTP/1.1 301 Moved\n"
    "Location: http://foo/bar,baz.html\n",
    "http://foo/bar,baz.html",
    true
  },
  // Bug 1224617 (location header with non-ASCII bytes).
  { "HTTP/1.1 301 Moved\n"
    "Location: http://foo/bar?key=\xE4\xF6\xFC\n",
    "http://foo/bar?key=%E4%F6%FC",
    true
  },
  // Shift_JIS, Big5, and GBK contain multibyte characters with the trailing
  // byte falling in the ASCII range.
  { "HTTP/1.1 301 Moved\n"
    "Location: http://foo/bar?key=\x81\x5E\xD8\xBF\n",
    "http://foo/bar?key=%81^%D8%BF",
    true
  },
  { "HTTP/1.1 301 Moved\n"
    "Location: http://foo/bar?key=\x82\x40\xBD\xC4\n",
    "http://foo/bar?key=%82@%BD%C4",
    true
  },
  { "HTTP/1.1 301 Moved\n"
    "Location: http://foo/bar?key=\x83\x5C\x82\x5D\xCB\xD7\n",
    "http://foo/bar?key=%83\\%82]%CB%D7",
    true
  },
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         IsRedirectTest,
                         testing::ValuesIn(is_redirect_tests));

struct HasStorageAccessRetryTestData {
  const char* headers;
  std::optional<std::string> expected_origin;

  bool want_result;
};

class HasStorageAccessRetryTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<HasStorageAccessRetryTestData> {};

TEST_P(HasStorageAccessRetryTest, HasStorageAccessRetry) {
  const HasStorageAccessRetryTestData test = GetParam();

  std::string headers(test.headers);
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);

  EXPECT_EQ(parsed->HasStorageAccessRetryHeader(
                base::OptionalToPtr(test.expected_origin)),
            test.want_result);
}

const HasStorageAccessRetryTestData has_storage_access_retry_tests[] = {
    // No expected initiator; explicit allowlist.
    {"HTTP/1.1 200 OK\n"
     R"(Activate-Storage-Access: retry; allowed-origin="https://example.com:123")"
     "\n",
     std::nullopt, false},
    // No expected initiator; wildcard allowlist matches anyway, since the
    // server says anything goes.
    {"HTTP/1.1 200 OK\n"
     R"(Activate-Storage-Access: retry; allowed-origin=*)"
     "\n",
     std::nullopt, true},
    // No allowlist, no expected initiator.
    {"HTTP/1.1 200 OK\n"
     "Activate-Storage-Access: retry\n",
     std::nullopt, false},
    // No allowlist.
    {"HTTP/1.1 200 OK\n"
     "Activate-Storage-Access: retry\n",
     "https://example.com", false},
    // Invalid structured header.
    {"HTTP/1.1 200 OK\n"
     R"(Activate-Storage-Access: retry, allowed-origin:"https://example.com:123")"
     "\n",
     "https://example.com:123", false},
    // Unknown parameter.
    {"HTTP/1.1 200 OK\n"
     R"(Activate-Storage-Access: retry; frobnify="https://example.com:123")"
     "\n",
     "https://example.com:123", false},
    // allowed-origin parameter present along with unrecognized parameter.
    {"HTTP/1.1 200 OK\n"
     R"(Activate-Storage-Access: retry; frobnify=*;)"
     R"( allowed-origin="https://example.com:123")"
     "\n",
     "https://example.com:123", true},
    // Allowlist and expected initiator match.
    {"HTTP/1.1 200 OK\n"
     R"(Activate-Storage-Access: retry; allowed-origin="https://example.com:123")"
     "\n",
     "https://example.com:123", true},
    // Allowlist and expected initiator mismatch.
    {"HTTP/1.1 200 OK\n"
     R"(Activate-Storage-Access: retry; allowed-origin="https://example.com")"
     "\n",
     "https://example.com:123", false},
    // This is a list, not an item, so it is ignored.
    {"HTTP/1.1 200 OK\n"
     R"(Activate-Storage-Access: foo, retry; allowed-origin=*, bar)"
     "\n",
     "https://example.com", false},
    // This is a list (supplied in multiple field lines), not an item, so it is
    // ignored.
    {"HTTP/1.1 200 OK\n"
     "Activate-Storage-Access: foo\n"
     "Activate-Storage-Access: retry; allowed-origin=*, bar\n",
     "https://example.com", false},
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         HasStorageAccessRetryTest,
                         testing::ValuesIn(has_storage_access_retry_tests));

struct ContentLengthTestData {
  const char* headers;
  int64_t expected_len;
};

class GetContentLengthTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<ContentLengthTestData> {
};

TEST_P(GetContentLengthTest, GetContentLength) {
  const ContentLengthTestData test = GetParam();

  std::string headers(test.headers);
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);

  EXPECT_EQ(test.expected_len, parsed->GetContentLength());
}

const ContentLengthTestData content_length_tests[] = {
    {"HTTP/1.1 200 OK\n", -1},
    {"HTTP/1.1 200 OK\n"
     "Content-Length: 10\n",
     10},
    {"HTTP/1.1 200 OK\n"
     "Content-Length: \n",
     -1},
    {"HTTP/1.1 200 OK\n"
     "Content-Length: abc\n",
     -1},
    {"HTTP/1.1 200 OK\n"
     "Content-Length: -10\n",
     -1},
    {"HTTP/1.1 200 OK\n"
     "Content-Length:  +10\n",
     -1},
    {"HTTP/1.1 200 OK\n"
     "Content-Length: 23xb5\n",
     -1},
    {"HTTP/1.1 200 OK\n"
     "Content-Length: 0xA\n",
     -1},
    {"HTTP/1.1 200 OK\n"
     "Content-Length: 010\n",
     10},
    // Content-Length too big, will overflow an int64_t.
    {"HTTP/1.1 200 OK\n"
     "Content-Length: 40000000000000000000\n",
     -1},
    {"HTTP/1.1 200 OK\n"
     "Content-Length:       10\n",
     10},
    {"HTTP/1.1 200 OK\n"
     "Content-Length: 10  \n",
     10},
    {"HTTP/1.1 200 OK\n"
     "Content-Length: \t10\n",
     10},
    {"HTTP/1.1 200 OK\n"
     "Content-Length: \v10\n",
     -1},
    {"HTTP/1.1 200 OK\n"
     "Content-Length: \f10\n",
     -1},
    {"HTTP/1.1 200 OK\n"
     "cOnTeNt-LENgth: 33\n",
     33},
    {"HTTP/1.1 200 OK\n"
     "Content-Length: 34\r\n",
     -1},
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         GetContentLengthTest,
                         testing::ValuesIn(content_length_tests));

struct ContentRangeTestData {
  const char* headers;
  bool expected_return_value;
  int64_t expected_first_byte_position;
  int64_t expected_last_byte_position;
  int64_t expected_instance_size;
};

class ContentRangeTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<ContentRangeTestData> {
};

TEST_P(ContentRangeTest, GetContentRangeFor206) {
  const ContentRangeTestData test = GetParam();

  std::string headers(test.headers);
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);

  int64_t first_byte_position;
  int64_t last_byte_position;
  int64_t instance_size;
  bool return_value = parsed->GetContentRangeFor206(
      &first_byte_position, &last_byte_position, &instance_size);
  EXPECT_EQ(test.expected_return_value, return_value);
  EXPECT_EQ(test.expected_first_byte_position, first_byte_position);
  EXPECT_EQ(test.expected_last_byte_position, last_byte_position);
  EXPECT_EQ(test.expected_instance_size, instance_size);
}

const ContentRangeTestData content_range_tests[] = {
    {"HTTP/1.1 206 Partial Content", false, -1, -1, -1},
    {"HTTP/1.1 206 Partial Content\n"
     "Content-Range:",
     false, -1, -1, -1},
    {"HTTP/1.1 206 Partial Content\n"
     "Content-Range: bytes 0-50/51",
     true, 0, 50, 51},
    {"HTTP/1.1 206 Partial Content\n"
     "Content-Range: bytes 50-0/51",
     false, -1, -1, -1},
    {"HTTP/1.1 416 Requested range not satisfiable\n"
     "Content-Range: bytes */*",
     false, -1, -1, -1},
    {"HTTP/1.1 206 Partial Content\n"
     "Content-Range: bytes 0-50/*",
     false, -1, -1, -1},
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         ContentRangeTest,
                         testing::ValuesIn(content_range_tests));

struct KeepAliveTestData {
  const char* headers;
  bool expected_keep_alive;
};

// Enable GTest to print KeepAliveTestData in an intelligible way if the test
// fails.
void PrintTo(const KeepAliveTestData& keep_alive_test_data,
             std::ostream* os) {
  *os << "{\"" << keep_alive_test_data.headers << "\", " << std::boolalpha
      << keep_alive_test_data.expected_keep_alive << "}";
}

class IsKeepAliveTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<KeepAliveTestData> {
};

TEST_P(IsKeepAliveTest, IsKeepAlive) {
  const KeepAliveTestData test = GetParam();

  std::string headers(test.headers);
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);

  EXPECT_EQ(test.expected_keep_alive, parsed->IsKeepAlive());
}

const KeepAliveTestData keepalive_tests[] = {
  // The status line fabricated by HttpNetworkTransaction for a 0.9 response.
  // Treated as 0.9.
  { "HTTP/0.9 200 OK",
    false
  },
  // This could come from a broken server.  Treated as 1.0 because it has a
  // header.
  { "HTTP/0.9 200 OK\n"
    "connection: keep-alive\n",
    true
  },
  { "HTTP/1.1 200 OK\n",
    true
  },
  { "HTTP/1.0 200 OK\n",
    false
  },
  { "HTTP/1.0 200 OK\n"
    "connection: close\n",
    false
  },
  { "HTTP/1.0 200 OK\n"
    "connection: keep-alive\n",
    true
  },
  { "HTTP/1.0 200 OK\n"
    "connection: kEeP-AliVe\n",
    true
  },
  { "HTTP/1.0 200 OK\n"
    "connection: keep-aliveX\n",
    false
  },
  { "HTTP/1.1 200 OK\n"
    "connection: close\n",
    false
  },
  { "HTTP/1.1 200 OK\n"
    "connection: keep-alive\n",
    true
  },
  { "HTTP/1.0 200 OK\n"
    "proxy-connection: close\n",
    false
  },
  { "HTTP/1.0 200 OK\n"
    "proxy-connection: keep-alive\n",
    true
  },
  { "HTTP/1.1 200 OK\n"
    "proxy-connection: close\n",
    false
  },
  { "HTTP/1.1 200 OK\n"
    "proxy-connection: keep-alive\n",
    true
  },
  { "HTTP/1.1 200 OK\n"
    "Connection: Upgrade, close\n",
    false
  },
  { "HTTP/1.1 200 OK\n"
    "Connection: Upgrade, keep-alive\n",
    true
  },
  { "HTTP/1.1 200 OK\n"
    "Connection: Upgrade\n"
    "Connection: close\n",
    false
  },
  { "HTTP/1.1 200 OK\n"
    "Connection: Upgrade\n"
    "Connection: keep-alive\n",
    true
  },
  { "HTTP/1.1 200 OK\n"
    "Connection: close, Upgrade\n",
    false
  },
  { "HTTP/1.1 200 OK\n"
    "Connection: keep-alive, Upgrade\n",
    true
  },
  { "HTTP/1.1 200 OK\n"
    "Connection: Upgrade\n"
    "Proxy-Connection: close\n",
    false
  },
  { "HTTP/1.1 200 OK\n"
    "Connection: Upgrade\n"
    "Proxy-Connection: keep-alive\n",
    true
  },
  // In situations where the response headers conflict with themselves, use the
  // first one for backwards-compatibility.
  { "HTTP/1.1 200 OK\n"
    "Connection: close\n"
    "Connection: keep-alive\n",
    false
  },
  { "HTTP/1.1 200 OK\n"
    "Connection: keep-alive\n"
    "Connection: close\n",
    true
  },
  { "HTTP/1.0 200 OK\n"
    "Connection: close\n"
    "Connection: keep-alive\n",
    false
  },
  { "HTTP/1.0 200 OK\n"
    "Connection: keep-alive\n"
    "Connection: close\n",
    true
  },
  // Ignore the Proxy-Connection header if at all possible.
  { "HTTP/1.0 200 OK\n"
    "Proxy-Connection: keep-alive\n"
    "Connection: close\n",
    false
  },
  { "HTTP/1.1 200 OK\n"
    "Proxy-Connection: close\n"
    "Connection: keep-alive\n",
    true
  },
  // Older versions of Chrome would have ignored Proxy-Connection in this case,
  // but it doesn't seem safe.
  { "HTTP/1.1 200 OK\n"
    "Proxy-Connection: close\n"
    "Connection: Transfer-Encoding\n",
    false
  },
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         IsKeepAliveTest,
                         testing::ValuesIn(keepalive_tests));

struct HasStrongValidatorsTestData {
  const char* headers;
  bool expected_result;
};

class HasStrongValidatorsTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<HasStrongValidatorsTestData> {
};

TEST_P(HasStrongValidatorsTest, HasStrongValidators) {
  const HasStrongValidatorsTestData test = GetParam();

  std::string headers(test.headers);
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);

  EXPECT_EQ(test.expected_result, parsed->HasStrongValidators());
  // Having string validators implies having validators.
  if (parsed->HasStrongValidators()) {
    EXPECT_TRUE(parsed->HasValidators());
  }
}

const HasStrongValidatorsTestData strong_validators_tests[] = {
  { "HTTP/0.9 200 OK",
    false
  },
  { "HTTP/1.0 200 OK\n"
    "Date: Wed, 28 Nov 2007 01:40:10 GMT\n"
    "Last-Modified: Wed, 28 Nov 2007 00:40:10 GMT\n"
    "ETag: \"foo\"\n",
    false
  },
  { "HTTP/1.1 200 OK\n"
    "Date: Wed, 28 Nov 2007 01:40:10 GMT\n"
    "Last-Modified: Wed, 28 Nov 2007 00:40:10 GMT\n"
    "ETag: \"foo\"\n",
    true
  },
  { "HTTP/1.1 200 OK\n"
    "Date: Wed, 28 Nov 2007 00:41:10 GMT\n"
    "Last-Modified: Wed, 28 Nov 2007 00:40:10 GMT\n",
    true
  },
  { "HTTP/1.1 200 OK\n"
    "Date: Wed, 28 Nov 2007 00:41:09 GMT\n"
    "Last-Modified: Wed, 28 Nov 2007 00:40:10 GMT\n",
    false
  },
  { "HTTP/1.1 200 OK\n"
    "ETag: \"foo\"\n",
    true
  },
  // This is not really a weak etag:
  { "HTTP/1.1 200 OK\n"
    "etag: \"w/foo\"\n",
    true
  },
  // This is a weak etag:
  { "HTTP/1.1 200 OK\n"
    "etag: w/\"foo\"\n",
    false
  },
  { "HTTP/1.1 200 OK\n"
    "etag:    W  /   \"foo\"\n",
    false
  }
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         HasStrongValidatorsTest,
                         testing::ValuesIn(strong_validators_tests));

TEST(HttpResponseHeadersTest, HasValidatorsNone) {
  std::string headers("HTTP/1.1 200 OK");
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);
  EXPECT_FALSE(parsed->HasValidators());
}

TEST(HttpResponseHeadersTest, HasValidatorsEtag) {
  std::string headers(
      "HTTP/1.1 200 OK\n"
      "etag: \"anything\"");
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);
  EXPECT_TRUE(parsed->HasValidators());
}

TEST(HttpResponseHeadersTest, HasValidatorsLastModified) {
  std::string headers(
      "HTTP/1.1 200 OK\n"
      "Last-Modified: Wed, 28 Nov 2007 00:40:10 GMT");
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);
  EXPECT_TRUE(parsed->HasValidators());
}

TEST(HttpResponseHeadersTest, HasValidatorsWeakEtag) {
  std::string headers(
      "HTTP/1.1 200 OK\n"
      "etag: W/\"anything\"");
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);
  EXPECT_TRUE(parsed->HasValidators());
}

TEST(HttpResponseHeadersTest, GetNormalizedHeaderWithEmptyValues) {
  std::string headers(
      "HTTP/1.1 200 OK\n"
      "a:\n"
      "b: \n"
      "c:*\n"
      "d: *\n"
      "e:    \n"
      "a: \n"
      "b:*\n"
      "c:\n"
      "d:*\n"
      "a:\n");
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);

  EXPECT_EQ(parsed->GetNormalizedHeader("a"), ", , ");
  EXPECT_EQ(parsed->GetNormalizedHeader("b"), ", *");
  EXPECT_EQ(parsed->GetNormalizedHeader("c"), "*, ");
  EXPECT_EQ(parsed->GetNormalizedHeader("d"), "*, *");
  EXPECT_EQ(parsed->GetNormalizedHeader("e"), "");
  EXPECT_EQ(parsed->GetNormalizedHeader("f"), std::nullopt);
}

TEST(HttpResponseHeadersTest, GetNormalizedHeaderWithCommas) {
  std::string headers(
      "HTTP/1.1 200 OK\n"
      "a: foo, bar\n"
      "b: , foo, bar,\n"
      "c: ,,,\n"
      "d:  ,  ,  ,  \n"
      "e:\t,\t,\t,\t\n"
      "a: ,");
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);

  // TODO(mmenke): "Normalized" headers probably should preserve the
  // leading/trailing whitespace from the original headers.
  EXPECT_EQ(parsed->GetNormalizedHeader("a"), "foo, bar, ,");
  EXPECT_EQ(parsed->GetNormalizedHeader("b"), ", foo, bar,");
  EXPECT_EQ(parsed->GetNormalizedHeader("c"), ",,,");
  EXPECT_EQ(parsed->GetNormalizedHeader("d"), ",  ,  ,");
  EXPECT_EQ(parsed->GetNormalizedHeader("e"), ",\t,\t,");
  EXPECT_EQ(parsed->GetNormalizedHeader("f"), std::nullopt);
}

TEST(HttpResponseHeadersTest, AddHeader) {
  scoped_refptr<HttpResponseHeaders> headers = HttpResponseHeaders::TryToCreate(
      "HTTP/1.1 200 OK\n"
      "connection: keep-alive\n"
      "Cache-control: max-age=10000\n");
  ASSERT_TRUE(headers);

  headers->AddHeader("Content-Length", "450");
  EXPECT_EQ(
      "HTTP/1.1 200 OK\n"
      "connection: keep-alive\n"
      "Cache-control: max-age=10000\n"
      "Content-Length: 450\n",
      ToSimpleString(headers));

  // Add a second Content-Length header with extra spaces in the value. It
  // should be added to the end, and the extra spaces removed.
  headers->AddHeader("Content-Length", "   42    ");
  EXPECT_EQ(
      "HTTP/1.1 200 OK\n"
      "connection: keep-alive\n"
      "Cache-control: max-age=10000\n"
      "Content-Length: 450\n"
      "Content-Length: 42\n",
      ToSimpleString(headers));
}

TEST(HttpResponseHeadersTest, SetHeader) {
  scoped_refptr<HttpResponseHeaders> headers = HttpResponseHeaders::TryToCreate(
      "HTTP/1.1 200 OK\n"
      "connection: keep-alive\n"
      "Cache-control: max-age=10000\n");
  ASSERT_TRUE(headers);

  headers->SetHeader("Content-Length", "450");
  EXPECT_EQ(
      "HTTP/1.1 200 OK\n"
      "connection: keep-alive\n"
      "Cache-control: max-age=10000\n"
      "Content-Length: 450\n",
      ToSimpleString(headers));

  headers->SetHeader("Content-Length", "   42    ");
  EXPECT_EQ(
      "HTTP/1.1 200 OK\n"
      "connection: keep-alive\n"
      "Cache-control: max-age=10000\n"
      "Content-Length: 42\n",
      ToSimpleString(headers));

  headers->SetHeader("connection", "close");
  EXPECT_EQ(
      "HTTP/1.1 200 OK\n"
      "Cache-control: max-age=10000\n"
      "Content-Length: 42\n"
      "connection: close\n",
      ToSimpleString(headers));
}

TEST(HttpResponseHeadersTest, TryToCreateWithNul) {
  static constexpr char kHeadersWithNuls[] = {
      "HTTP/1.1 200 OK\0"
      "Content-Type: application/octet-stream\0"};
  // The size must be specified explicitly to include the nul characters.
  static constexpr std::string_view kHeadersWithNulsAsStringPiece(
      kHeadersWithNuls, sizeof(kHeadersWithNuls));
  scoped_refptr<HttpResponseHeaders> headers =
      HttpResponseHeaders::TryToCreate(kHeadersWithNulsAsStringPiece);
  EXPECT_EQ(headers, nullptr);
}

TEST(HttpResponseHeadersTest, TracingSupport) {
  scoped_refptr<HttpResponseHeaders> headers = HttpResponseHeaders::TryToCreate(
      "HTTP/1.1 200 OK\n"
      "connection: keep-alive\n");
  ASSERT_TRUE(headers);

  EXPECT_EQ(perfetto::TracedValueToString(headers),
            "{response_code:200,headers:[{name:connection,value:keep-alive}]}");
}

struct RemoveHeaderTestData {
  const char* orig_headers;
  const char* to_remove;
  const char* expected_headers;
};

class RemoveHeaderTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<RemoveHeaderTestData> {
};

TEST_P(RemoveHeaderTest, RemoveHeader) {
  const RemoveHeaderTestData test = GetParam();

  std::string orig_headers(test.orig_headers);
  HeadersToRaw(&orig_headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(orig_headers);

  std::string name(test.to_remove);
  parsed->RemoveHeader(name);

  EXPECT_EQ(std::string(test.expected_headers), ToSimpleString(parsed));
}

const RemoveHeaderTestData remove_header_tests[] = {
  { "HTTP/1.1 200 OK\n"
    "connection: keep-alive\n"
    "Cache-control: max-age=10000\n"
    "Content-Length: 450\n",

    "Content-Length",

    "HTTP/1.1 200 OK\n"
    "connection: keep-alive\n"
    "Cache-control: max-age=10000\n"
  },
  { "HTTP/1.1 200 OK\n"
    "connection: keep-alive  \n"
    "Content-Length  : 450  \n"
    "Cache-control: max-age=10000\n",

    "Content-Length",

    "HTTP/1.1 200 OK\n"
    "connection: keep-alive\n"
    "Cache-control: max-age=10000\n"
  },
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         RemoveHeaderTest,
                         testing::ValuesIn(remove_header_tests));

struct RemoveHeadersTestData {
  const char* orig_headers;
  const char* to_remove[2];
  const char* expected_headers;
};

class RemoveHeadersTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<RemoveHeadersTestData> {};

TEST_P(RemoveHeadersTest, RemoveHeaders) {
  const RemoveHeadersTestData test = GetParam();

  std::string orig_headers(test.orig_headers);
  HeadersToRaw(&orig_headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(orig_headers);

  std::unordered_set<std::string> to_remove;
  for (auto* header : test.to_remove) {
    if (header)
      to_remove.insert(header);
  }
  parsed->RemoveHeaders(to_remove);

  EXPECT_EQ(std::string(test.expected_headers), ToSimpleString(parsed));
}

const RemoveHeadersTestData remove_headers_tests[] = {
    {"HTTP/1.1 200 OK\n"
     "connection: keep-alive\n"
     "Cache-control: max-age=10000\n"
     "Content-Length: 450\n",

     {"Content-Length", "CACHE-control"},

     "HTTP/1.1 200 OK\n"
     "connection: keep-alive\n"},

    {"HTTP/1.1 200 OK\n"
     "connection: keep-alive\n"
     "Content-Length: 450\n",

     {"foo", "bar"},

     "HTTP/1.1 200 OK\n"
     "connection: keep-alive\n"
     "Content-Length: 450\n"},

    {"HTTP/1.1 404 Kinda not OK\n"
     "connection: keep-alive  \n",

     {},

     "HTTP/1.1 404 Kinda not OK\n"
     "connection: keep-alive\n"},
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         RemoveHeadersTest,
                         testing::ValuesIn(remove_headers_tests));

struct RemoveIndividualHeaderTestData {
  const char* orig_headers;
  const char* to_remove_name;
  const char* to_remove_value;
  const char* expected_headers;
};

class RemoveIndividualHeaderTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<RemoveIndividualHeaderTestData> {
};

TEST_P(RemoveIndividualHeaderTest, RemoveIndividualHeader) {
  const RemoveIndividualHeaderTestData test = GetParam();

  std::string orig_headers(test.orig_headers);
  HeadersToRaw(&orig_headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(orig_headers);

  std::string name(test.to_remove_name);
  std::string value(test.to_remove_value);
  parsed->RemoveHeaderLine(name, value);

  EXPECT_EQ(std::string(test.expected_headers), ToSimpleString(parsed));
}

const RemoveIndividualHeaderTestData remove_individual_header_tests[] = {
  { "HTTP/1.1 200 OK\n"
    "connection: keep-alive\n"
    "Cache-control: max-age=10000\n"
    "Content-Length: 450\n",

    "Content-Length",

    "450",

    "HTTP/1.1 200 OK\n"
    "connection: keep-alive\n"
    "Cache-control: max-age=10000\n"
  },
  { "HTTP/1.1 200 OK\n"
    "connection: keep-alive  \n"
    "Content-Length  : 450  \n"
    "Cache-control: max-age=10000\n",

    "Content-Length",

    "450",

    "HTTP/1.1 200 OK\n"
    "connection: keep-alive\n"
    "Cache-control: max-age=10000\n"
  },
  { "HTTP/1.1 200 OK\n"
    "connection: keep-alive  \n"
    "Content-Length: 450\n"
    "Cache-control: max-age=10000\n",

    "Content-Length",  // Matching name.

    "999",  // Mismatching value.

    "HTTP/1.1 200 OK\n"
    "connection: keep-alive\n"
    "Content-Length: 450\n"
    "Cache-control: max-age=10000\n"
  },
  { "HTTP/1.1 200 OK\n"
    "connection: keep-alive  \n"
    "Foo: bar, baz\n"
    "Foo: bar\n"
    "Cache-control: max-age=10000\n",

    "Foo",

    "bar, baz",  // Space in value.

    "HTTP/1.1 200 OK\n"
    "connection: keep-alive\n"
    "Foo: bar\n"
    "Cache-control: max-age=10000\n"
  },
  { "HTTP/1.1 200 OK\n"
    "connection: keep-alive  \n"
    "Foo: bar, baz\n"
    "Cache-control: max-age=10000\n",

    "Foo",

    "baz",  // Only partial match -> ignored.

    "HTTP/1.1 200 OK\n"
    "connection: keep-alive\n"
    "Foo: bar, baz\n"
    "Cache-control: max-age=10000\n"
  },
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         RemoveIndividualHeaderTest,
                         testing::ValuesIn(remove_individual_header_tests));

struct ReplaceStatusTestData {
  const char* orig_headers;
  const char* new_status;
  const char* expected_headers;
};

class ReplaceStatusTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<ReplaceStatusTestData> {
};

TEST_P(ReplaceStatusTest, ReplaceStatus) {
  const ReplaceStatusTestData test = GetParam();

  std::string orig_headers(test.orig_headers);
  HeadersToRaw(&orig_headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(orig_headers);

  std::string name(test.new_status);
  parsed->ReplaceStatusLine(name);

  EXPECT_EQ(std::string(test.expected_headers), ToSimpleString(parsed));
}

const ReplaceStatusTestData replace_status_tests[] = {
  { "HTTP/1.1 206 Partial Content\n"
    "connection: keep-alive\n"
    "Cache-control: max-age=10000\n"
    "Content-Length: 450\n",

    "HTTP/1.1 200 OK",

    "HTTP/1.1 200 OK\n"
    "connection: keep-alive\n"
    "Cache-control: max-age=10000\n"
    "Content-Length: 450\n"
  },
  { "HTTP/1.1 200 OK\n"
    "connection: keep-alive\n",

    "HTTP/1.1 304 Not Modified",

    "HTTP/1.1 304 Not Modified\n"
    "connection: keep-alive\n"
  },
  { "HTTP/1.1 200 OK\n"
    "connection: keep-alive  \n"
    "Content-Length  : 450   \n"
    "Cache-control: max-age=10000\n",

    "HTTP/1//1 304 Not Modified",

    "HTTP/1.0 304 Not Modified\n"
    "connection: keep-alive\n"
    "Content-Length: 450\n"
    "Cache-control: max-age=10000\n"
  },
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         ReplaceStatusTest,
                         testing::ValuesIn(replace_status_tests));

struct UpdateWithNewRangeTestData {
  const char* orig_headers;
  const char* expected_headers;
  const char* expected_headers_with_replaced_status;
};

class UpdateWithNewRangeTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<UpdateWithNewRangeTestData> {
};

TEST_P(UpdateWithNewRangeTest, UpdateWithNewRange) {
  const UpdateWithNewRangeTestData test = GetParam();

  const HttpByteRange range = HttpByteRange::Bounded(3, 5);

  std::string orig_headers(test.orig_headers);
  std::replace(orig_headers.begin(), orig_headers.end(), '\n', '\0');
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(orig_headers + '\0');
  int64_t content_size = parsed->GetContentLength();

  // Update headers without replacing status line.
  parsed->UpdateWithNewRange(range, content_size, false);
  EXPECT_EQ(std::string(test.expected_headers), ToSimpleString(parsed));

  // Replace status line too.
  parsed->UpdateWithNewRange(range, content_size, true);
  EXPECT_EQ(std::string(test.expected_headers_with_replaced_status),
            ToSimpleString(parsed));
}

const UpdateWithNewRangeTestData update_range_tests[] = {
  { "HTTP/1.1 200 OK\n"
    "Content-Length: 450\n",

    "HTTP/1.1 200 OK\n"
    "Content-Range: bytes 3-5/450\n"
    "Content-Length: 3\n",

    "HTTP/1.1 206 Partial Content\n"
    "Content-Range: bytes 3-5/450\n"
    "Content-Length: 3\n",
  },
  { "HTTP/1.1 200 OK\n"
    "Content-Length: 5\n",

    "HTTP/1.1 200 OK\n"
    "Content-Range: bytes 3-5/5\n"
    "Content-Length: 3\n",

    "HTTP/1.1 206 Partial Content\n"
    "Content-Range: bytes 3-5/5\n"
    "Content-Length: 3\n",
  },
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         UpdateWithNewRangeTest,
                         testing::ValuesIn(update_range_tests));

TEST_F(HttpResponseHeadersCacheControlTest, AbsentMaxAgeReturnsFalse) {
  InitializeHeadersWithCacheControl("nocache");
  EXPECT_FALSE(headers()->GetMaxAgeValue());
}

TEST_F(HttpResponseHeadersCacheControlTest, MaxAgeWithNoParameterRejected) {
  InitializeHeadersWithCacheControl("max-age=,private");
  EXPECT_FALSE(headers()->GetMaxAgeValue());
}

TEST_F(HttpResponseHeadersCacheControlTest, MaxAgeWithSpaceParameterRejected) {
  InitializeHeadersWithCacheControl("max-age= ,private");
  EXPECT_FALSE(headers()->GetMaxAgeValue());
}

TEST_F(HttpResponseHeadersCacheControlTest, MaxAgeWithInterimSpaceIsRejected) {
  InitializeHeadersWithCacheControl("max-age=1 2");
  EXPECT_FALSE(headers()->GetMaxAgeValue());
}

TEST_F(HttpResponseHeadersCacheControlTest, MaxAgeWithMinusSignIsRejected) {
  InitializeHeadersWithCacheControl("max-age=-7");
  EXPECT_FALSE(headers()->GetMaxAgeValue());
}

TEST_F(HttpResponseHeadersCacheControlTest,
       MaxAgeWithSpaceBeforeEqualsIsRejected) {
  InitializeHeadersWithCacheControl("max-age = 7");
  EXPECT_FALSE(headers()->GetMaxAgeValue());
}

TEST_F(HttpResponseHeadersCacheControlTest,
       MaxAgeWithLeadingandTrailingSpaces) {
  InitializeHeadersWithCacheControl("max-age= 7  ");
  EXPECT_EQ(base::Seconds(7), GetMaxAgeValue());
}

TEST_F(HttpResponseHeadersCacheControlTest, MaxAgeFirstMatchUsed) {
  InitializeHeadersWithCacheControl("max-age=10, max-age=20");
  EXPECT_EQ(base::Seconds(10), GetMaxAgeValue());
}

TEST_F(HttpResponseHeadersCacheControlTest, MaxAgeBogusFirstMatchUsed) {
  // "max-age10" isn't parsed as "max-age"; "max-age=now" is bogus and
  // ignored and so "max-age=20" is used.
  InitializeHeadersWithCacheControl(
      "max-age10, max-age=now, max-age=20, max-age=30");
  EXPECT_EQ(base::Seconds(20), GetMaxAgeValue());
}

TEST_F(HttpResponseHeadersCacheControlTest, MaxAgeCaseInsensitive) {
  InitializeHeadersWithCacheControl("Max-aGe=15");
  EXPECT_EQ(base::Seconds(15), GetMaxAgeValue());
}

TEST_F(HttpResponseHeadersCacheControlTest, MaxAgeOverflow) {
  InitializeHeadersWithCacheControl("max-age=99999999999999999999");
  EXPECT_EQ(base::TimeDelta::FiniteMax().InSeconds(),
            GetMaxAgeValue().InSeconds());
}

struct MaxAgeTestData {
  const char* max_age_string;
  const std::optional<int64_t> expected_seconds;
};

class MaxAgeEdgeCasesTest
    : public HttpResponseHeadersCacheControlTest,
      public ::testing::WithParamInterface<MaxAgeTestData> {
};

TEST_P(MaxAgeEdgeCasesTest, MaxAgeEdgeCases) {
  const MaxAgeTestData test = GetParam();

  std::string max_age = "max-age=";
  InitializeHeadersWithCacheControl(
      (max_age + test.max_age_string).c_str());
  if (test.expected_seconds.has_value()) {
    EXPECT_EQ(test.expected_seconds.value(), GetMaxAgeValue().InSeconds())
        << " for max-age=" << test.max_age_string;
  } else {
    EXPECT_FALSE(headers()->GetMaxAgeValue());
  }
}

const MaxAgeTestData max_age_tests[] = {
    {" 1 ", 1},  // Spaces are ignored.
    {"-1", std::nullopt},
    {"--1", std::nullopt},
    {"2s", std::nullopt},
    {"3 days", std::nullopt},
    {"'4'", std::nullopt},
    {"\"5\"", std::nullopt},
    {"0x6", std::nullopt},  // Hex not parsed as hex.
    {"7F", std::nullopt},   // Hex without 0x still not parsed as hex.
    {"010", 10},            // Octal not parsed as octal.
    {"9223372036853", 9223372036853},
    {"9223372036854", 9223372036854},
    {"9223372036855", 9223372036854},
    {"9223372036854775806", 9223372036854},
    {"9223372036854775807", 9223372036854},
    {"20000000000000000000", 9223372036854},  // Overflow int64_t.
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeadersCacheControl,
                         MaxAgeEdgeCasesTest,
                         testing::ValuesIn(max_age_tests));

TEST_F(HttpResponseHeadersCacheControlTest,
       AbsentStaleWhileRevalidateReturnsFalse) {
  InitializeHeadersWithCacheControl("max-age=3600");
  EXPECT_FALSE(headers()->GetStaleWhileRevalidateValue());
}

TEST_F(HttpResponseHeadersCacheControlTest,
       StaleWhileRevalidateWithoutValueRejected) {
  InitializeHeadersWithCacheControl("max-age=3600,stale-while-revalidate=");
  EXPECT_FALSE(headers()->GetStaleWhileRevalidateValue());
}

TEST_F(HttpResponseHeadersCacheControlTest,
       StaleWhileRevalidateWithInvalidValueIgnored) {
  InitializeHeadersWithCacheControl("max-age=3600,stale-while-revalidate=true");
  EXPECT_FALSE(headers()->GetStaleWhileRevalidateValue());
}

TEST_F(HttpResponseHeadersCacheControlTest, StaleWhileRevalidateValueReturned) {
  InitializeHeadersWithCacheControl("max-age=3600,stale-while-revalidate=7200");
  EXPECT_EQ(base::Seconds(7200), GetStaleWhileRevalidateValue());
}

TEST_F(HttpResponseHeadersCacheControlTest,
       FirstStaleWhileRevalidateValueUsed) {
  InitializeHeadersWithCacheControl(
      "stale-while-revalidate=1,stale-while-revalidate=7200");
  EXPECT_EQ(base::Seconds(1), GetStaleWhileRevalidateValue());
}

struct GetCurrentAgeTestData {
  const char* headers;
  const char* request_time;
  const char* response_time;
  const char* current_time;
  const int expected_age;
};

class GetCurrentAgeTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<GetCurrentAgeTestData> {
};

TEST_P(GetCurrentAgeTest, GetCurrentAge) {
  const GetCurrentAgeTestData test = GetParam();

  base::Time request_time, response_time, current_time;
  ASSERT_TRUE(base::Time::FromString(test.request_time, &request_time));
  ASSERT_TRUE(base::Time::FromString(test.response_time, &response_time));
  ASSERT_TRUE(base::Time::FromString(test.current_time, &current_time));

  std::string headers(test.headers);
  HeadersToRaw(&headers);
  auto parsed = base::MakeRefCounted<HttpResponseHeaders>(headers);

  base::TimeDelta age =
      parsed->GetCurrentAge(request_time, response_time, current_time);
  EXPECT_EQ(test.expected_age, age.InSeconds());
}

const struct GetCurrentAgeTestData get_current_age_tests[] = {
    // Without Date header.
    {"HTTP/1.1 200 OK\n"
     "Age: 2",
     "Fri, 20 Jan 2011 10:40:08 GMT", "Fri, 20 Jan 2011 10:40:12 GMT",
     "Fri, 20 Jan 2011 10:40:14 GMT", 8},
    // Without Age header.
    {"HTTP/1.1 200 OK\n"
     "Date: Fri, 20 Jan 2011 10:40:10 GMT\n",
     "Fri, 20 Jan 2011 10:40:08 GMT", "Fri, 20 Jan 2011 10:40:12 GMT",
     "Fri, 20 Jan 2011 10:40:14 GMT", 6},
    // date_value > response_time with Age header.
    {"HTTP/1.1 200 OK\n"
     "Date: Fri, 20 Jan 2011 10:40:14 GMT\n"
     "Age: 2\n",
     "Fri, 20 Jan 2011 10:40:08 GMT", "Fri, 20 Jan 2011 10:40:12 GMT",
     "Fri, 20 Jan 2011 10:40:14 GMT", 8},
     // date_value > response_time without Age header.
     {"HTTP/1.1 200 OK\n"
     "Date: Fri, 20 Jan 2011 10:40:14 GMT\n",
     "Fri, 20 Jan 2011 10:40:08 GMT", "Fri, 20 Jan 2011 10:40:12 GMT",
     "Fri, 20 Jan 2011 10:40:14 GMT", 6},
    // apparent_age > corrected_age_value
    {"HTTP/1.1 200 OK\n"
     "Date: Fri, 20 Jan 2011 10:40:07 GMT\n"
     "Age: 0\n",
     "Fri, 20 Jan 2011 10:40:08 GMT", "Fri, 20 Jan 2011 10:40:12 GMT",
     "Fri, 20 Jan 2011 10:40:14 GMT", 7}};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         GetCurrentAgeTest,
                         testing::ValuesIn(get_current_age_tests));

TEST(HttpResponseHeadersBuilderTest, Version) {
  for (HttpVersion version :
       {HttpVersion(1, 0), HttpVersion(1, 1), HttpVersion(2, 0)}) {
    auto headers = HttpResponseHeaders::Builder(version, "200").Build();
    EXPECT_EQ(base::StringPrintf("HTTP/%d.%d 200", version.major_value(),
                                 version.minor_value()),
              headers->GetStatusLine());
    EXPECT_EQ(version, headers->GetHttpVersion());
  }
}

struct BuilderStatusLineTestData {
  const std::string_view status;
  const std::string_view expected_status_line;
  const int expected_response_code;
  const std::string_view expected_status_text;
};

// Provide GTest with a method to print the BuilderStatusLineTestData, for ease
// of debugging.
void PrintTo(const BuilderStatusLineTestData& data, std::ostream* os) {
  *os << "\"" << data.status << "\", \"" << data.expected_status_line << "\", "
      << data.expected_response_code << ", \"" << data.expected_status_text
      << "\"}";
}

class BuilderStatusLineTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<BuilderStatusLineTestData> {};

TEST_P(BuilderStatusLineTest, Common) {
  const auto& [status, expected_status_line, expected_response_code,
               expected_status_text] = GetParam();

  auto http_response_headers =
      HttpResponseHeaders::Builder({1, 1}, status).Build();

  EXPECT_EQ(expected_status_line, http_response_headers->GetStatusLine());
  EXPECT_EQ(expected_response_code, http_response_headers->response_code());
  EXPECT_EQ(expected_status_text, http_response_headers->GetStatusText());
}

constexpr BuilderStatusLineTestData kBuilderStatusLineTests[] = {
    {// Simple case.
     "200 OK",

     "HTTP/1.1 200 OK", 200, "OK"},
    {// No status text.
     "200",

     "HTTP/1.1 200", 200, ""},
    {// Empty status.
     "",

     "HTTP/1.1 200", 200, ""},
    {// Space status.
     " ",

     "HTTP/1.1 200", 200, ""},
    {// Spaces removed from status.
     "    204       No content   ",

     "HTTP/1.1 204 No content", 204, "No content"},
    {// Tabs treated as terminating whitespace.
     "204   \t  No  content \t ",

     "HTTP/1.1 204 \t  No  content \t", 204, "\t  No  content \t"},
    {// Status text smushed into response code.
     "426Smush",

     "HTTP/1.1 426 Smush", 426, "Smush"},
    {// Tab gets included in status text.
     "501\tStatus\t",

     "HTTP/1.1 501 \tStatus\t", 501, "\tStatus\t"},
    {// Zero response code.
     "0 Zero",

     "HTTP/1.1 0 Zero", 0, "Zero"},
    {// Oversize response code.
     "20230904 Monday",

     "HTTP/1.1 20230904 Monday", 20230904, "Monday"},
    {// Overflowing response code.
     "9123456789 Overflow",

     "HTTP/1.1 9123456789 Overflow", 2147483647, "Overflow"},
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         BuilderStatusLineTest,
                         testing::ValuesIn(kBuilderStatusLineTests));

struct BuilderHeadersTestData {
  const std::vector<std::pair<std::string_view, std::string_view>> headers;
  const std::string_view expected_headers;
};

// Provide GTest with a method to print the BuilderHeadersTestData, for ease of
// debugging.
void PrintTo(const BuilderHeadersTestData& data, std::ostream* os) {
  *os << "{";
  for (const auto& header : data.headers) {
    *os << "{\"" << header.first << "\", \"" << header.second << "\"},";
  }
  std::string expected_headers(data.expected_headers);
  EscapeForPrinting(&expected_headers);
  *os << "}, \"" << expected_headers << "\"}";
}

class BuilderHeadersTest
    : public HttpResponseHeadersTest,
      public ::testing::WithParamInterface<BuilderHeadersTestData> {};

TEST_P(BuilderHeadersTest, Common) {
  const auto& [headers, expected_headers_const] = GetParam();
  HttpResponseHeaders::Builder builder({1, 1}, "200");
  for (const auto& [key, value] : headers) {
    builder.AddHeader(key, value);
  }
  auto http_response_headers = builder.Build();

  std::string output_headers = ToSimpleString(http_response_headers);
  std::string expected_headers(expected_headers_const);

  EscapeForPrinting(&output_headers);
  EscapeForPrinting(&expected_headers);

  EXPECT_EQ(expected_headers, output_headers);
}

const BuilderHeadersTestData builder_headers_tests[] = {
    {// Single header.
     {{"Content-Type", "text/html"}},

     "HTTP/1.1 200\n"
     "Content-Type: text/html\n"},
    {// Multiple headers.
     {
         {"Content-Type", "text/html"},
         {"Content-Length", "6"},
         {"Set-Cookie", "a=1"},
     },

     "HTTP/1.1 200\n"
     "Content-Type: text/html\n"
     "Content-Length: 6\n"
     "Set-Cookie: a=1\n"},
    {// Empty header value.
     {{"Pragma", ""}},

     "HTTP/1.1 200\n"
     "Pragma: \n"},
    {// Multiple header value.
     {{"Cache-Control", "no-cache, no-store"}},

     "HTTP/1.1 200\n"
     "Cache-Control: no-cache, no-store\n"},
    {// Spaces are removed around values, but when EnumerateHeaderLines()
     // rejoins continuations, it keeps interior spaces. .
     {{"X-Commas", "   ,  ,    "}},

     "HTTP/1.1 200\n"
     "X-Commas: ,  ,\n"},
    {// Single value is trimmed.
     {{"Pragma", "     no-cache   "}},

     "HTTP/1.1 200\n"
     "Pragma: no-cache\n"},
    {// Location header is trimmed.
     {{"Location", "   http://example.com/   "}},

     "HTTP/1.1 200\n"
     "Location: http://example.com/\n"},
};

INSTANTIATE_TEST_SUITE_P(HttpResponseHeaders,
                         BuilderHeadersTest,
                         testing::ValuesIn(builder_headers_tests));

TEST(HttpResponseHeadersTest, StrictlyEqualsSuccess) {
  constexpr char kRawHeaders[] =
      "HTTP/1.1 200\n"
      "Content-Type:application/octet-stream\n"
      "Cache-Control:no-cache, no-store\n";
  std::string raw_headers = kRawHeaders;
  HeadersToRaw(&raw_headers);
  const auto parsed = base::MakeRefCounted<HttpResponseHeaders>(raw_headers);
  const auto built = HttpResponseHeaders::Builder({1, 1}, "200")
                         .AddHeader("Content-Type", "application/octet-stream")
                         .AddHeader("Cache-Control", "no-cache, no-store")
                         .Build();
  EXPECT_TRUE(parsed->StrictlyEquals(*built));
  EXPECT_TRUE(built->StrictlyEquals(*parsed));
}

TEST(HttpResponseHeadersTest, StrictlyEqualsVersionMismatch) {
  const auto http10 = HttpResponseHeaders::Builder({1, 0}, "200").Build();
  const auto http11 = HttpResponseHeaders::Builder({1, 1}, "200").Build();
  EXPECT_FALSE(http10->StrictlyEquals(*http11));
  EXPECT_FALSE(http11->StrictlyEquals(*http10));
}

TEST(HttpResponseHeadersTest, StrictlyEqualsResponseCodeMismatch) {
  const auto response200 = HttpResponseHeaders::Builder({1, 1}, "200").Build();
  const auto response404 = HttpResponseHeaders::Builder({1, 1}, "404").Build();
  EXPECT_FALSE(response200->StrictlyEquals(*response404));
  EXPECT_FALSE(response404->StrictlyEquals(*response200));
}

TEST(HttpResponseHeadersTest, StrictlyEqualsStatusTextMismatch) {
  const auto ok = HttpResponseHeaders::Builder({1, 1}, "200 OK").Build();
  const auto ng = HttpResponseHeaders::Builder({1, 1}, "200 NG").Build();
  EXPECT_FALSE(ok->StrictlyEquals(*ng));
  EXPECT_FALSE(ng->StrictlyEquals(*ok));
}

TEST(HttpResponseHeadersTest, StrictlyEqualsRawMismatch) {
  // These are designed so that the offsets of names and values will be the
  // same.
  std::string raw1 =
      "HTTP/1.1 200\n"
      "Pragma :None\n";
  std::string raw2 =
      "HTTP/1.1 200\n"
      "Pragma: None\n";
  HeadersToRaw(&raw1);
  HeadersToRaw(&raw2);
  const auto parsed1 = base::MakeRefCounted<HttpResponseHeaders>(raw1);
  const auto parsed2 = base::MakeRefCounted<HttpResponseHeaders>(raw2);
  EXPECT_FALSE(parsed1->StrictlyEquals(*parsed2));
  EXPECT_FALSE(parsed2->StrictlyEquals(*parsed1));
}

// There's no known way to produce an HttpResponseHeaders object with the same
// `raw_headers_` but different `parsed_` structures, so there's no test for
// that.

struct FreshnessLifetimesTestCase {
  const char* headers;
  base::TimeDelta freshness;
  base::TimeDelta staleness;
};

class HttpResponseHeadersFreshnessTest
    : public testing::TestWithParam<FreshnessLifetimesTestCase> {};

TEST_P(HttpResponseHeadersFreshnessTest, GetFreshnessLifetimes) {
  const FreshnessLifetimesTestCase& test_case = GetParam();

  scoped_refptr<HttpResponseHeaders> parsed(
      new HttpResponseHeaders(HttpUtil::AssembleRawHeaders(test_case.headers)));

  base::Time response_time = base::Time::Now();

  HttpResponseHeaders::FreshnessLifetimes lifetimes =
      parsed->GetFreshnessLifetimes(response_time);

  EXPECT_EQ(test_case.freshness, lifetimes.freshness);
  EXPECT_EQ(test_case.staleness, lifetimes.staleness);
}

INSTANTIATE_TEST_SUITE_P(
    HttpResponseHeaders,
    HttpResponseHeadersFreshnessTest,
    testing::Values(
        // Basic max-age directive
        FreshnessLifetimesTestCase{"HTTP/1.1 200 OK\n"
                                   "Cache-Control: max-age10, max-age=8a0, "
                                   "max-age= 500, max-age=900\n\n",
                                   base::Seconds(500), base::TimeDelta()},
        // max-age with stale-while-revalidate
        FreshnessLifetimesTestCase{
            "HTTP/1.1 200 OK\n"
            "Cache-Control: max-age=300, stale-while-revalidate=600\n\n",
            base::Seconds(300), base::Seconds(600)},
        // must-revalidate overrides stale-while-revalidate
        FreshnessLifetimesTestCase{
            "HTTP/1.1 200 OK\n"
            "Cache-Control: max-age=400, must-revalidate, "
            "stale-while-revalidate=200\n\n",
            base::Seconds(400), base::TimeDelta()},
        // no-store directive should have zero freshness and staleness
        FreshnessLifetimesTestCase{"HTTP/1.1 200 OK\n"
                                   "Cache-Control: no-store\n\n",
                                   base::TimeDelta(), base::TimeDelta()},
        // no-cache directive should have zero freshness and staleness
        FreshnessLifetimesTestCase{"HTTP/1.1 200 OK\n"
                                   "Cache-Control: no-cache\n\n",
                                   base::TimeDelta(), base::TimeDelta()},
        // no-store overrides max-age and stale-while-revalidate
        FreshnessLifetimesTestCase{"HTTP/1.1 200 OK\n"
                                   "Cache-Control: max-age=500, "
                                   "stale-while-revalidate=600, no-store\n\n",
                                   base::TimeDelta(), base::TimeDelta()}));

}  // namespace

}  // namespace net
