#!/usr/bin/env python3
# Copyright 2018 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Updates the FeaturePolicyFeature enum in enums.xml file with
values read from permissions_policy_feature.mojom.

If the file was pretty-printed, the updated version is pretty-printed too.
"""

from __future__ import print_function

import os
import sys

from update_histogram_enum import UpdateHistogramEnum

if __name__ == '__main__':
  if len(sys.argv) > 1:
    print('No arguments expected!', file=sys.stderr)
    sys.stderr.write(__doc__)
    sys.exit(1)

  source_file = 'services/network/public/mojom/permissions_policy/' \
                'permissions_policy_feature.mojom'
  UpdateHistogramEnum('tools/metrics/histograms/metadata/blink/enums.xml',
                      histogram_enum_name='FeaturePolicyFeature',
                      source_enum_path=source_file,
                      start_marker='^enum PermissionsPolicyFeature {',
                      end_marker='^};',
                      strip_k_prefix=True,
                      calling_script=os.path.basename(__file__))
