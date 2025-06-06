// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/shared_storage/util.h"

#include "base/feature_list.h"
#include "base/memory/scoped_refptr.h"
#include "components/aggregation_service/aggregation_coordinator_utils.h"
#include "services/network/public/cpp/features.h"
#include "services/network/public/mojom/permissions_policy/permissions_policy_feature.mojom-blink.h"
#include "services/network/public/mojom/shared_storage.mojom-blink.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/shared_storage/shared_storage_utils.h"
#include "third_party/blink/public/mojom/shared_storage/shared_storage.mojom-blink.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_throw_dom_exception.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_shared_storage_private_aggregation_config.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_shared_storage_run_operation_method_options.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/bindings/script_state.h"
#include "third_party/blink/renderer/platform/weborigin/security_origin.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

bool StringFromV8(v8::Isolate* isolate, v8::Local<v8::Value> val, String* out) {
  DCHECK(out);

  if (!val->IsString()) {
    return false;
  }

  *out = ToBlinkString<String>(isolate, v8::Local<v8::String>::Cast(val),
                               kDoNotExternalize);
  return true;
}

bool IsReservedLockName(const String& lock_name) {
  return lock_name.StartsWith('-');
}

bool IsValidSharedStorageBatchUpdateMethodsArgument(
    const Vector<
        network::mojom::blink::SharedStorageModifierMethodWithOptionsPtr>&
        methods_with_options) {
  if (!base::FeatureList::IsEnabled(
          network::features::kSharedStorageTransactionalBatchUpdate)) {
    return true;
  }

  for (const auto& method : methods_with_options) {
    if (method->with_lock) {
      return false;
    }
  }
  return true;
}

bool CheckBrowsingContextIsValid(ScriptState& script_state,
                                 ExceptionState& exception_state) {
  if (!script_state.ContextIsValid()) {
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidAccessError,
                                      "context is not valid");
    return false;
  }

  ExecutionContext* execution_context = ExecutionContext::From(&script_state);
  if (execution_context->IsContextDestroyed()) {
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidAccessError,
                                      "context has been destroyed");
    return false;
  }

  return true;
}

bool CheckSharedStoragePermissionsPolicy(ExecutionContext& execution_context,
                                         ExceptionState& exception_state) {
  // The worklet scope has to be created from the Window scope, thus the
  // shared-storage permissions policy feature must have been enabled. Besides,
  // the `SharedStorageWorkletGlobalScope` is currently given a null
  // `PermissionsPolicy`, so we shouldn't attempt to check the permissions
  // policy.
  //
  // TODO(crbug.com/1414951): When the `PermissionsPolicy` is properly set for
  // `SharedStorageWorkletGlobalScope`, we can remove this.
  if (execution_context.IsSharedStorageWorkletGlobalScope()) {
    return true;
  }

  if (!execution_context.IsFeatureEnabled(
          network::mojom::PermissionsPolicyFeature::kSharedStorage)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidAccessError,
        "The \"shared-storage\" Permissions Policy denied the method on "
        "window.sharedStorage.");
    return false;
  }

  return true;
}

bool CheckPrivateAggregationConfig(
    const SharedStorageRunOperationMethodOptions& options,
    ScriptState& script_state,
    ScriptPromiseResolverBase& resolver,
    mojom::blink::PrivateAggregationConfigPtr& out_private_aggregation_config) {
  out_private_aggregation_config = mojom::blink::PrivateAggregationConfig::New();

  WTF::String& out_context_id = out_private_aggregation_config->context_id;
  scoped_refptr<const SecurityOrigin>& out_aggregation_coordinator_origin =
      out_private_aggregation_config->aggregation_coordinator_origin;
  uint32_t& out_filtering_id_max_bytes =
      out_private_aggregation_config->filtering_id_max_bytes;

  out_filtering_id_max_bytes = kPrivateAggregationApiDefaultFilteringIdMaxBytes;

  if (!options.hasPrivateAggregationConfig()) {
    return true;
  }

  bool is_in_fenced_frame =
      ExecutionContext::From(&script_state)->IsInFencedFrame();

  if (options.privateAggregationConfig()->hasContextId()) {
    if (options.privateAggregationConfig()->contextId().length() >
        kPrivateAggregationApiContextIdMaxLength) {
      resolver.Reject(V8ThrowDOMException::CreateOrEmpty(
          script_state.GetIsolate(), DOMExceptionCode::kDataError,
          "contextId length cannot be larger than 64"));
      return false;
    }
    if (is_in_fenced_frame &&
        base::FeatureList::IsEnabled(
            features::kFencedFramesLocalUnpartitionedDataAccess)) {
      resolver.Reject(V8ThrowDOMException::CreateOrEmpty(
          script_state.GetIsolate(), DOMExceptionCode::kDataError,
          "contextId cannot be set inside of fenced frames."));
      return false;
    }
    out_context_id = options.privateAggregationConfig()->contextId();
  }

  if (options.privateAggregationConfig()->hasAggregationCoordinatorOrigin()) {
    scoped_refptr<SecurityOrigin> parsed_coordinator =
        SecurityOrigin::CreateFromString(
            options.privateAggregationConfig()->aggregationCoordinatorOrigin());
    CHECK(parsed_coordinator);
    if (parsed_coordinator->IsOpaque()) {
      resolver.Reject(V8ThrowDOMException::CreateOrEmpty(
          script_state.GetIsolate(), DOMExceptionCode::kSyntaxError,
          "aggregationCoordinatorOrigin must be a valid origin"));
      return false;
    }
    if (!aggregation_service::IsAggregationCoordinatorOriginAllowed(
            parsed_coordinator->ToUrlOrigin())) {
      resolver.Reject(V8ThrowDOMException::CreateOrEmpty(
          script_state.GetIsolate(), DOMExceptionCode::kDataError,
          "aggregationCoordinatorOrigin must be on the allowlist"));
      return false;
    }
    out_aggregation_coordinator_origin = parsed_coordinator;
  }

  if (options.privateAggregationConfig()->hasFilteringIdMaxBytes()) {
    if (options.privateAggregationConfig()->filteringIdMaxBytes() < 1) {
      resolver.Reject(V8ThrowDOMException::CreateOrEmpty(
          script_state.GetIsolate(), DOMExceptionCode::kDataError,
          "filteringIdMaxBytes must be positive"));
      return false;
    }
    if (options.privateAggregationConfig()->filteringIdMaxBytes() >
        kMaximumFilteringIdMaxBytes) {
      resolver.Reject(V8ThrowDOMException::CreateOrEmpty(
          script_state.GetIsolate(), DOMExceptionCode::kDataError,
          "filteringIdMaxBytes is too big"));
      return false;
    }
    if (is_in_fenced_frame &&
        base::FeatureList::IsEnabled(
            features::kFencedFramesLocalUnpartitionedDataAccess)) {
      resolver.Reject(V8ThrowDOMException::CreateOrEmpty(
          script_state.GetIsolate(), DOMExceptionCode::kDataError,
          "filteringIdMaxBytes cannot be set inside of fenced frames."));
      return false;
    }
    out_filtering_id_max_bytes = static_cast<uint32_t>(
        options.privateAggregationConfig()->filteringIdMaxBytes());
  }

  if (options.privateAggregationConfig()->hasMaxContributions() &&
      base::FeatureList::IsEnabled(
          features::kPrivateAggregationApiMaxContributions)) {
    const auto requested_max_contributions =
        options.privateAggregationConfig()->maxContributions();
    if (requested_max_contributions == 0) {
      resolver.Reject(V8ThrowDOMException::CreateOrEmpty(
          script_state.GetIsolate(), DOMExceptionCode::kDataError,
          "maxContributions must be positive"));
      return false;
    }
    const uint16_t max_contributions_clamped =
        base::MakeClampedNum(requested_max_contributions);
    out_private_aggregation_config->max_contributions =
        max_contributions_clamped;
  }

  return true;
}

}  // namespace blink
