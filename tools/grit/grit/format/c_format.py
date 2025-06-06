# Copyright 2012 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Formats as a .C file for compilation.
"""

import codecs
import os
import re

from grit import constants
from grit import util


def _FormatHeader(root, output_dir):
  """Returns the required preamble for C files."""
  # Find the location of the resource header file, so that we can include
  # it.
  resource_header = 'resource.h'  # fall back to this
  for output in root.GetOutputFiles():
    if output.attrs['type'] == 'rc_header':
      resource_header = os.path.abspath(output.GetOutputFilename())
      resource_header = util.MakeRelativePath(output_dir, resource_header)
  return """// This file is automatically generated by GRIT.  Do not edit.

#include "%s"

// All strings are UTF-8
""" % (resource_header)
# end _FormatHeader() function


def Format(root, lang='en', gender=None, output_dir='.'):
  """Outputs a C switch statement representing the string table."""
  assert gender is None, "c_format doesn't support gender translations, yet " \
      f"Format() was called with gender {gender}"

  from grit.node import message
  assert isinstance(lang, str)

  yield _FormatHeader(root, output_dir)

  yield 'const char* GetString(int id) {\n  switch (id) {'

  for item in root.ActiveDescendants():
    with item:
      if isinstance(item, message.MessageNode):
        yield _FormatMessage(item, lang)

  yield '\n    default:\n      return 0;\n  }\n}\n'


def _HexToOct(match):
  "Return the octal form of the hex numbers"
  hex = match.group("hex")
  result = ""
  while len(hex):
    next_num = int(hex[2:4], 16)
    result += "\\" + '%03o' % next_num
    hex = hex[4:]
  return match.group("escaped_backslashes") + result


def _FormatMessage(item, lang):
  """Format a single <message> element."""

  message = item.ws_at_start + item.Translate(
      lang, constants.DEFAULT_GENDER) + item.ws_at_end
  # Output message with non-ascii chars escaped as octal numbers C's grammar
  # allows escaped hexadecimal numbers to be infinite, but octal is always of
  # the form \OOO.  Python 3 doesn't support string-escape, so we have to jump
  # through some hoops here via codecs.escape_encode.
  # This basically does:
  #   - message - the starting string
  #   - message.encode(...) - convert to bytes
  #   - codecs.escape_encode(...) - convert non-ASCII bytes to \x## escapes
  #   - (...).decode() - convert bytes back to a string
  message = codecs.escape_encode(message.encode('utf-8'))[0].decode('utf-8')
  # an escaped char is (\xHH)+ but only if the initial
  # backslash is not escaped.
  not_a_backslash = r"(^|[^\\])"  # beginning of line or a non-backslash char
  escaped_backslashes = not_a_backslash + r"(\\\\)*"
  hex_digits = r"((\\x)[0-9a-f]{2})+"
  two_digit_hex_num = re.compile(
    r"(?P<escaped_backslashes>%s)(?P<hex>%s)"
    % (escaped_backslashes, hex_digits))
  message = two_digit_hex_num.sub(_HexToOct, message)
  # unescape \ (convert \\ back to \)
  message = message.replace('\\\\', '\\')
  message = message.replace('"', '\\"')
  message = util.LINEBREAKS.sub(r'\\n', message)

  name_attr = item.GetTextualIds()[0]

  return '\n    case %s:\n      return "%s";' % (name_attr, message)
