// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_FORM_PARSING_EMAIL_FIELD_PARSER_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_FORM_PARSING_EMAIL_FIELD_PARSER_H_

#include <memory>

#include "base/compiler_specific.h"
#include "base/memory/raw_ptr.h"
#include "components/autofill/core/browser/country_type.h"
#include "components/autofill/core/browser/form_parsing/form_field_parser.h"
#include "components/autofill/core/common/language_code.h"

namespace autofill {

class EmailFieldParser : public FormFieldParser {
 public:
  static std::unique_ptr<FormFieldParser> Parse(ParsingContext& context,
                                                AutofillScanner* scanner);
  explicit EmailFieldParser(FieldAndMatchInfo match, FieldType email_type);

  EmailFieldParser(const EmailFieldParser&) = delete;
  EmailFieldParser& operator=(const EmailFieldParser&) = delete;

 protected:
  void AddClassifications(FieldCandidatesMap& field_candidates) const override;

 private:
  FieldAndMatchInfo match_;
  // Email related types e.g. email-only or "email or loyalty card" fields.
  // In particular, it can not have the joint email address/username type as
  // value.
  const FieldType email_type_;
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_FORM_PARSING_EMAIL_FIELD_PARSER_H_
