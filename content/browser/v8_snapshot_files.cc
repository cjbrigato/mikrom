// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/v8_snapshot_files.h"

#include <variant>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "build/build_config.h"
#include "content/public/common/content_descriptor_keys.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "tools/v8_context_snapshot/buildflags.h"

namespace content {

#if BUILDFLAG(IS_ANDROID) && BUILDFLAG(USE_V8_CONTEXT_SNAPSHOT)
namespace {
void registerContextSnapshotAndroid(
    std::map<std::string, std::variant<base::FilePath, base::ScopedFD>>&
        files) {
#ifdef __LP64__
  files[kV8ContextSnapshot64DataDescriptor] =
#else
  files[kV8ContextSnapshot32DataDescriptor] =
#endif  // __LP64__
      base::FilePath(FILE_PATH_LITERAL("assets"))
          .Append(FILE_PATH_LITERAL(BUILDFLAG(V8_CONTEXT_SNAPSHOT_FILENAME)));
}
}  // namespace
#endif  // BUILDFLAG(IS_ANDROID)

std::map<std::string, std::variant<base::FilePath, base::ScopedFD>>
GetV8SnapshotFilesToPreload(base::CommandLine& process_command_line) {
  std::map<std::string, std::variant<base::FilePath, base::ScopedFD>> files;
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
#if BUILDFLAG(USE_V8_CONTEXT_SNAPSHOT)
  files[kV8ContextSnapshotDataDescriptor] = base::FilePath(
      FILE_PATH_LITERAL(BUILDFLAG(V8_CONTEXT_SNAPSHOT_FILENAME)));
#else
  files[kV8SnapshotDataDescriptor] =
      base::FilePath(FILE_PATH_LITERAL("snapshot_blob.bin"));
#endif
#elif BUILDFLAG(IS_ANDROID)
#if !BUILDFLAG(USE_V8_CONTEXT_SNAPSHOT) || BUILDFLAG(INCLUDE_BOTH_V8_SNAPSHOTS)
#if BUILDFLAG(INCLUDE_BOTH_V8_SNAPSHOTS)
  if (base::FeatureList::IsEnabled(features::kUseContextSnapshot)) {
    process_command_line.AppendSwitch(switches::kUseContextSnapshotSwitch);
    registerContextSnapshotAndroid(files);
  }
#endif  // BUILDFLAG(INCLUDE_BOTH_V8_SNAPSHOTS)
#ifdef __LP64__
  files[kV8Snapshot64DataDescriptor] =
      base::FilePath(FILE_PATH_LITERAL("assets/snapshot_blob_64.bin"));
#else
  files[kV8Snapshot32DataDescriptor] =
      base::FilePath(FILE_PATH_LITERAL("assets/snapshot_blob_32.bin"));
#endif  // __LP64__
#elif BUILDFLAG(USE_V8_CONTEXT_SNAPSHOT)
  registerContextSnapshotAndroid(files);
#endif  // !BUILDFLAG(USE_V8_CONTEXT_SNAPSHOT) ||
        // BUILDFLAG(INCLUDE_BOTH_V8_SNAPSHOTS)
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  return files;
}

}  // namespace content
