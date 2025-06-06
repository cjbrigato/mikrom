// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/animation/property_handle.h"

#include "third_party/blink/renderer/platform/wtf/text/atomic_string_hash.h"

namespace blink {

bool PropertyHandle::operator==(const PropertyHandle& other) const {
  if (handle_type_ != other.handle_type_)
    return false;

  switch (handle_type_) {
    case kHandleCSSProperty:
      return css_property_ == other.css_property_;
    case kHandleCSSCustomProperty:
      return property_name_ == other.property_name_;
    default:
      return true;
  }
}

unsigned PropertyHandle::GetHash() const {
  switch (handle_type_) {
    case kHandleCSSProperty:
      return static_cast<int>(css_property_->PropertyID());
    case kHandleCSSCustomProperty:
      return WTF::GetHash(property_name_);
    default:
      NOTREACHED();
  }
}

}  // namespace blink
