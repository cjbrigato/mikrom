// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "chrome/browser/browser_process.h"
#include "components/autofill/core/browser/geo/autofill_country.h"
#include "third_party/libphonenumber/phonenumber_api.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "chrome/browser/autofill/android/jni_headers/PhoneNumberUtil_jni.h"

namespace autofill {

namespace {
using ::base::android::ConvertJavaStringToUTF8;
using ::base::android::ConvertUTF8ToJavaString;
using ::base::android::JavaParamRef;
using ::base::android::ScopedJavaLocalRef;

// Formats the `phone_number` to the specified `format` for the given country
// `country_code`. Returns the original number if the operation is not possible.
std::string FormatPhoneNumberWithCountryCode(
    const std::string& phone_number,
    const std::string& country_code,
    ::i18n::phonenumbers::PhoneNumberUtil::PhoneNumberFormat format) {
  ::i18n::phonenumbers::PhoneNumber parsed_number;
  ::i18n::phonenumbers::PhoneNumberUtil* phone_number_util =
      ::i18n::phonenumbers::PhoneNumberUtil::GetInstance();
  if (phone_number_util->Parse(phone_number, country_code, &parsed_number) !=
      ::i18n::phonenumbers::PhoneNumberUtil::NO_PARSING_ERROR) {
    return phone_number;
  }

  std::string formatted_number;
  phone_number_util->Format(parsed_number, format, &formatted_number);
  return formatted_number;
}

// Formats the `phone_number` to the specified `format`. Use application locale
// to determine country code. Returns the original number if the operation is
// not possible.
std::string FormatPhoneNumber(
    const std::string& phone_number,
    ::i18n::phonenumbers::PhoneNumberUtil::PhoneNumberFormat format) {
  return FormatPhoneNumberWithCountryCode(
      phone_number,
      autofill::AutofillCountry::CountryCodeForLocale(
          g_browser_process->GetApplicationLocale()),
      format);
}

}  // namespace

// Formats the given number `phone_number` for the given country
// `jcountry_code` to
// i18n::phonenumbers::PhoneNumberUtil::PhoneNumberFormat::INTERNATIONAL format
// by using i18n::phonenumbers::PhoneNumberUtil::Format.
std::string JNI_PhoneNumberUtil_FormatForDisplay(
    JNIEnv* env,
    std::string& phone_number,
    const JavaParamRef<jstring>& jcountry_code) {
  return jcountry_code.is_null()
             ? FormatPhoneNumber(phone_number,
                                 ::i18n::phonenumbers::PhoneNumberUtil::
                                     PhoneNumberFormat::INTERNATIONAL)
             : FormatPhoneNumberWithCountryCode(
                   phone_number, ConvertJavaStringToUTF8(env, jcountry_code),
                   ::i18n::phonenumbers::PhoneNumberUtil::PhoneNumberFormat::
                       INTERNATIONAL);
}

// Formats the given number `phone_number` to
// i18n::phonenumbers::PhoneNumberUtil::PhoneNumberFormat::E164 format by using
// i18n::phonenumbers::PhoneNumberUtil::Format , as defined in the Payment
// Request spec
// (https://w3c.github.io/browser-payment-api/#paymentrequest-updated-algorithm)
std::string JNI_PhoneNumberUtil_FormatForResponse(JNIEnv* env,
                                                  std::string& phone_number) {
  return FormatPhoneNumber(
      phone_number,
      ::i18n::phonenumbers::PhoneNumberUtil::PhoneNumberFormat::E164);
}

// Checks whether the given number `phone_number` is a possible number for a
// given country `jcountry_code` by using
// i18n::phonenumbers::PhoneNumberUtil::IsPossibleNumberForString.
jboolean JNI_PhoneNumberUtil_IsPossibleNumber(
    JNIEnv* env,
    std::string& phone_number,
    const JavaParamRef<jstring>& jcountry_code) {
  const std::string country_code =
      jcountry_code.is_null() ? autofill::AutofillCountry::CountryCodeForLocale(
                                    g_browser_process->GetApplicationLocale())
                              : ConvertJavaStringToUTF8(env, jcountry_code);

  return ::i18n::phonenumbers::PhoneNumberUtil::GetInstance()
      ->IsPossibleNumberForString(phone_number, country_code);
}

}  // namespace autofill
