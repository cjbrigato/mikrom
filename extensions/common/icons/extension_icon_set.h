// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_COMMON_ICONS_EXTENSION_ICON_SET_H_
#define EXTENSIONS_COMMON_ICONS_EXTENSION_ICON_SET_H_

#include <set>
#include <string>
#include <string_view>

#include "base/containers/flat_map.h"

namespace base {
class FilePath;
}

// Represents the set of icons for an extension.
class ExtensionIconSet {
 public:
  // Get an icon from the set, optionally falling back to a smaller or bigger
  // size. Match is exclusive (do not OR them together).
  enum class Match {
    kExactly,
    kBigger,
    kSmaller,
  };

  // Access to the underlying map from icon size->{path, bitmap}.
  using IconMap = base::flat_map<int, std::string>;

  ExtensionIconSet();
  ExtensionIconSet(const ExtensionIconSet& other);
  ~ExtensionIconSet();

  const IconMap& map() const { return map_; }
  bool empty() const { return map_.empty(); }

  // Remove all icons from the set.
  void Clear();

  // Add an icon path to the set. If a path for the specified size_in_px is
  // already present, it is overwritten.
  void Add(int size_in_px, const std::string& path);

  // Gets path value of the icon found when searching for `size_in_px` using
  // `match_type`.
  const std::string& Get(int size_in_px, Match match_type) const;

  // Returns true iff the set contains the specified path.
  bool ContainsPath(std::string_view path) const;

  // Returns icon size (in pixels) if the set contains the specified path or 0
  // if not found.
  int GetIconSizeFromPath(std::string_view path) const;

  // Add the paths of all icons in this set into `paths`, handling the
  // conversion of (string) -> (base::FilePath). Note that these paths are not
  // validated in any way, so they may be invalid paths or reference
  // nonexistent files.
  void GetPaths(std::set<base::FilePath>* paths) const;

 private:
  IconMap map_;
};

#endif  // EXTENSIONS_COMMON_ICONS_EXTENSION_ICON_SET_H_
