// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.app;

import org.chromium.build.annotations.NullMarked;

/**
 * This is needed to register multiple PrivilegedProcess services so that we can have more than one
 * unsandboxed process.
 */
@NullMarked
public class PrivilegedProcessService9 extends PrivilegedProcessService {}
