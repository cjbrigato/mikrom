// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/shared_storage/shared_storage_document_service_impl.h"

#include <stdint.h>

#include <string>
#include <utility>

#include "base/metrics/histogram_functions.h"
#include "base/strings/strcat.h"
#include "components/services/storage/shared_storage/shared_storage_database.h"
#include "components/services/storage/shared_storage/shared_storage_manager.h"
#include "content/browser/renderer_host/frame_tree_node.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/shared_storage/shared_storage_lock_manager.h"
#include "content/browser/shared_storage/shared_storage_runtime_manager.h"
#include "content/browser/shared_storage/shared_storage_worklet_host.h"
#include "content/browser/storage_partition_impl.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/content_client.h"
#include "services/network/public/cpp/features.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "services/network/public/mojom/shared_storage.mojom.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/fenced_frame/fenced_frame_utils.h"
#include "third_party/blink/public/common/shared_storage/shared_storage_utils.h"
#include "url/url_constants.h"

namespace content {

namespace {

// Note that this function would also return false if the context origin is
// opaque. This is stricter than the web platform's notion of "secure context".
// TODO(yaoxia): This should be function in FrameTreeNode.
bool IsSecureFrame(RenderFrameHost* frame) {
  while (frame) {
    if (!network::IsOriginPotentiallyTrustworthy(
            frame->GetLastCommittedOrigin())) {
      return false;
    }
    frame = frame->GetParent();
  }
  return true;
}

bool CheckSecureContext(RenderFrameHost& frame) {
  bool is_secure_frame = IsSecureFrame(&frame);

  base::UmaHistogramBoolean(
      "Storage.SharedStorage.DocumentServiceBind.IsSecureFrame",
      is_secure_frame);

  return is_secure_frame;
}

using AccessScope = blink::SharedStorageAccessScope;
using AccessMethod =
    SharedStorageRuntimeManager::SharedStorageObserverInterface::AccessMethod;

using OperationResult = storage::SharedStorageManager::OperationResult;
using GetResult = storage::SharedStorageManager::GetResult;

}  // namespace

const char kFencedStorageReadDisabledMessage[] =
    "Fenced storage read is disabled";

const char kFencedStorageReadWithoutRevokeNetworkMessage[] =
    "sharedStorage.get() is not allowed in a fenced frame until network "
    "access for it and all descendent frames has been revoked with "
    "window.fence.disableUntrustedNetwork()";

const char kSharedStorageDisabledMessage[] = "sharedStorage is disabled";

const char kSharedStorageSelectURLDisabledMessage[] =
    "sharedStorage.selectURL is disabled";

const char kSharedStorageAddModuleDisabledMessage[] =
    "sharedStorage.worklet.addModule is disabled because either sharedStorage "
    "is disabled or both sharedStorage.selectURL and privateAggregation are "
    "disabled";

const char kSharedStorageSelectURLLimitReachedMessage[] =
    "sharedStorage.selectURL limit has been reached";

const char kSharedStorageMethodFromInsecureContextMessage[] =
    "Attempted to invoke a sharedStorage method from an insecure context";

// NOTE: To preserve user privacy, the default value of the
// `network::features::kSharedStorageExposeDebugMessageForSettingsStatus`
// feature param MUST remain set to false (although the value can be overridden
// via the command line or in tests).
std::string GetSharedStorageErrorMessage(const std::string& debug_message,
                                         const std::string& input_message) {
  return network::features::kSharedStorageExposeDebugMessageForSettingsStatus
                 .Get()
             ? base::StrCat({input_message, "\nDebug: ", debug_message})
             : input_message;
}

SharedStorageDocumentServiceImpl::~SharedStorageDocumentServiceImpl() {
  GetSharedStorageRuntimeManager()->OnDocumentServiceDestroyed(this);
}

void SharedStorageDocumentServiceImpl::Bind(
    mojo::PendingAssociatedReceiver<blink::mojom::SharedStorageDocumentService>
        receiver) {
  CHECK(!receiver_)
      << "Multiple attempts to bind the SharedStorageDocumentService receiver";

  receiver_.Bind(std::move(receiver));
}

void SharedStorageDocumentServiceImpl::CreateWorklet(
    const GURL& script_source_url,
    const url::Origin& data_origin,
    blink::mojom::SharedStorageDataOriginType data_origin_type,
    network::mojom::CredentialsMode credentials_mode,
    blink::mojom::SharedStorageWorkletCreationMethod creation_method,
    const std::vector<blink::mojom::OriginTrialFeature>& origin_trial_features,
    mojo::PendingAssociatedReceiver<blink::mojom::SharedStorageWorkletHost>
        worklet_host,
    CreateWorkletCallback callback) {
  create_worklet_called_ = true;
  bool is_same_origin =
      render_frame_host().GetLastCommittedOrigin().IsSameOriginWith(
          data_origin);

  // `CreateWorklet()` cannot differentiate between calls from addModule() and
  // createWorklet(). Hence, we skip the mojom validation for opaque origin
  // context for addModule().

  // There's no consistent secure context check between the renderer process and
  // the browser process (see crbug.com/1153336). This is particularly
  // problematic when the origin is opaque. Hence, we skip the mojom validation
  // for secure context. Until the issue is addressed, an insecure context (in
  // a compromised renderer) can create worklets and execute operations.

  std::string debug_message;
  bool prefs_failure_is_site_specific = false;
  bool prefs_success = IsSharedStorageAddModuleAllowedForOrigin(
      data_origin, &debug_message, &prefs_failure_is_site_specific);

  if (!prefs_success && (is_same_origin || !prefs_failure_is_site_specific)) {
    OnCreateWorkletResponseIntercepted(
        is_same_origin,
        /*prefs_success=*/false, prefs_failure_is_site_specific,
        std::move(callback),
        /*post_prefs_success=*/false,
        /*error_message=*/
        GetSharedStorageErrorMessage(debug_message,
                                     kSharedStorageAddModuleDisabledMessage));
    return;
  }

  GetSharedStorageRuntimeManager()->CreateWorkletHost(
      this, render_frame_host().GetLastCommittedOrigin(), data_origin,
      data_origin_type, script_source_url, credentials_mode, creation_method,
      origin_trial_features, std::move(worklet_host),
      base::BindOnce(
          &SharedStorageDocumentServiceImpl::OnCreateWorkletResponseIntercepted,
          weak_ptr_factory_.GetWeakPtr(), is_same_origin, prefs_success,
          prefs_failure_is_site_specific, std::move(callback)));
}

void SharedStorageDocumentServiceImpl::SharedStorageGet(
    const std::u16string& key,
    SharedStorageGetCallback callback) {
  if (!render_frame_host().IsNestedWithinFencedFrame()) {
    receiver_.ReportBadMessage(
        "Attempted to call get() outside of a fenced frame.");
    return;
  }

  if (!base::FeatureList::IsEnabled(
          blink::features::kFencedFramesLocalUnpartitionedDataAccess)) {
    receiver_.ReportBadMessage(
        "Attempted to call sharedStorage.get() in a fenced frame with feature "
        "FencedFramesLocalUnpartitionedDataAccess disabled.");
    return;
  }

  if (!render_frame_host().GetPermissionsPolicy()->IsFeatureEnabled(
          network::mojom::PermissionsPolicyFeature::
              kFencedUnpartitionedStorageRead)) {
    // We already check for this permissions policy in the renderer.
    receiver_.ReportBadMessage("Attempted to call get() in a fenced frame "
        "with the fenced-unpartitioned-storage-read permissions policy "
        "disabled.");
    return;
  }

  if (render_frame_host().GetLastCommittedOrigin().opaque()) {
    receiver_.ReportBadMessage(
        "Attempted to call sharedStorage.get() from an opaque origin context.");
    return;
  }

  if (!CheckSecureContext(render_frame_host())) {
    RecordSharedStorageGetInFencedFrameOutcome(
        blink::SharedStorageGetInFencedFrameOutcome::kInsecureContext);
    std::move(callback).Run(
        blink::mojom::SharedStorageGetStatus::kError,
        /*error_message=*/kSharedStorageMethodFromInsecureContextMessage,
        /*value=*/{});

    // TODO(crbug.com/40068897): Invoke receiver_.ReportBadMessage here when
    // we can be sure honest renderers won't hit this path.
    return;
  }

  if (!IsFencedStorageReadAllowed(
          /*accessing_origin=*/render_frame_host().GetLastCommittedOrigin())) {
    RecordSharedStorageGetInFencedFrameOutcome(
        blink::SharedStorageGetInFencedFrameOutcome::kDisabled);
    std::move(callback).Run(blink::mojom::SharedStorageGetStatus::kError,
                            /*error_message=*/
                            kFencedStorageReadDisabledMessage,
                            /*value=*/{});
    return;
  }

  if (!(static_cast<RenderFrameHostImpl&>(render_frame_host())
            .CanReadFromSharedStorage())) {
    RecordSharedStorageGetInFencedFrameOutcome(
        blink::SharedStorageGetInFencedFrameOutcome::kWithoutRevokeNetwork);
    std::move(callback).Run(blink::mojom::SharedStorageGetStatus::kError,
                            /*error_message=*/
                            kFencedStorageReadWithoutRevokeNetworkMessage,
                            /*value=*/{});
    return;
  }

  GetSharedStorageRuntimeManager()->NotifySharedStorageAccessed(
      AccessScope::kWindow, AccessMethod::kGet, main_frame_id(),
      SerializeLastCommittedOrigin(),
      SharedStorageEventParams::CreateForGet(base::UTF16ToUTF8(key)));

  auto operation_completed_callback = base::BindOnce(
      [](SharedStorageGetCallback callback, GetResult result) {
        // If the key is not found but there is no other error, the worklet will
        // resolve the promise to undefined.
        if (result.result == OperationResult::kNotFound ||
            result.result == OperationResult::kExpired) {
          RecordSharedStorageGetInFencedFrameOutcome(
              blink::SharedStorageGetInFencedFrameOutcome::kKeyNotFound);
          std::move(callback).Run(
              blink::mojom::SharedStorageGetStatus::kNotFound,
              /*error_message=*/"sharedStorage.get() could not find key",
              /*value=*/{});
          return;
        }

        if (result.result != OperationResult::kSuccess) {
          RecordSharedStorageGetInFencedFrameOutcome(
              blink::SharedStorageGetInFencedFrameOutcome::kGetError);
          std::move(callback).Run(
              blink::mojom::SharedStorageGetStatus::kError,
              /*error_message=*/"sharedStorage.get() failed", /*value=*/{});
          return;
        }

        RecordSharedStorageGetInFencedFrameOutcome(
            blink::SharedStorageGetInFencedFrameOutcome::kSuccess);
        std::move(callback).Run(blink::mojom::SharedStorageGetStatus::kSuccess,
                                /*error_message=*/{}, /*value=*/result.data);
      },
      std::move(callback));

  GetSharedStorageManager()->Get(render_frame_host().GetLastCommittedOrigin(),
                                 key, std::move(operation_completed_callback));
}

void SharedStorageDocumentServiceImpl::SharedStorageUpdate(
    network::mojom::SharedStorageModifierMethodWithOptionsPtr
        method_with_options,
    SharedStorageUpdateCallback callback) {
  if (render_frame_host().GetLastCommittedOrigin().opaque()) {
    receiver_.ReportBadMessage(
        "Attempted to call SharedStorageUpdate() from an opaque origin "
        "context.");
    return;
  }

  if (!CheckSecureContext(render_frame_host())) {
    std::move(callback).Run(
        /*error_message=*/kSharedStorageMethodFromInsecureContextMessage);

    // TODO(crbug.com/40068897): Invoke receiver_.ReportBadMessage here when
    // we can be sure honest renderers won't hit this path.
    return;
  }

  std::string debug_message;
  if (!IsSharedStorageAllowed(&debug_message)) {
    std::move(callback).Run(GetSharedStorageErrorMessage(
        debug_message, kSharedStorageDisabledMessage));
    return;
  }

  GetSharedStorageRuntimeManager()->lock_manager().SharedStorageUpdate(
      std::move(method_with_options),
      /*shared_storage_origin=*/render_frame_host().GetLastCommittedOrigin(),
      AccessScope::kWindow, main_frame_id(),
      /*worklet_devtools_token=*/base::UnguessableToken::Null(),
      base::DoNothing());

  std::move(callback).Run(/*error_message=*/{});
}

void SharedStorageDocumentServiceImpl::SharedStorageBatchUpdate(
    std::vector<network::mojom::SharedStorageModifierMethodWithOptionsPtr>
        methods_with_options,
    const std::optional<std::string>& with_lock,
    SharedStorageBatchUpdateCallback callback) {
  if (render_frame_host().GetLastCommittedOrigin().opaque()) {
    receiver_.ReportBadMessage(
        "Attempted to call SharedStorageBatchUpdate() from an opaque origin "
        "context.");
    return;
  }

  if (!CheckSecureContext(render_frame_host())) {
    std::move(callback).Run(
        /*error_message=*/kSharedStorageMethodFromInsecureContextMessage);

    // TODO(crbug.com/40068897): Invoke receiver_.ReportBadMessage here when
    // we can be sure honest renderers won't hit this path.
    return;
  }

  std::string debug_message;
  if (!IsSharedStorageAllowed(&debug_message)) {
    std::move(callback).Run(GetSharedStorageErrorMessage(
        debug_message, kSharedStorageDisabledMessage));
    return;
  }

  GetSharedStorageRuntimeManager()->lock_manager().SharedStorageBatchUpdate(
      std::move(methods_with_options), with_lock,
      /*shared_storage_origin=*/render_frame_host().GetLastCommittedOrigin(),
      AccessScope::kWindow, main_frame_id(),
      /*worklet_devtools_token=*/base::UnguessableToken::Null(),
      base::DoNothing());

  std::move(callback).Run(/*error_message=*/{});
}

base::WeakPtr<SharedStorageDocumentServiceImpl>
SharedStorageDocumentServiceImpl::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

SharedStorageDocumentServiceImpl::SharedStorageDocumentServiceImpl(
    RenderFrameHost* rfh)
    : DocumentUserData<SharedStorageDocumentServiceImpl>(rfh),
      main_frame_origin_(
          rfh->GetOutermostMainFrame()->GetLastCommittedOrigin()),
      main_frame_id_(
          static_cast<RenderFrameHostImpl*>(rfh->GetOutermostMainFrame())
              ->GetGlobalId()) {}

void SharedStorageDocumentServiceImpl::OnCreateWorkletResponseIntercepted(
    bool is_same_origin,
    bool prefs_success,
    bool prefs_failure_is_site_specific,
    CreateWorkletCallback original_callback,
    bool post_prefs_success,
    const std::string& error_message) {
  bool web_visible_prefs_error =
      !prefs_success && (is_same_origin || !prefs_failure_is_site_specific);
  bool other_web_visible_error = !post_prefs_success;

  if (web_visible_prefs_error || other_web_visible_error) {
    std::move(original_callback).Run(/*success=*/false, error_message);
    return;
  }

  // When the worklet and the worklet creator are not same-origin, the user
  // preferences for the worklet origin should not be revealed. So any
  // site-specific preference error will be suppressed.
  if (!prefs_success) {
    CHECK(!is_same_origin && prefs_failure_is_site_specific);
    LogSharedStorageWorkletError(
        blink::SharedStorageWorkletErrorType::
            kAddModuleNonWebVisibleCrossOriginSharedStorageDisabled);
    std::move(original_callback).Run(/*success=*/true, /*error_message=*/{});
    return;
  }

  CHECK(post_prefs_success);
  LogSharedStorageWorkletError(blink::SharedStorageWorkletErrorType::kSuccess);
  std::move(original_callback).Run(/*success=*/true, /*error_message=*/{});
}

storage::SharedStorageManager*
SharedStorageDocumentServiceImpl::GetSharedStorageManager() {
  storage::SharedStorageManager* shared_storage_manager =
      static_cast<StoragePartitionImpl*>(
          render_frame_host().GetProcess()->GetStoragePartition())
          ->GetSharedStorageManager();

  // This `SharedStorageDocumentServiceImpl` is created only if
  // `kSharedStorageAPI` is enabled, in which case the `shared_storage_manager`
  // must be valid.
  DCHECK(shared_storage_manager);

  return shared_storage_manager;
}

SharedStorageRuntimeManager*
SharedStorageDocumentServiceImpl::GetSharedStorageRuntimeManager() {
  return static_cast<StoragePartitionImpl*>(
             render_frame_host().GetProcess()->GetStoragePartition())
      ->GetSharedStorageRuntimeManager();
}

bool SharedStorageDocumentServiceImpl::IsSharedStorageAllowed(
    std::string* out_debug_message,
    bool* out_block_is_site_setting_specific) {
  // Will trigger a call to
  // `content_settings::PageSpecificContentSettings::BrowsingDataAccessed()` for
  // reporting purposes.
  return IsSharedStorageAllowedForOrigin(
      render_frame_host().GetLastCommittedOrigin(), out_debug_message,
      out_block_is_site_setting_specific);
}

bool SharedStorageDocumentServiceImpl::IsSharedStorageAllowedForOrigin(
    const url::Origin& accessing_origin,
    std::string* out_debug_message,
    bool* out_block_is_site_setting_specific) {
  // Will trigger a call to
  // `content_settings::PageSpecificContentSettings::BrowsingDataAccessed()` for
  // reporting purposes.
  return GetContentClient()->browser()->IsSharedStorageAllowed(
      render_frame_host().GetBrowserContext(), &render_frame_host(),
      main_frame_origin_, accessing_origin, out_debug_message,
      out_block_is_site_setting_specific);
}

bool SharedStorageDocumentServiceImpl::IsFencedStorageReadAllowed(
    const url::Origin& accessing_origin) {
  return GetContentClient()->browser()->IsFencedStorageReadAllowed(
      render_frame_host().GetBrowserContext(), &render_frame_host(),
      /*top_frame_origin=*/main_frame_origin_,
      /*accessing_origin=*/accessing_origin);
}

bool SharedStorageDocumentServiceImpl::IsSharedStorageAddModuleAllowedForOrigin(
    const url::Origin& accessing_origin,
    std::string* out_debug_message,
    bool* out_block_is_site_setting_specific) {
  // Will trigger a call to
  // `content_settings::PageSpecificContentSettings::BrowsingDataAccessed()` for
  // reporting purposes.
  if (!IsSharedStorageAllowedForOrigin(accessing_origin, out_debug_message,
                                       out_block_is_site_setting_specific)) {
    return false;
  }

  bool select_url_block_is_site_setting_specific = false;
  bool private_aggregation_block_is_site_setting_specific = false;

  bool add_module_allowed =
      GetContentClient()->browser()->IsSharedStorageSelectURLAllowed(
          render_frame_host().GetBrowserContext(), main_frame_origin_,
          accessing_origin, out_debug_message,
          &select_url_block_is_site_setting_specific) ||
      GetContentClient()->browser()->IsPrivateAggregationAllowed(
          render_frame_host().GetBrowserContext(), main_frame_origin_,
          accessing_origin,
          &private_aggregation_block_is_site_setting_specific);

  *out_block_is_site_setting_specific =
      !add_module_allowed &&
      (select_url_block_is_site_setting_specific ||
       private_aggregation_block_is_site_setting_specific);
  return add_module_allowed;
}

std::string SharedStorageDocumentServiceImpl::SerializeLastCommittedOrigin()
    const {
  return render_frame_host().GetLastCommittedOrigin().Serialize();
}

DOCUMENT_USER_DATA_KEY_IMPL(SharedStorageDocumentServiceImpl);

}  // namespace content
