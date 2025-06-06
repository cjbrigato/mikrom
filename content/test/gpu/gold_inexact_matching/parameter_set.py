# Copyright 2020 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import dataclasses
import typing


@dataclasses.dataclass
class ParameterSet():
  """Struct-like object for holding parameters for an iteration."""

  # The maximum number of pixels that are allowed to differ.
  max_diff: int
  # The maximum per-channel delta sum that is allowed.
  delta_threshold: int
  # The threshold for what is considered an edge for a Sobel filter.
  edge_threshold: int
  # This parameter is not varied, so it is set statically once instead of being
  # passed around everywhere.
  ignored_border_thickness: int = 0

  def AsList(self) -> typing.List[str]:
    """Returns the object's data in list format.

    The returned object is suitable for appending to a "goldctl match" command
    in order to compare using the parameters stored within the object.

    Returns:
      A list of strings.
    """
    return [
        '--parameter',
        f'fuzzy_max_different_pixels:{self.max_diff}',
        '--parameter',
        f'fuzzy_pixel_delta_threshold:{self.delta_threshold}',
        '--parameter',
        f'fuzzy_ignored_border_thickness:{self.ignored_border_thickness}',
        '--parameter',
        f'sobel_edge_threshold:{self.edge_threshold}',
    ]

  def __str__(self) -> str:
    return (f'Max different pixels: {self.max_diff}, '
            f'Max per-channel delta sum: {self.delta_threshold}, '
            f'Sobel edge threshold: {self.edge_threshold}, '
            f'Ignored border thickness: {self.ignored_border_thickness}')

  def __eq__(self, other: 'ParameterSet') -> bool:
    return (self.max_diff == other.max_diff
            and self.delta_threshold == other.delta_threshold
            and self.edge_threshold == other.edge_threshold
            and self.ignored_border_thickness == other.ignored_border_thickness)

  def __ne__(self, other: 'ParameterSet') -> bool:
    return not self.__eq__(other)

  def __hash__(self) -> int:
    return hash((self.max_diff, self.delta_threshold, self.edge_threshold,
                 self.ignored_border_thickness))
