// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/content_settings/core/common/content_settings_metadata.h"

#include <stddef.h>

#include <tuple>

#include "base/time/clock.h"
#include "base/time/time.h"
#include "components/content_settings/core/common/content_settings_constraints.h"

namespace content_settings {

RuleMetaData::RuleMetaData() = default;
RuleMetaData::RuleMetaData(RuleMetaData&& other) = default;

RuleMetaData RuleMetaData::Clone() const {
  RuleMetaData clone;
  clone.last_modified_ = last_modified_;
  clone.last_used_ = last_used_;
  clone.last_visited_ = last_visited_;
  clone.expiration_ = expiration_;
  clone.session_model_ = session_model_;
  clone.lifetime_ = lifetime_;
  clone.tpcd_metadata_rule_source_ = tpcd_metadata_rule_source_;
  clone.tpcd_metadata_cohort_ = tpcd_metadata_cohort_;
  clone.tpcd_metadata_elected_dtrp_ = tpcd_metadata_elected_dtrp_;
  clone.decided_by_related_website_sets_ = decided_by_related_website_sets_;
  clone.rule_options_ = rule_options_.Clone();
  return clone;
}

RuleMetaData& RuleMetaData::operator=(RuleMetaData&& other) = default;

void RuleMetaData::SetFromConstraints(
    const ContentSettingConstraints& constraints) {
  session_model_ = constraints.session_model();
  decided_by_related_website_sets_ =
      constraints.decided_by_related_website_sets();
  SetExpirationAndLifetime(constraints.expiration(), constraints.lifetime());
  rule_options_ = constraints.options().Clone();
}

void RuleMetaData::SetExpirationAndLifetime(base::Time expiration,
                                            base::TimeDelta lifetime) {
  CHECK_EQ(lifetime.is_zero(), expiration.is_null());
  CHECK_GE(lifetime, base::TimeDelta());
  expiration_ = expiration;
  lifetime_ = lifetime;
}

bool RuleMetaData::IsExpired(const base::Clock* clock) const {
  return !expiration().is_null() && expiration() <= clock->Now();
}

bool RuleMetaData::operator==(const RuleMetaData& other) const = default;

// static
base::TimeDelta RuleMetaData::ComputeLifetime(base::TimeDelta lifetime,
                                              base::Time expiration) {
  if (!lifetime.is_zero() && expiration.is_null()) {
    // This is an invalid state, since:
    // * old writes would have written a nonzero expiration and zero lifetime or
    // zero expiration and zero lifetime;
    // * new writes would write a nonzero expiration and nonzero lifetime or
    // zero expiration and zero lifetime.
    //
    // Yet when we read from disk, we got a zero expiration and nonzero
    // lifetime. This indicates disk corruption: the expiration field is either
    // missing or mangled. We therefore defer to the expiration, and give up on
    // the lifetime. This has the effect that temporary settings may become
    // permanent in the event of disk corruption; but this has always been the
    // case.
    return base::TimeDelta();
  }

  if (expiration.is_null()) {
    return base::TimeDelta();
  }
  if (!lifetime.is_zero()) {
    return lifetime;
  }

  lifetime = expiration - base::Time::Now();
  if (lifetime < base::TimeDelta()) {
    // Not setting to zero, since a non-null expiration doesn't make sense with
    // a zero lifetime.
    lifetime = base::Microseconds(1);
  }
  return lifetime;
}

}  // namespace content_settings
