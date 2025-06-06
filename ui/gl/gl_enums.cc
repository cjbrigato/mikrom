// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gl/gl_enums.h"

#include <GLES2/gl2.h>

#include <sstream>

#include "ui/gl/gl_enums_implementation_autogen.h"

namespace gl {

std::string GLEnums::GetStringEnum(uint32_t value) {
  for (const auto& entry : kEnumToStringTable) {
    if (value == entry.value) {
      return std::string(entry.name);
    }
  }
  std::stringstream ss;
  ss.fill('0');
  ss.width(value < 0x10000 ? 4 : 8);
  ss << std::hex << value;
  return "0x" + ss.str();
}

std::string GLEnums::GetStringError(uint32_t value) {
  if (value == GL_NONE)
    return "GL_NONE";
  return GetStringEnum(value);
}

std::string GLEnums::GetStringBool(uint32_t value) {
  return value ? "GL_TRUE" : "GL_FALSE";
}

}  // namespace gl
