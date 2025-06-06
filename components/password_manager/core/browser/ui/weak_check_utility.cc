// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/ui/weak_check_utility.h"

#include <functional>
#include <string_view>

#include "base/i18n/break_iterator.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/utf_string_conversion_utils.h"
#include "base/strings/utf_string_conversions.h"
#include "third_party/zxcvbn-cpp/native-src/zxcvbn/matching.hpp"
#include "third_party/zxcvbn-cpp/native-src/zxcvbn/scoring.hpp"
#include "third_party/zxcvbn-cpp/native-src/zxcvbn/time_estimates.hpp"

namespace password_manager {

std::u16string_view SafeTruncateUTF16(std::u16string_view str,
                                      size_t max_length) {
  if (str.length() <= max_length) {
    return str;
  }

  base::i18n::BreakIterator iter(str,
                                 base::i18n::BreakIterator::BREAK_CHARACTER);
  if (!iter.Init()) {
    return str.substr(0, max_length);
  }

  size_t char_count = 0;
  while (iter.Advance() && char_count < max_length) {
    char_count++;
  }
  return str.substr(0, iter.prev());
}

namespace {

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class PasswordWeaknessScore {
  kTooGuessablePassword = 0,
  kVeryGuessablePassword = 1,
  kSomewhatGuessablePassword = 2,
  kSafelyUnguessablePassword = 3,
  kVeryUnguessablePassword = 4,
  kMaxValue = kVeryUnguessablePassword,
};

// Passwords longer than this constant should not be checked for weakness using
// the zxcvbn-cpp library. This is because the runtime grows extremely, starting
// at a password length of 40.
// See https://github.com/dropbox/zxcvbn#runtime-latency
// Needs to stay in sync with google3 constant: http://shortn/_1ufIF61G4X
constexpr int kZxcvbnLengthCap = 40;

// If the password has a score of 2 or less, this password should be marked as
// weak. The lower the password score, the weaker it is.
constexpr int kLowSeverityScore = 2;

// Returns the |password| score.
int PasswordWeakCheck(std::u16string_view password16) {
  // zxcvbn's computation time explodes for long passwords, so cap at that
  // number.
  std::string password =
      base::UTF16ToUTF8(SafeTruncateUTF16(password16, kZxcvbnLengthCap));
  std::vector<zxcvbn::Match> matches = zxcvbn::omnimatch(password);
  zxcvbn::ScoringResult result =
      zxcvbn::most_guessable_match_sequence(password, matches);

  int score = zxcvbn::estimate_attack_times(result.guesses).score;
  base::UmaHistogramEnumeration("PasswordManager.WeakCheck.PasswordScore",
                                static_cast<PasswordWeaknessScore>(score));
  return score;
}

}  // namespace

IsWeakPassword IsWeak(std::u16string_view password) {
  return IsWeakPassword(PasswordWeakCheck(password) <= kLowSeverityScore);
}

base::flat_set<std::u16string> BulkWeakCheck(
    base::flat_set<std::u16string> passwords) {
  base::UmaHistogramCounts1000("PasswordManager.WeakCheck.CheckedPasswords",
                               passwords.size());
  base::EraseIf(passwords, std::not_fn(&IsWeak));
  base::UmaHistogramCounts1000("PasswordManager.WeakCheck.WeakPasswords",
                               passwords.size());
  return passwords;
}

}  // namespace password_manager
