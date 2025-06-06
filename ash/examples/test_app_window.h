// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_EXAMPLES_TEST_APP_WINDOW_H_
#define ASH_EXAMPLES_TEST_APP_WINDOW_H_

namespace ash {

// Open a test window that whose capabilities, such as resizability, can be
// modified. If `use_client_controlled_state` is true, it create a window with
// client controlled state, which updates its state asynchronously like ARC++
// (but not exactly the same).
void OpenTestAppWindow(bool use_client_controlled_state);

}  // namespace ash

#endif  // ASH_EXAMPLES_TEST_APP_WINDOW_H_
