// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/351564777): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include "crypto/hmac.h"

#include <stddef.h>
#include <string.h>

#include <array>
#include <string>
#include <string_view>

#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

static const size_t kSHA1DigestSize = 20;
static const size_t kSHA256DigestSize = 32;

static const char* kSimpleKey =
    "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA"
    "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA"
    "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA"
    "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA"
    "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA";
static const size_t kSimpleKeyLength = 80;

struct SimpleHmacCases {
  const char *data;
  const int data_len;
  const char *digest;
};
static const auto kSimpleHmacCases = std::to_array<SimpleHmacCases>(
    {{"Test Using Larger Than Block-Size Key - Hash Key First", 54,
      "\xAA\x4A\xE5\xE1\x52\x72\xD0\x0E\x95\x70\x56\x37\xCE\x8A\x3B\x55"
      "\xED\x40\x21\x12"},
     {"Test Using Larger Than Block-Size Key and Larger "
      "Than One Block-Size Data",
      73,
      "\xE8\xE9\x9D\x0F\x45\x23\x7D\x78\x6D\x6B\xBA\xA7\x96\x5C\x78\x08"
      "\xBB\xFF\x1A\x91"}});

TEST(HMACTest, HmacSafeBrowsingResponseTest) {
  const int kKeySize = 16;

  // Client key.
  const unsigned char kClientKey[kKeySize] =
      { 0xbf, 0xf6, 0x83, 0x4b, 0x3e, 0xa3, 0x23, 0xdd,
        0x96, 0x78, 0x70, 0x8e, 0xa1, 0x9d, 0x3b, 0x40 };

  // Expected HMAC result using kMessage and kClientKey.
  const unsigned char kReceivedHmac[kSHA1DigestSize] =
      { 0xb9, 0x3c, 0xd6, 0xf0, 0x49, 0x47, 0xe2, 0x52,
        0x59, 0x7a, 0xbd, 0x1f, 0x2b, 0x4c, 0x83, 0xad,
        0x86, 0xd2, 0x48, 0x85 };

  const char kMessage[] =
"n:1896\ni:goog-malware-shavar\nu:s.ytimg.com/safebrowsing/rd/goog-malware-shav"
"ar_s_445-450\nu:s.ytimg.com/safebrowsing/rd/goog-malware-shavar_s_439-444\nu:s"
".ytimg.com/safebrowsing/rd/goog-malware-shavar_s_437\nu:s.ytimg.com/safebrowsi"
"ng/rd/goog-malware-shavar_s_436\nu:s.ytimg.com/safebrowsing/rd/goog-malware-sh"
"avar_s_433-435\nu:s.ytimg.com/safebrowsing/rd/goog-malware-shavar_s_431\nu:s.y"
"timg.com/safebrowsing/rd/goog-malware-shavar_s_430\nu:s.ytimg.com/safebrowsing"
"/rd/goog-malware-shavar_s_429\nu:s.ytimg.com/safebrowsing/rd/goog-malware-shav"
"ar_s_428\nu:s.ytimg.com/safebrowsing/rd/goog-malware-shavar_s_426\nu:s.ytimg.c"
"om/safebrowsing/rd/goog-malware-shavar_s_424\nu:s.ytimg.com/safebrowsing/rd/go"
"og-malware-shavar_s_423\nu:s.ytimg.com/safebrowsing/rd/goog-malware-shavar_s_4"
"22\nu:s.ytimg.com/safebrowsing/rd/goog-malware-shavar_s_420\nu:s.ytimg.com/saf"
"ebrowsing/rd/goog-malware-shavar_s_419\nu:s.ytimg.com/safebrowsing/rd/goog-mal"
"ware-shavar_s_414\nu:s.ytimg.com/safebrowsing/rd/goog-malware-shavar_s_409-411"
"\nu:s.ytimg.com/safebrowsing/rd/goog-malware-shavar_s_405\nu:s.ytimg.com/safeb"
"rowsing/rd/goog-malware-shavar_s_404\nu:s.ytimg.com/safebrowsing/rd/goog-malwa"
"re-shavar_s_402\nu:s.ytimg.com/safebrowsing/rd/goog-malware-shavar_s_401\nu:s."
"ytimg.com/safebrowsing/rd/goog-malware-shavar_a_973-978\nu:s.ytimg.com/safebro"
"wsing/rd/goog-malware-shavar_a_937-972\nu:s.ytimg.com/safebrowsing/rd/goog-mal"
"ware-shavar_a_931-936\nu:s.ytimg.com/safebrowsing/rd/goog-malware-shavar_a_925"
"-930\nu:s.ytimg.com/safebrowsing/rd/goog-malware-shavar_a_919-924\ni:goog-phis"
"h-shavar\nu:s.ytimg.com/safebrowsing/rd/goog-phish-shavar_a_2633\nu:s.ytimg.co"
"m/safebrowsing/rd/goog-phish-shavar_a_2632\nu:s.ytimg.com/safebrowsing/rd/goog"
"-phish-shavar_a_2629-2631\nu:s.ytimg.com/safebrowsing/rd/goog-phish-shavar_a_2"
"626-2628\nu:s.ytimg.com/safebrowsing/rd/goog-phish-shavar_a_2625\n";

  std::string message_data(kMessage);

  crypto::HMAC hmac(crypto::HMAC::SHA1);
  ASSERT_TRUE(hmac.Init(kClientKey, kKeySize));
  unsigned char calculated_hmac[kSHA1DigestSize];

  EXPECT_TRUE(hmac.Sign(message_data, calculated_hmac, kSHA1DigestSize));
  EXPECT_EQ(0, memcmp(kReceivedHmac, calculated_hmac, kSHA1DigestSize));
}

// Test cases from RFC 2202 section 3
TEST(HMACTest, RFC2202TestCases) {
  struct Cases {
    const char *key;
    const int key_len;
    const char *data;
    const int data_len;
    const char *digest;
  };
  const auto cases = std::to_array<Cases>({
      {"\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B"
       "\x0B\x0B\x0B\x0B",
       20, "Hi There", 8,
       "\xB6\x17\x31\x86\x55\x05\x72\x64\xE2\x8B\xC0\xB6\xFB\x37\x8C\x8E"
       "\xF1\x46\xBE\x00"},
      {"Jefe", 4, "what do ya want for nothing?", 28,
       "\xEF\xFC\xDF\x6A\xE5\xEB\x2F\xA2\xD2\x74\x16\xD5\xF1\x84\xDF\x9C"
       "\x25\x9A\x7C\x79"},
      {"\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA"
       "\xAA\xAA\xAA\xAA",
       20,
       "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
       "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
       "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
       "\xDD\xDD",
       50,
       "\x12\x5D\x73\x42\xB9\xAC\x11\xCD\x91\xA3\x9A\xF4\x8A\xA1\x7B\x4F"
       "\x63\xF1\x75\xD3"},
      {"\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F\x10"
       "\x11\x12\x13\x14\x15\x16\x17\x18\x19",
       25,
       "\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
       "\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
       "\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
       "\xCD\xCD",
       50,
       "\x4C\x90\x07\xF4\x02\x62\x50\xC6\xBC\x84\x14\xF9\xBF\x50\xC8\x6C"
       "\x2D\x72\x35\xDA"},
      {"\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C"
       "\x0C\x0C\x0C\x0C",
       20, "Test With Truncation", 20,
       "\x4C\x1A\x03\x42\x4B\x55\xE0\x7F\xE7\xF2\x7B\xE1\xD5\x8B\xB9\x32"
       "\x4A\x9A\x5A\x04"},
      {"\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA"
       "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA"
       "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA"
       "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA"
       "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA",
       80, "Test Using Larger Than Block-Size Key - Hash Key First", 54,
       "\xAA\x4A\xE5\xE1\x52\x72\xD0\x0E\x95\x70\x56\x37\xCE\x8A\x3B\x55"
       "\xED\x40\x21\x12"},
      {"\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA"
       "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA"
       "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA"
       "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA"
       "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA",
       80,
       "Test Using Larger Than Block-Size Key and Larger "
       "Than One Block-Size Data",
       73,
       "\xE8\xE9\x9D\x0F\x45\x23\x7D\x78\x6D\x6B\xBA\xA7\x96\x5C\x78\x08"
       "\xBB\xFF\x1A\x91"},
  });

  for (size_t i = 0; i < std::size(cases); ++i) {
    crypto::HMAC hmac(crypto::HMAC::SHA1);
    ASSERT_TRUE(hmac.Init(reinterpret_cast<const unsigned char*>(cases[i].key),
                          cases[i].key_len));
    std::string data_string(cases[i].data, cases[i].data_len);
    unsigned char digest[kSHA1DigestSize];
    EXPECT_TRUE(hmac.Sign(data_string, digest, kSHA1DigestSize));
    EXPECT_EQ(0, memcmp(cases[i].digest, digest, kSHA1DigestSize));
  }
}

// TODO(wtc): add other test vectors from RFC 4231.
TEST(HMACTest, RFC4231TestCase6) {
  unsigned char key[131];
  for (size_t i = 0; i < sizeof(key); ++i)
    key[i] = 0xaa;

  std::string data = "Test Using Larger Than Block-Size Key - Hash Key First";
  ASSERT_EQ(54U, data.size());

  static unsigned char kKnownHMACSHA256[] = {
    0x60, 0xe4, 0x31, 0x59, 0x1e, 0xe0, 0xb6, 0x7f,
    0x0d, 0x8a, 0x26, 0xaa, 0xcb, 0xf5, 0xb7, 0x7f,
    0x8e, 0x0b, 0xc6, 0x21, 0x37, 0x28, 0xc5, 0x14,
    0x05, 0x46, 0x04, 0x0f, 0x0e, 0xe3, 0x7f, 0x54
  };

  crypto::HMAC hmac(crypto::HMAC::SHA256);
  ASSERT_TRUE(hmac.Init(key, sizeof(key)));
  unsigned char calculated_hmac[kSHA256DigestSize];

  EXPECT_EQ(kSHA256DigestSize, hmac.DigestLength());
  EXPECT_TRUE(hmac.Sign(data, calculated_hmac, kSHA256DigestSize));
  EXPECT_EQ(0, memcmp(kKnownHMACSHA256, calculated_hmac, kSHA256DigestSize));
}

// Based on NSS's FIPS HMAC power-up self-test.
TEST(HMACTest, NSSFIPSPowerUpSelfTest) {
  static const char kKnownMessage[] =
      "The test message for the MD2, MD5, and SHA-1 hashing algorithms.";

  static const unsigned char kKnownSecretKey[] = {
    0x46, 0x69, 0x72, 0x65, 0x66, 0x6f, 0x78, 0x20,
    0x61, 0x6e, 0x64, 0x20, 0x54, 0x68, 0x75, 0x6e,
    0x64, 0x65, 0x72, 0x42, 0x69, 0x72, 0x64, 0x20,
    0x61, 0x72, 0x65, 0x20, 0x61, 0x77, 0x65, 0x73,
    0x6f, 0x6d, 0x65, 0x21, 0x00
  };

  static const size_t kKnownSecretKeySize = sizeof(kKnownSecretKey);

  // HMAC-SHA-1 known answer (20 bytes).
  static const unsigned char kKnownHMACSHA1[] = {
    0xd5, 0x85, 0xf6, 0x5b, 0x39, 0xfa, 0xb9, 0x05,
    0x3b, 0x57, 0x1d, 0x61, 0xe7, 0xb8, 0x84, 0x1e,
    0x5d, 0x0e, 0x1e, 0x11
  };

  // HMAC-SHA-256 known answer (32 bytes).
  static const unsigned char kKnownHMACSHA256[] = {
    0x05, 0x75, 0x9a, 0x9e, 0x70, 0x5e, 0xe7, 0x44,
    0xe2, 0x46, 0x4b, 0x92, 0x22, 0x14, 0x22, 0xe0,
    0x1b, 0x92, 0x8a, 0x0c, 0xfe, 0xf5, 0x49, 0xe9,
    0xa7, 0x1b, 0x56, 0x7d, 0x1d, 0x29, 0x40, 0x48
  };

  std::string message_data(kKnownMessage);

  crypto::HMAC hmac(crypto::HMAC::SHA1);
  ASSERT_TRUE(hmac.Init(kKnownSecretKey, kKnownSecretKeySize));
  unsigned char calculated_hmac[kSHA1DigestSize];

  EXPECT_EQ(kSHA1DigestSize, hmac.DigestLength());
  EXPECT_TRUE(hmac.Sign(message_data, calculated_hmac, kSHA1DigestSize));
  EXPECT_EQ(0, memcmp(kKnownHMACSHA1, calculated_hmac, kSHA1DigestSize));
  EXPECT_TRUE(hmac.Verify(
      message_data,
      std::string_view(reinterpret_cast<const char*>(kKnownHMACSHA1),
                       kSHA1DigestSize)));
  EXPECT_TRUE(hmac.VerifyTruncated(
      message_data,
      std::string_view(reinterpret_cast<const char*>(kKnownHMACSHA1),
                       kSHA1DigestSize / 2)));

  crypto::HMAC hmac2(crypto::HMAC::SHA256);
  ASSERT_TRUE(hmac2.Init(kKnownSecretKey, kKnownSecretKeySize));
  unsigned char calculated_hmac2[kSHA256DigestSize];

  EXPECT_TRUE(hmac2.Sign(message_data, calculated_hmac2, kSHA256DigestSize));
  EXPECT_EQ(0, memcmp(kKnownHMACSHA256, calculated_hmac2, kSHA256DigestSize));
}

TEST(HMACTest, HMACObjectReuse) {
  crypto::HMAC hmac(crypto::HMAC::SHA1);
  ASSERT_TRUE(
      hmac.Init(reinterpret_cast<const unsigned char*>(kSimpleKey),
                kSimpleKeyLength));
  for (size_t i = 0; i < std::size(kSimpleHmacCases); ++i) {
    std::string data_string(kSimpleHmacCases[i].data,
                            kSimpleHmacCases[i].data_len);
    unsigned char digest[kSHA1DigestSize];
    EXPECT_TRUE(hmac.Sign(data_string, digest, kSHA1DigestSize));
    EXPECT_EQ(0, memcmp(kSimpleHmacCases[i].digest, digest, kSHA1DigestSize));
  }
}

TEST(HMACTest, Verify) {
  crypto::HMAC hmac(crypto::HMAC::SHA1);
  ASSERT_TRUE(
      hmac.Init(reinterpret_cast<const unsigned char*>(kSimpleKey),
                kSimpleKeyLength));
  const char empty_digest[kSHA1DigestSize] = { 0 };
  for (size_t i = 0; i < std::size(kSimpleHmacCases); ++i) {
    // Expected results
    EXPECT_TRUE(hmac.Verify(
        std::string_view(kSimpleHmacCases[i].data,
                         kSimpleHmacCases[i].data_len),
        std::string_view(kSimpleHmacCases[i].digest, kSHA1DigestSize)));
    // Mismatched size
    EXPECT_FALSE(hmac.Verify(std::string_view(kSimpleHmacCases[i].data,
                                              kSimpleHmacCases[i].data_len),
                             std::string_view(kSimpleHmacCases[i].data,
                                              kSimpleHmacCases[i].data_len)));

    // Expected size, mismatched data
    EXPECT_FALSE(hmac.Verify(std::string_view(kSimpleHmacCases[i].data,
                                              kSimpleHmacCases[i].data_len),
                             std::string_view(empty_digest, kSHA1DigestSize)));
  }
}

TEST(HMACTest, EmptyKey) {
  // Test vector from https://en.wikipedia.org/wiki/HMAC
  const char* kExpectedDigest =
      "\xFB\xDB\x1D\x1B\x18\xAA\x6C\x08\x32\x4B\x7D\x64\xB7\x1F\xB7\x63"
      "\x70\x69\x0E\x1D";
  std::string_view data("");

  crypto::HMAC hmac(crypto::HMAC::SHA1);
  ASSERT_TRUE(hmac.Init(nullptr, 0));

  unsigned char digest[kSHA1DigestSize];
  EXPECT_TRUE(hmac.Sign(data, digest, kSHA1DigestSize));
  EXPECT_EQ(0, memcmp(kExpectedDigest, digest, kSHA1DigestSize));

  EXPECT_TRUE(
      hmac.Verify(data, std::string_view(kExpectedDigest, kSHA1DigestSize)));
}

TEST(HMACTest, TooLong) {
  // See RFC4231, section 4.7.
  unsigned char key[131];
  for (size_t i = 0; i < sizeof(key); ++i)
    key[i] = 0xaa;

  std::string data = "Test Using Larger Than Block-Size Key - Hash Key First";
  static uint8_t kKnownHMACSHA256[] = {
      0x60, 0xe4, 0x31, 0x59, 0x1e, 0xe0, 0xb6, 0x7f, 0x0d, 0x8a, 0x26,
      0xaa, 0xcb, 0xf5, 0xb7, 0x7f, 0x8e, 0x0b, 0xc6, 0x21, 0x37, 0x28,
      0xc5, 0x14, 0x05, 0x46, 0x04, 0x0f, 0x0e, 0xe3, 0x7f, 0x54};

  crypto::HMAC hmac(crypto::HMAC::SHA256);
  ASSERT_TRUE(hmac.Init(key, sizeof(key)));

  // Attempting to write too large of an HMAC is an error.
  uint8_t calculated_hmac[kSHA256DigestSize + 1];
  EXPECT_FALSE(hmac.Sign(data, calculated_hmac, sizeof(calculated_hmac)));

  // Attempting to verify too large of an HMAC is an error.
  memcpy(calculated_hmac, kKnownHMACSHA256, kSHA256DigestSize);
  calculated_hmac[kSHA256DigestSize] = 0;
  EXPECT_FALSE(hmac.VerifyTruncated(
      data,
      std::string(calculated_hmac, calculated_hmac + sizeof(calculated_hmac))));
}

TEST(HMACTest, Bytes) {
  // See RFC4231, section 4.7.
  std::vector<uint8_t> key(131, 0xaa);
  std::string data_str =
      "Test Using Larger Than Block-Size Key - Hash Key First";
  std::vector<uint8_t> data(data_str.begin(), data_str.end());
  static uint8_t kKnownHMACSHA256[] = {
      0x60, 0xe4, 0x31, 0x59, 0x1e, 0xe0, 0xb6, 0x7f, 0x0d, 0x8a, 0x26,
      0xaa, 0xcb, 0xf5, 0xb7, 0x7f, 0x8e, 0x0b, 0xc6, 0x21, 0x37, 0x28,
      0xc5, 0x14, 0x05, 0x46, 0x04, 0x0f, 0x0e, 0xe3, 0x7f, 0x54};

  crypto::HMAC hmac(crypto::HMAC::SHA256);
  ASSERT_TRUE(hmac.Init(key));

  uint8_t calculated_hmac[kSHA256DigestSize];
  ASSERT_TRUE(hmac.Sign(data, calculated_hmac));
  EXPECT_EQ(0, memcmp(kKnownHMACSHA256, calculated_hmac, kSHA256DigestSize));

  EXPECT_TRUE(hmac.Verify(data, calculated_hmac));
  EXPECT_TRUE(hmac.VerifyTruncated(
      data, base::span(calculated_hmac, kSHA256DigestSize / 2)));

  data[0]++;
  EXPECT_FALSE(hmac.Verify(data, calculated_hmac));
  EXPECT_FALSE(hmac.VerifyTruncated(
      data, base::span(calculated_hmac, kSHA256DigestSize / 2)));
}

TEST(HMACTest, OneShotSha1) {
  // RFC 2202 test case 3:
  std::vector<uint8_t> key(20, 0xaa);
  std::vector<uint8_t> data(50, 0xdd);
  std::vector<uint8_t> expected;
  CHECK(base::HexStringToBytes("125d7342b9ac11cd91a39af48aa17b4f63f175d3",
                               &expected));

  auto result = crypto::hmac::SignSha1(key, data);
  EXPECT_EQ(base::as_byte_span(result), base::as_byte_span(expected));
  EXPECT_TRUE(crypto::hmac::VerifySha1(key, data, result));
  result[0] ^= 0x01;
  EXPECT_FALSE(crypto::hmac::VerifySha1(key, data, result));
}

TEST(HMACTest, OneShotSha256) {
  // RFC 4231 test case 3:
  std::vector<uint8_t> key(20, 0xaa);
  std::vector<uint8_t> data(50, 0xdd);
  std::vector<uint8_t> expected;
  CHECK(base::HexStringToBytes(
      "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe",
      &expected));

  auto result = crypto::hmac::SignSha256(key, data);
  EXPECT_EQ(base::as_byte_span(result), base::as_byte_span(expected));
  EXPECT_TRUE(crypto::hmac::VerifySha256(key, data, result));
  result[0] ^= 0x01;
  EXPECT_FALSE(crypto::hmac::VerifySha256(key, data, result));
}

TEST(HMACTest, OneShotSha384) {
  // RFC 4231 test case 3:
  std::vector<uint8_t> key(20, 0xaa);
  std::vector<uint8_t> data(50, 0xdd);
  std::vector<uint8_t> expected;
  CHECK(
      base::HexStringToBytes("88062608d3e6ad8a0aa2ace014c8a86f0aa635d947ac9febe"
                             "83ef4e55966144b2a5ab39dc13814b94e3ab6e101a34f27",
                             &expected));

  std::array<uint8_t, crypto::hash::kSha384Size> result;
  crypto::hmac::Sign(crypto::hash::kSha384, key, data, result);
  EXPECT_EQ(base::as_byte_span(result), base::as_byte_span(expected));
  EXPECT_TRUE(crypto::hmac::Verify(crypto::hash::kSha384, key, data, result));
  result[0] ^= 0x01;
  EXPECT_FALSE(crypto::hmac::Verify(crypto::hash::kSha384, key, data, result));
}

TEST(HMACTest, OneShotSha512) {
  // RFC 4231 test case 3:
  std::vector<uint8_t> key(20, 0xaa);
  std::vector<uint8_t> data(50, 0xdd);
  std::vector<uint8_t> expected;
  CHECK(base::HexStringToBytes(
      "fa73b0089d56a284efb0f0756c890be9b1b5dbdd8ee81a3655f83e33b2279d39"
      "bf3e848279a722c806b485a47e67c807b946a337bee8942674278859e13292fb",
      &expected));

  auto result = crypto::hmac::SignSha512(key, data);
  EXPECT_EQ(base::as_byte_span(result), base::as_byte_span(expected));
  EXPECT_TRUE(crypto::hmac::VerifySha512(key, data, result));
  result[0] ^= 0x01;
  EXPECT_FALSE(crypto::hmac::VerifySha512(key, data, result));
}

TEST(HMACTest, OneShotWrongLengthDies) {
  std::array<uint8_t, 32> key;
  std::array<uint8_t, 32> data;
  std::array<uint8_t, 16> small_hmac;
  std::array<uint8_t, 128> big_hmac;

  EXPECT_DEATH_IF_SUPPORTED(crypto::hmac::Sign(crypto::hash::HashKind::kSha256,
                                               key, data, small_hmac),
                            "");
  EXPECT_DEATH_IF_SUPPORTED(
      crypto::hmac::Sign(crypto::hash::HashKind::kSha256, key, data, big_hmac),
      "");

  EXPECT_DEATH_IF_SUPPORTED(
      (void)crypto::hmac::Verify(crypto::hash::HashKind::kSha256, key, data,
                                 small_hmac),
      "");
  EXPECT_DEATH_IF_SUPPORTED(
      (void)crypto::hmac::Verify(crypto::hash::HashKind::kSha256, key, data,
                                 big_hmac),
      "");
}
