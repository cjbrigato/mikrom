// Copyright 2009 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/containers/linked_list.h"

#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/strings/stringprintf.h"
#include "base/test/gtest_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace {

class Node : public LinkNode<Node> {
 public:
  explicit Node(int id) : id_(id) {}

  int id() const { return id_; }

 private:
  int id_;
};

class MultipleInheritanceNodeBase {
 public:
  MultipleInheritanceNodeBase() = default;
  int field_taking_up_space_ = 0;
};

class MultipleInheritanceNode : public MultipleInheritanceNodeBase,
                                public LinkNode<MultipleInheritanceNode> {
 public:
  MultipleInheritanceNode() = default;
};

class MovableNode : public LinkNode<MovableNode> {
 public:
  explicit MovableNode(int id) : id_(id) {}

  MovableNode(MovableNode&&) = default;

  int id() const { return id_; }

 private:
  int id_;
};

// Checks that when iterating |list| (either from head to tail, or from
// tail to head, as determined by |forward|), we get back |node_ids|.
void ExpectListContentsForDirection(const LinkedList<Node>& list,
                                    base::span<const int> node_ids,
                                    bool forward) {
  size_t i = 0;
  for (const LinkNode<Node>* node = (forward ? list.head() : list.tail());
       node != list.end(); node = (forward ? node->next() : node->previous())) {
    ASSERT_LT(i, node_ids.size());
    EXPECT_EQ(node_ids[forward ? i : node_ids.size() - i - 1],
              node->value()->id());
    ++i;
  }
  EXPECT_EQ(node_ids.size(), i);
}

void ExpectListContents(const LinkedList<Node>& list,
                        base::span<const int> node_ids) {
  {
    SCOPED_TRACE("Iterating forward (from head to tail)");
    ExpectListContentsForDirection(list, node_ids, true);
  }
  {
    SCOPED_TRACE("Iterating backward (from tail to head)");
    ExpectListContentsForDirection(list, node_ids, false);
  }
}

TEST(LinkedList, Empty) {
  LinkedList<Node> list;
  EXPECT_EQ(list.end(), list.head());
  EXPECT_EQ(list.end(), list.tail());
  ExpectListContents(list, {});
}

TEST(LinkedList, Append) {
  LinkedList<Node> list;
  ExpectListContents(list, {});

  Node n1(1);
  list.Append(&n1);

  EXPECT_EQ(&n1, list.head());
  EXPECT_EQ(&n1, list.tail());
  {
    const int expected[] = {1};
    ExpectListContents(list, expected);
  }

  Node n2(2);
  list.Append(&n2);

  EXPECT_EQ(&n1, list.head());
  EXPECT_EQ(&n2, list.tail());
  {
    const int expected[] = {1, 2};
    ExpectListContents(list, expected);
  }

  Node n3(3);
  list.Append(&n3);

  EXPECT_EQ(&n1, list.head());
  EXPECT_EQ(&n3, list.tail());
  {
    const int expected[] = {1, 2, 3};
    ExpectListContents(list, expected);
  }
}

TEST(LinkedList, RemoveFromList) {
  LinkedList<Node> list;

  Node n1(1);
  Node n2(2);
  Node n3(3);
  Node n4(4);
  Node n5(5);

  list.Append(&n1);
  list.Append(&n2);
  list.Append(&n3);
  list.Append(&n4);
  list.Append(&n5);

  EXPECT_EQ(&n1, list.head());
  EXPECT_EQ(&n5, list.tail());
  {
    const int expected[] = {1, 2, 3, 4, 5};
    ExpectListContents(list, expected);
  }

  // Remove from the middle.
  n3.RemoveFromList();

  EXPECT_EQ(&n1, list.head());
  EXPECT_EQ(&n5, list.tail());
  {
    const int expected[] = {1, 2, 4, 5};
    ExpectListContents(list, expected);
  }

  // Remove from the tail.
  n5.RemoveFromList();

  EXPECT_EQ(&n1, list.head());
  EXPECT_EQ(&n4, list.tail());
  {
    const int expected[] = {1, 2, 4};
    ExpectListContents(list, expected);
  }

  // Remove from the head.
  n1.RemoveFromList();

  EXPECT_EQ(&n2, list.head());
  EXPECT_EQ(&n4, list.tail());
  {
    const int expected[] = {2, 4};
    ExpectListContents(list, expected);
  }

  // Empty the list.
  n2.RemoveFromList();
  n4.RemoveFromList();

  ExpectListContents(list, {});
  EXPECT_EQ(list.end(), list.head());
  EXPECT_EQ(list.end(), list.tail());

  // Fill the list once again.
  list.Append(&n1);
  list.Append(&n2);
  list.Append(&n3);
  list.Append(&n4);
  list.Append(&n5);

  EXPECT_EQ(&n1, list.head());
  EXPECT_EQ(&n5, list.tail());
  {
    const int expected[] = {1, 2, 3, 4, 5};
    ExpectListContents(list, expected);
  }
}

TEST(LinkedList, InsertBefore) {
  LinkedList<Node> list;

  Node n1(1);
  Node n2(2);
  Node n3(3);
  Node n4(4);

  list.Append(&n1);
  list.Append(&n2);

  EXPECT_EQ(&n1, list.head());
  EXPECT_EQ(&n2, list.tail());
  {
    const int expected[] = {1, 2};
    ExpectListContents(list, expected);
  }

  n3.InsertBefore(&n2);

  EXPECT_EQ(&n1, list.head());
  EXPECT_EQ(&n2, list.tail());
  {
    const int expected[] = {1, 3, 2};
    ExpectListContents(list, expected);
  }

  n4.InsertBefore(&n1);

  EXPECT_EQ(&n4, list.head());
  EXPECT_EQ(&n2, list.tail());
  {
    const int expected[] = {4, 1, 3, 2};
    ExpectListContents(list, expected);
  }
}

TEST(LinkedList, InsertAfter) {
  LinkedList<Node> list;

  Node n1(1);
  Node n2(2);
  Node n3(3);
  Node n4(4);

  list.Append(&n1);
  list.Append(&n2);

  EXPECT_EQ(&n1, list.head());
  EXPECT_EQ(&n2, list.tail());
  {
    const int expected[] = {1, 2};
    ExpectListContents(list, expected);
  }

  n3.InsertAfter(&n2);

  EXPECT_EQ(&n1, list.head());
  EXPECT_EQ(&n3, list.tail());
  {
    const int expected[] = {1, 2, 3};
    ExpectListContents(list, expected);
  }

  n4.InsertAfter(&n1);

  EXPECT_EQ(&n1, list.head());
  EXPECT_EQ(&n3, list.tail());
  {
    const int expected[] = {1, 4, 2, 3};
    ExpectListContents(list, expected);
  }
}

TEST(LinkedList, MultipleInheritanceNode) {
  MultipleInheritanceNode node;
  EXPECT_EQ(&node, node.value());
}

TEST(LinkedList, EmptyListIsEmpty) {
  LinkedList<Node> list;
  EXPECT_TRUE(list.empty());
}

TEST(LinkedList, NonEmptyListIsNotEmpty) {
  LinkedList<Node> list;

  Node n(1);
  list.Append(&n);

  EXPECT_FALSE(list.empty());
}

TEST(LinkedList, EmptiedListIsEmptyAgain) {
  LinkedList<Node> list;

  Node n(1);
  list.Append(&n);
  n.RemoveFromList();

  EXPECT_TRUE(list.empty());
}

TEST(LinkedList, NodesCanBeReused) {
  LinkedList<Node> list1;
  LinkedList<Node> list2;

  Node n(1);
  list1.Append(&n);
  n.RemoveFromList();
  list2.Append(&n);

  EXPECT_EQ(list2.head()->value(), &n);
}

TEST(LinkedList, RemovedNodeHasNullNextPrevious) {
  LinkedList<Node> list;

  Node n(1);
  list.Append(&n);
  n.RemoveFromList();

  EXPECT_EQ(nullptr, n.next());
  EXPECT_EQ(nullptr, n.previous());
}

TEST(LinkedList, NodeMoveConstructor) {
  LinkedList<MovableNode> list;

  MovableNode n1(1);
  MovableNode n2(2);
  MovableNode n3(3);

  list.Append(&n1);
  list.Append(&n2);
  list.Append(&n3);

  EXPECT_EQ(&n1, n2.previous());
  EXPECT_EQ(&n2, n1.next());
  EXPECT_EQ(&n3, n2.next());
  EXPECT_EQ(&n2, n3.previous());
  EXPECT_EQ(2, n2.id());

  MovableNode n2_new(std::move(n2));

  EXPECT_EQ(nullptr, n2.next());      // NOLINT(bugprone-use-after-move)
  EXPECT_EQ(nullptr, n2.previous());  // NOLINT(bugprone-use-after-move)

  EXPECT_EQ(&n1, n2_new.previous());
  EXPECT_EQ(&n2_new, n1.next());
  EXPECT_EQ(&n3, n2_new.next());
  EXPECT_EQ(&n2_new, n3.previous());
  EXPECT_EQ(2, n2_new.id());
}

TEST(LinkedList, LinkedListMoveConstructor) {
  // Moving list sizes 0 (head==end), 1 (head==tail), 2 (head!=tail) all stress
  // different cases. Also test size 3 in case it does something weird.
  for (size_t size = 0; size <= 3; ++size) {
    SCOPED_TRACE(StringPrintf("List size %zu", size));
    LinkedList<Node> original_list;

    std::vector<Node> nodes;
    std::vector<int> expected_contents;
    nodes.reserve(size);
    for (int id = 0; id < size; ++id) {
      nodes.emplace_back(id);
      original_list.Append(&nodes.back());
      expected_contents.push_back(id);
    }

    LinkedList<Node> new_list = std::move(original_list);

    ExpectListContents(new_list, expected_contents);

    EXPECT_TRUE(original_list.empty());  // NOLINT(bugprone-use-after-move)
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_EQ(original_list.head(), original_list.tail());
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_EQ(original_list.tail(), original_list.end());
  }
}

TEST(LinkedListDeathTest, ChecksOnInsertBeforeWhenInList) {
  LinkedList<Node> list1;
  LinkedList<Node> list2;

  Node n1(1);
  Node n2(2);
  Node n3(3);

  list1.Append(&n1);

  list2.Append(&n2);
  list2.Append(&n3);

  EXPECT_CHECK_DEATH(n1.InsertBefore(&n3));
}

TEST(LinkedListDeathTest, ChecksOnInsertAfterWhenInList) {
  LinkedList<Node> list1;
  LinkedList<Node> list2;

  Node n1(1);
  Node n2(2);
  Node n3(3);

  list1.Append(&n1);

  list2.Append(&n2);
  list2.Append(&n3);

  EXPECT_CHECK_DEATH(n1.InsertAfter(&n2));
}

}  // namespace
}  // namespace base
