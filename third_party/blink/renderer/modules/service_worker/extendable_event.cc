/*
 * Copyright (C) 2014 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/modules/service_worker/extendable_event.h"

#include "third_party/blink/renderer/modules/service_worker/wait_until_observer.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"

namespace blink {

ExtendableEvent* ExtendableEvent::Create(
    const AtomicString& type,
    const ExtendableEventInit* event_init) {
  return MakeGarbageCollected<ExtendableEvent>(type, event_init);
}

ExtendableEvent* ExtendableEvent::Create(const AtomicString& type,
                                         const ExtendableEventInit* event_init,
                                         WaitUntilObserver* observer) {
  return MakeGarbageCollected<ExtendableEvent>(type, event_init, observer);
}

ExtendableEvent::~ExtendableEvent() = default;

// This injects an extra microtask step for WaitUntilObserver::WaitUntil() on
// fulfill/reject, as required by
// https://w3c.github.io/ServiceWorker/#extendableevent-add-lifetime-promise
class WaitUntilFulfill final : public ThenCallable<IDLAny, WaitUntilFulfill> {
 public:
  void React(ScriptState*, ScriptValue) {}
};

class WaitUntilReject final
    : public ThenCallable<IDLAny, WaitUntilReject, IDLPromise<IDLAny>> {
 public:
  ScriptPromise<IDLAny> React(ScriptState* script_state, ScriptValue value) {
    return ScriptPromise<IDLAny>::Reject(script_state, value);
  }
};

void ExtendableEvent::waitUntil(ScriptState* script_state,
                                ScriptPromise<IDLAny> script_promise,
                                ExceptionState& exception_state) {
  if (!observer_) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidStateError,
        "Can not call waitUntil on a script constructed ExtendableEvent.");
    return;
  }

  observer_->WaitUntil(
      script_state,
      script_promise.Then(script_state,
                          MakeGarbageCollected<WaitUntilFulfill>(),
                          MakeGarbageCollected<WaitUntilReject>()),
      exception_state);
}

ExtendableEvent::ExtendableEvent(const AtomicString& type,
                                 const ExtendableEventInit* initializer)
    : Event(type, initializer) {}

ExtendableEvent::ExtendableEvent(const AtomicString& type,
                                 const ExtendableEventInit* initializer,
                                 WaitUntilObserver* observer)
    : Event(type, initializer), observer_(observer) {}

const AtomicString& ExtendableEvent::InterfaceName() const {
  return event_interface_names::kExtendableEvent;
}

void ExtendableEvent::Trace(Visitor* visitor) const {
  visitor->Trace(observer_);
  Event::Trace(visitor);
}

}  // namespace blink
