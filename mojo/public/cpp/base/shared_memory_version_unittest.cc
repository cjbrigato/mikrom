// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "mojo/public/cpp/base/shared_memory_version.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace {

TEST(SharedMemoryVersionTest, InitialSetupAllowsVersionSharing) {
  mojo::SharedMemoryVersionController controller;
  EXPECT_EQ(controller.GetSharedVersion(),
            mojo::shared_memory_version::kInitialVersion);
  mojo::SharedMemoryVersionClient client(controller.GetSharedMemoryRegion());

  // Shared version is equal to `kInitialVersion`
  EXPECT_FALSE(client.SharedVersionIsGreaterThan(
      mojo::shared_memory_version::kInitialVersion));
  EXPECT_FALSE(client.SharedVersionIsLessThan(
      mojo::shared_memory_version::kInitialVersion));

  // Comparing to `kInvalidVersion` always defaults to true.
  EXPECT_TRUE(client.SharedVersionIsGreaterThan(
      mojo::shared_memory_version::kInvalidVersion));
  EXPECT_TRUE(client.SharedVersionIsLessThan(
      mojo::shared_memory_version::kInvalidVersion));

  EXPECT_FALSE(client.CommittedWritesIsLessThan(0));
  EXPECT_TRUE(client.CommittedWritesIsLessThan(1));
}

TEST(SharedMemoryVersionTest, VersionIncrementsAreReflectedInClient) {
  mojo::SharedMemoryVersionController controller;
  mojo::SharedMemoryVersionClient client(controller.GetSharedMemoryRegion());

  mojo::VersionType local_version =
      mojo::shared_memory_version::kInitialVersion;

  // Remote version initially smaller then incremented.
  EXPECT_FALSE(client.SharedVersionIsGreaterThan(local_version));
  controller.Increment();
  EXPECT_TRUE(client.SharedVersionIsGreaterThan(local_version));

  // Local version catches up.
  ++local_version;
  EXPECT_FALSE(client.SharedVersionIsGreaterThan(local_version));
  EXPECT_FALSE(client.SharedVersionIsLessThan(local_version));

  // Local version overtakes remote version.
  ++local_version;
  EXPECT_TRUE(client.SharedVersionIsLessThan(local_version));
}

TEST(SharedMemoryVersionTest, CommittedWritesAreReflectedInClient) {
  mojo::SharedMemoryVersionController controller;
  mojo::SharedMemoryVersionClient client(controller.GetSharedMemoryRegion());

  mojo::VersionType required_committed_writes = 0;

  // Remote count up to date.
  EXPECT_FALSE(client.CommittedWritesIsLessThan(required_committed_writes));

  // Local count increases.
  ++required_committed_writes;
  EXPECT_TRUE(client.CommittedWritesIsLessThan(required_committed_writes));

  // Remote count catches up.
  controller.CommitWrite();
  EXPECT_FALSE(client.CommittedWritesIsLessThan(required_committed_writes));

  // More remote updates still satisfy the requirement.
  controller.CommitWrite();
  EXPECT_FALSE(client.CommittedWritesIsLessThan(required_committed_writes));
}

TEST(SharedMemoryVersionTest, ClientRemainsValidThroughControllerDestuction) {
  std::optional<mojo::SharedMemoryVersionController> controller;
  controller.emplace();
  controller->Increment();
  controller->CommitWrite();

  mojo::SharedMemoryVersionClient client(controller->GetSharedMemoryRegion());
  controller.reset();

  // Client is still valid past the lifetime of the controller.
  EXPECT_TRUE(client.SharedVersionIsGreaterThan(
      mojo::shared_memory_version::kInitialVersion));
  EXPECT_FALSE(client.SharedVersionIsLessThan(
      mojo::shared_memory_version::kInitialVersion));
  EXPECT_FALSE(client.CommittedWritesIsLessThan(1));
  EXPECT_TRUE(client.CommittedWritesIsLessThan(2));
}

}  // namespace
