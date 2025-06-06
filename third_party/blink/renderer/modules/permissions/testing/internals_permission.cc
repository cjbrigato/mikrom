// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/permissions/testing/internals_permission.h"

#include <utility>

#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/blink/public/common/permissions/permission_utils.h"
#include "third_party/blink/public/common/thread_safe_browser_interface_broker_proxy.h"
#include "third_party/blink/public/mojom/permissions/permission.mojom-blink.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/test/mojom/permissions/permission_automation.test-mojom-blink.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/bindings/core/v8/script_value.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_permission_state.h"
#include "third_party/blink/renderer/core/dom/dom_exception.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/page/frame_tree.h"
#include "third_party/blink/renderer/core/testing/internals.h"
#include "third_party/blink/renderer/modules/permissions/permission_utils.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/bindings/script_state.h"
#include "third_party/blink/renderer/platform/weborigin/kurl.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

// static
ScriptPromise<IDLUndefined> InternalsPermission::setPermission(
    ScriptState* script_state,
    Internals&,
    const ScriptValue& raw_descriptor,
    const V8PermissionState& state,
    ExceptionState& exception_state) {
  mojom::blink::PermissionDescriptorPtr descriptor =
      ParsePermissionDescriptor(script_state, raw_descriptor, exception_state);
  if (exception_state.HadException() || !script_state->ContextIsValid())
    return EmptyPromise();

  LocalDOMWindow* window = LocalDOMWindow::From(script_state);
  const SecurityOrigin* security_origin = window->GetSecurityOrigin();
  if (security_origin->IsOpaque()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotAllowedError,
        "Unable to set permission for an opaque origin.");
    return EmptyPromise();
  }
  KURL url = KURL(security_origin->ToString());
  DCHECK(url.IsValid());

  Frame& top_frame = window->GetFrame()->Tree().Top();
  const SecurityOrigin* top_security_origin =
      top_frame.GetSecurityContext()->GetSecurityOrigin();
  if (top_security_origin->IsOpaque()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotAllowedError,
        "Unable to set permission for an opaque embedding origin.");
    return EmptyPromise();
  }
  KURL embedding_url = KURL(top_security_origin->ToString());

  mojo::Remote<test::mojom::blink::PermissionAutomation> permission_automation;
  Platform::Current()->GetBrowserInterfaceBroker()->GetInterface(
      permission_automation.BindNewPipeAndPassReceiver());
  DCHECK(permission_automation.is_bound());

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLUndefined>>(
      script_state, exception_state.GetContext());
  auto promise = resolver->Promise();
  auto* raw_permission_automation = permission_automation.get();
  raw_permission_automation->SetPermission(
      std::move(descriptor), ToPermissionStatus(state.AsCStr()), url,
      embedding_url,
      WTF::BindOnce(
          // While we only really need |resolver|, we also take the
          // mojo::Remote<> so that it remains alive after this function exits.
          [](ScriptPromiseResolver<IDLUndefined>* resolver,
             mojo::Remote<test::mojom::blink::PermissionAutomation>,
             bool success) {
            if (success)
              resolver->Resolve();
            else
              resolver->Reject();
          },
          WrapPersistent(resolver), std::move(permission_automation)));

  return promise;
}

}  // namespace blink
