// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buildtools/checkdeps/testdata/disallowed/teststuff/bad.h"

// Test that directories outside of base_directory do not cause issues.
#include "/does_not_exist.h"
