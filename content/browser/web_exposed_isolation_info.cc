// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/web_exposed_isolation_info.h"

#include <ostream>

#include "base/notreached.h"

namespace content {

// static
WebExposedIsolationInfo WebExposedIsolationInfo::CreateNonIsolated() {
  return WebExposedIsolationInfo(std::nullopt /* origin */,
                                 false /* isolated_application */);
}

WebExposedIsolationInfo WebExposedIsolationInfo::CreateIsolated(
    const url::Origin& origin) {
  return WebExposedIsolationInfo(origin, false /* isolated_application */);
}

WebExposedIsolationInfo WebExposedIsolationInfo::CreateIsolatedApplication(
    const url::Origin& origin) {
  return WebExposedIsolationInfo(origin, true /* isolated_application */);
}

bool WebExposedIsolationInfo::AreCompatible(const WebExposedIsolationInfo& a,
                                            const WebExposedIsolationInfo& b) {
  return a == b;
}

bool WebExposedIsolationInfo::AreCompatible(
    const std::optional<WebExposedIsolationInfo>& a,
    const WebExposedIsolationInfo& b) {
  if (!a.has_value())
    return true;
  return AreCompatible(a.value(), b);
}

bool WebExposedIsolationInfo::AreCompatible(
    const WebExposedIsolationInfo& a,
    const std::optional<WebExposedIsolationInfo>& b) {
  return AreCompatible(b, a);
}

bool WebExposedIsolationInfo::AreCompatible(
    const std::optional<WebExposedIsolationInfo>& a,
    const std::optional<WebExposedIsolationInfo>& b) {
  if (!a.has_value() || !b.has_value())
    return true;
  return AreCompatible(a.value(), b.value());
}

WebExposedIsolationInfo::WebExposedIsolationInfo(
    const std::optional<url::Origin>& origin,
    bool isolated_application)
    : origin_(origin), isolated_application_(isolated_application) {}

WebExposedIsolationInfo::WebExposedIsolationInfo(
    const WebExposedIsolationInfo& other) = default;

WebExposedIsolationInfo::~WebExposedIsolationInfo() = default;

const url::Origin& WebExposedIsolationInfo::origin() const {
  DCHECK(is_isolated())
      << "The origin() getter should only be used for "
         "WebExposedIsolationInfo's where is_isolated() is true.";
  return origin_.value();
}

bool WebExposedIsolationInfo::operator==(
    const WebExposedIsolationInfo& b) const {
  if (is_isolated_application() != b.is_isolated_application())
    return false;

  if (is_isolated() != b.is_isolated())
    return false;

  if (is_isolated() && !(origin_->IsSameOriginWith(b.origin())))
    return false;

  return true;
}

bool WebExposedIsolationInfo::operator<(
    const WebExposedIsolationInfo& b) const {
  // Nonisolated < Isolated < Isolated Application.
  if (is_isolated_application() != b.is_isolated_application())
    return !is_isolated_application();

  if (is_isolated() != b.is_isolated())
    return !is_isolated();

  // Within Isolated and Isolated Application, compare origins:
  if (is_isolated()) {
    DCHECK(b.is_isolated());
    return origin_.value() < b.origin();
  }

  // Nonisolated == Nonisolated.
  DCHECK(!is_isolated() && !b.is_isolated());
  return false;
}

void WebExposedIsolationInfo::WriteIntoTrace(
    perfetto::TracedProto<TraceProto> proto) const {
  proto->set_is_isolated(is_isolated());
  if (is_isolated()) {
    proto->set_origin(origin_->GetDebugString());
  }
  proto->set_is_isolated_application(is_isolated_application());
}

std::ostream& operator<<(std::ostream& out,
                         const WebExposedIsolationInfo& info) {
  out << "{";
  if (info.is_isolated()) {
    out << info.origin();
    if (info.is_isolated_application())
      out << " (application)";
  }
  out << "}";
  return out;
}

}  // namespace content
