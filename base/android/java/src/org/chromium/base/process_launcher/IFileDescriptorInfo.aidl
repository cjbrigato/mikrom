// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.process_launcher;

parcelable IFileDescriptorInfo {
  int id;
  ParcelFileDescriptor fd;
  long offset;
  long size;
}
