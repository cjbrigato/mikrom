/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2003, 2010 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "third_party/blink/renderer/core/html/html_pre_element.h"

#include "third_party/blink/renderer/core/css/css_property_names.h"
#include "third_party/blink/renderer/core/css/css_property_value_set.h"
#include "third_party/blink/renderer/core/css_value_keywords.h"
#include "third_party/blink/renderer/core/html_names.h"

namespace blink {

HTMLPreElement::HTMLPreElement(const QualifiedName& tag_name,
                               Document& document)
    : HTMLElement(tag_name, document) {}

bool HTMLPreElement::IsPresentationAttribute(const QualifiedName& name) const {
  if (name == html_names::kWrapAttr)
    return true;
  return HTMLElement::IsPresentationAttribute(name);
}

void HTMLPreElement::CollectStyleForPresentationAttribute(
    const QualifiedName& name,
    const AtomicString& value,
    HeapVector<CSSPropertyValue, 8>& style) {
  if (name == html_names::kWrapAttr) {
    // Longhands of `white-space: pre-wrap`.
    AddPropertyToPresentationAttributeStyle(
        style, CSSPropertyID::kWhiteSpaceCollapse, CSSValueID::kPreserve);
    AddPropertyToPresentationAttributeStyle(style, CSSPropertyID::kTextWrapMode,
                                            CSSValueID::kWrap);
  } else {
    HTMLElement::CollectStyleForPresentationAttribute(name, value, style);
  }
}

}  // namespace blink
