// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/permissions/permission_indicators_tab_data.h"

#include "components/permissions/permission_uma_util.h"
#include "content/public/browser/web_contents.h"

namespace permissions {
constexpr int kMinElapsedTimeSinceLastUsage = 4;

PermissionIndicatorsTabData::PermissionIndicatorsTabData(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents) {
  DCHECK(web_contents);
  origin_ = web_contents->GetPrimaryMainFrame()->GetLastCommittedOrigin();
}

PermissionIndicatorsTabData::~PermissionIndicatorsTabData() = default;

bool PermissionIndicatorsTabData::IsVerboseIndicatorAllowed(
    IndicatorsType type) const {
  return !displayed_indicators_.contains(type);
}

void PermissionIndicatorsTabData::SetVerboseIndicatorDisplayed(
    IndicatorsType type) {
  displayed_indicators_.insert(type);
}

void PermissionIndicatorsTabData::RecordActivity(
    RequestTypeForUma request_type) {
  if (request_type != RequestTypeForUma::PERMISSION_GEOLOCATION &&
      request_type != RequestTypeForUma::PERMISSION_MEDIASTREAM_CAMERA &&
      request_type != RequestTypeForUma::PERMISSION_MEDIASTREAM_MIC) {
    return;
  }
  if (!last_usage_time_[request_type].has_value()) {
    return;
  }

  // Data with time interval less than 4 seconds is meaningless.
  const base::TimeDelta time_delta =
      base::TimeTicks::Now() - last_usage_time_[request_type].value();
  if (time_delta > base::Seconds(kMinElapsedTimeSinceLastUsage)) {
    PermissionUmaUtil::RecordPermissionIndicatorElapsedTimeSinceLastUsage(
        request_type, time_delta);
  }
  last_usage_time_[request_type] = base::TimeTicks::Now();
}

void PermissionIndicatorsTabData::OnMediaCaptureChanged(
    RequestTypeForUma request_type,
    bool used) {
  if (used) {
    RecordActivity(request_type);
  } else {
    last_usage_time_[request_type] = base::TimeTicks::Now();
  }
}

void PermissionIndicatorsTabData::OnCapabilityTypesChanged(
    content::WebContentsCapabilityType connection_type,
    bool used) {
  if (connection_type == content::WebContentsCapabilityType::kGeolocation) {
    if (used) {
      RecordActivity(RequestTypeForUma::PERMISSION_GEOLOCATION);
    } else {
      last_usage_time_[RequestTypeForUma::PERMISSION_GEOLOCATION] =
          base::TimeTicks::Now();
    }
  }
}

void PermissionIndicatorsTabData::ClearData() {
  last_usage_time_[RequestTypeForUma::PERMISSION_GEOLOCATION].reset();
  last_usage_time_[RequestTypeForUma::PERMISSION_MEDIASTREAM_MIC].reset();
  last_usage_time_[RequestTypeForUma::PERMISSION_MEDIASTREAM_CAMERA].reset();
  displayed_indicators_.clear();
}

void PermissionIndicatorsTabData::WebContentsDestroyed() {
  ClearData();
}

void PermissionIndicatorsTabData::PrimaryPageChanged(content::Page& page) {
  if (origin_ != page.GetMainDocument().GetLastCommittedOrigin()) {
    origin_ = page.GetMainDocument().GetLastCommittedOrigin();
    ClearData();
  }
}

}  // namespace permissions
