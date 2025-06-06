# Copyright 2014 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

from mojom.parse import ast

class _TestNode(ast.NodeBase):
  """Node type for tests."""

  def __init__(self, value, **kwargs):
    super().__init__(**kwargs)
    self.value = value

  def __eq__(self, other):
    return super().__eq__(other) and self.value == other.value

class _TestNodeList(ast.NodeListBase):
  """Node list type for tests."""

  _list_item_type = _TestNode

class ASTTest(unittest.TestCase):
  """Tests various AST classes."""

  def testNodeBase(self):
    # Test |__eq__()|; this is only used for testing, where we want to do
    # comparison by value and ignore filenames/line numbers (for convenience).
    node1 = ast.NodeBase(filename="hello.mojom")
    node1.start = ast.Location(45, 100)
    node2 = ast.NodeBase()
    self.assertEqual(node1, node2)
    self.assertEqual(node2, node1)

    # Check that |__ne__()| just defers to |__eq__()| properly.
    self.assertFalse(node1 != node2)
    self.assertFalse(node2 != node1)

    # Check that |filename| and lex locations are set properly (and are None by
    # default).
    self.assertEqual(node1.filename, "hello.mojom")
    self.assertEqual(node1.start.line, 45)
    self.assertEqual(node1.start.line, 45)
    self.assertEqual(node1.end.lexpos, 0)
    self.assertEqual(node1.end.lexpos, 0)
    self.assertIsNone(node2.filename)
    self.assertEqual(node2.start.line, 0)
    self.assertEqual(node2.start.lexpos, 0)
    self.assertEqual(node2.end.line, 0)
    self.assertEqual(node2.end.lexpos, 0)

    # |NodeBase|'s |__eq__()| should compare types (and a subclass's |__eq__()|
    # should first defer to its superclass's).
    node3 = _TestNode(123)
    self.assertNotEqual(node1, node3)
    self.assertNotEqual(node3, node1)
    # Also test |__eq__()| directly.
    self.assertFalse(node1 == node3)
    self.assertFalse(node3 == node1)

    node4 = _TestNode(123, filename="world.mojom")
    node4.start = ast.Location(123, 555)
    self.assertEqual(node4, node3)
    node5 = _TestNode(456)
    self.assertNotEqual(node5, node4)

  def testNodeListBase(self):
    node1 = _TestNode(1, filename="foo.mojom")
    node1.start = ast.Location(1, 1)
    # Equal to, but not the same as, |node1|:
    node1b = _TestNode(1, filename="foo.mojom")
    node1b.start = ast.Location(1, 1)
    node2 = _TestNode(2, filename="foo.mojom")
    node2.start = ast.Location(2, 2)

    nodelist1 = _TestNodeList()  # Contains: (empty).
    self.assertEqual(nodelist1, nodelist1)
    self.assertEqual(nodelist1.items, [])
    self.assertIsNone(nodelist1.filename)
    self.assertEqual(nodelist1.start.line, 0)

    nodelist2 = _TestNodeList(node1)  # Contains: 1.
    self.assertEqual(nodelist2, nodelist2)
    self.assertEqual(nodelist2.items, [node1])
    self.assertNotEqual(nodelist2, nodelist1)
    self.assertEqual(nodelist2.filename, "foo.mojom")
    self.assertEqual(nodelist2.start.line, 1)

    nodelist3 = _TestNodeList([node2])  # Contains: 2.
    self.assertEqual(nodelist3.items, [node2])
    self.assertNotEqual(nodelist3, nodelist1)
    self.assertNotEqual(nodelist3, nodelist2)
    self.assertEqual(nodelist3.filename, "foo.mojom")
    self.assertEqual(nodelist3.start.line, 2)

    nodelist1.Append(node1b)  # Contains: 1.
    self.assertEqual(nodelist1.items, [node1])
    self.assertEqual(nodelist1, nodelist2)
    self.assertNotEqual(nodelist1, nodelist3)
    self.assertEqual(nodelist1.filename, "foo.mojom")
    self.assertEqual(nodelist1.start.line, 1)

    nodelist1.Append(node2)  # Contains: 1, 2.
    self.assertEqual(nodelist1.items, [node1, node2])
    self.assertNotEqual(nodelist1, nodelist2)
    self.assertNotEqual(nodelist1, nodelist3)
    self.assertEqual(nodelist1.start.line, 1)

    nodelist2.Append(node2)  # Contains: 1, 2.
    self.assertEqual(nodelist2.items, [node1, node2])
    self.assertEqual(nodelist2, nodelist1)
    self.assertNotEqual(nodelist2, nodelist3)
    self.assertEqual(nodelist2.start.line, 1)

    nodelist3.Insert(node1)  # Contains: 1, 2.
    self.assertEqual(nodelist3.items, [node1, node2])
    self.assertEqual(nodelist3, nodelist1)
    self.assertEqual(nodelist3, nodelist2)
    self.assertEqual(nodelist3.start.line, 1)

    # Test iteration:
    i = 1
    for item in nodelist1:
      self.assertEqual(item.value, i)
      i += 1
