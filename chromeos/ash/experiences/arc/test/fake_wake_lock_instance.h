// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_ASH_EXPERIENCES_ARC_TEST_FAKE_WAKE_LOCK_INSTANCE_H_
#define CHROMEOS_ASH_EXPERIENCES_ARC_TEST_FAKE_WAKE_LOCK_INSTANCE_H_

#include "chromeos/ash/experiences/arc/mojom/wake_lock.mojom.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace arc {

class FakeWakeLockInstance : public mojom::WakeLockInstance {
 public:
  FakeWakeLockInstance();

  FakeWakeLockInstance(const FakeWakeLockInstance&) = delete;
  FakeWakeLockInstance& operator=(const FakeWakeLockInstance&) = delete;

  ~FakeWakeLockInstance() override;

  // mojom::WakeLockInstance overrides:
  void Init(mojo::PendingRemote<mojom::WakeLockHost> host_remote,
            InitCallback callback) override;

 private:
  mojo::Remote<mojom::WakeLockHost> host_remote_;
};

}  // namespace arc

#endif  // CHROMEOS_ASH_EXPERIENCES_ARC_TEST_FAKE_WAKE_LOCK_INSTANCE_H_
