// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/controller/javascript_call_stack_collector.h"

#include "third_party/blink/public/common/permissions_policy/document_policy_features.h"
#include "third_party/blink/public/common/scheme_registry.h"
#include "third_party/blink/public/common/tokens/tokens.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/platform/bindings/dom_wrapper_world.h"
#include "third_party/blink/renderer/platform/bindings/script_state.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/scheduler/public/main_thread.h"
#include "third_party/blink/renderer/platform/scheduler/public/main_thread_scheduler.h"
#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_copier.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"

namespace WTF {

template <>
struct CrossThreadCopier<std::optional<blink::LocalFrameToken>>
    : public CrossThreadCopierPassThrough<
          std::optional<blink::LocalFrameToken>> {};

}  // namespace WTF

namespace blink {

namespace {

// Determines whether a script frame should be included in the call stack.
// frames whose URL protocol matches an extension scheme are excluded.
// Returns true to include the frame (i.e. not redacted) and false to exclude
// it.
bool IsScriptFrameAllowed(v8::Isolate* isolate,
                          v8::Local<v8::String> script_name) {
  String script_url =
      ToCoreStringWithUndefinedOrNullCheck(isolate, script_name);
  if (script_url.empty()) {
    return true;
  }
  KURL url(script_url);
  if (!url.IsValid()) {
    return true;
  }

  return !CommonSchemeRegistry::IsExtensionScheme(url.Protocol().Ascii());
}

// Gathers and formats the call stack in a format that's
// consistent with Error.stack If extension frames are detected,
// we replace them with <redacted> to protect privacy.
// The function respects the stack trace limit set in the isolate.
void CollectFilteredCallStack(v8::Isolate* isolate, StringBuilder& builder) {
  std::ostringstream oss;
  v8::Message::PrintCurrentStackTrace(isolate, oss, &IsScriptFrameAllowed);
  const std::string stack_trace = oss.str();
  std::istringstream iss(stack_trace);
  std::string line;
  int processed_frames = 0;

  const int stack_trace_limit = isolate->GetStackTraceLimit();
  while (std::getline(iss, line) && processed_frames < stack_trace_limit) {
    builder.Append(kStackFramePrefix);
    builder.Append(base::as_byte_span(line));
    processed_frames++;
  }
}

void PostHandleCollectedCallStackTask(
    JavaScriptCallStackCollector* collector,
    WTF::StringBuilder& builder,
    std::optional<LocalFrameToken> frame_token = std::nullopt) {
  DCHECK(Platform::Current());
  PostCrossThreadTask(
      *Platform::Current()->GetIOTaskRunner(), FROM_HERE,
      WTF::CrossThreadBindOnce(
          &JavaScriptCallStackCollector::HandleCallStackCollected,
          WTF::CrossThreadUnretained(collector), builder.ReleaseString(),
          frame_token));
}

void GenerateJavaScriptCallStack(v8::Isolate* isolate, void* data) {
  CHECK(IsMainThread());

  auto* collector = static_cast<JavaScriptCallStackCollector*>(data);
  v8::HandleScope handle_scope(isolate);
  WTF::StringBuilder builder;
  if (!isolate->InContext()) {
    PostHandleCollectedCallStackTask(collector, builder);
    return;
  }

  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  ScriptState* script_state = ScriptState::MaybeFrom(isolate, context);
  if (!script_state) {
    PostHandleCollectedCallStackTask(collector, builder);
    return;
  }
  ExecutionContext* execution_context = ToExecutionContext(script_state);
  if (!RuntimeEnabledFeatures::
          DocumentPolicyIncludeJSCallStacksInCrashReportsEnabled(
              execution_context)) {
    PostHandleCollectedCallStackTask(collector, builder);
    return;
  }
  DOMWrapperWorld& world = script_state->World();
  auto* execution_dom_window = DynamicTo<LocalDOMWindow>(execution_context);
  LocalFrame* frame =
      execution_dom_window ? execution_dom_window->GetFrame() : nullptr;

  std::optional<LocalFrameToken> frame_token;
  if (frame && world.IsMainWorld()) {
    frame_token = frame->GetLocalFrameToken();
    if (!execution_context->IsFeatureEnabled(
            mojom::blink::DocumentPolicyFeature::
                kIncludeJSCallStacksInCrashReports)) {
      builder.Append(kWebsiteOwnerNotOptedInMessage);
    } else {
      UseCounter::Count(
          execution_context,
          WebFeature::kDocumentPolicyIncludeJSCallStacksInCrashReports);
      CollectFilteredCallStack(isolate, builder);
    }
  }
  PostHandleCollectedCallStackTask(collector, builder, frame_token);
}

}  // namespace

void JavaScriptCallStackCollector::InterruptIsolateAndCollectCallStack(
    v8::Isolate* isolate) {
  if (has_interrupted_isolate_) {
    return;
  }
  has_interrupted_isolate_ = true;
  isolate->RequestInterrupt(&GenerateJavaScriptCallStack,
                            static_cast<void*>(this));
}

void JavaScriptCallStackCollector::HandleCallStackCollected(
    const String& call_stack,
    const std::optional<LocalFrameToken> frame_token) {
  DCHECK(result_callback_);
  std::move(result_callback_).Run(call_stack, frame_token);
  DCHECK(finished_callback_);
  std::move(finished_callback_).Run(this);
}

void JavaScriptCallStackCollector::CollectJavaScriptCallStack() {
  Thread::MainThread()
      ->Scheduler()
      ->ToMainThreadScheduler()
      ->ForEachMainThreadIsolate(WTF::BindRepeating(
          &JavaScriptCallStackCollector::InterruptIsolateAndCollectCallStack,
          WTF::Unretained(this)));
}

}  // namespace blink
