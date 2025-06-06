// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/shared_storage/shared_storage.h"

#include <memory>
#include <tuple>
#include <utility>

#include "base/check.h"
#include "base/metrics/histogram_functions.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/strcat.h"
#include "base/time/time.h"
#include "services/network/public/cpp/shared_storage_utils.h"
#include "services/network/public/mojom/shared_storage.mojom-blink.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/fenced_frame/fenced_frame_utils.h"
#include "third_party/blink/public/common/shared_storage/shared_storage_utils.h"
#include "third_party/blink/public/mojom/shared_storage/shared_storage.mojom-blink.h"
#include "third_party/blink/public/mojom/shared_storage/shared_storage_worklet_service.mojom-blink.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/public/platform/web_content_settings_client.h"
#include "third_party/blink/public/platform/web_security_origin.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_throw_dom_exception.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_binding_for_modules.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_shared_storage_modifier_method.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_shared_storage_modifier_method_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_shared_storage_private_aggregation_config.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_shared_storage_run_operation_method_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_shared_storage_set_method_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_shared_storage_url_with_metadata.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_shared_storage_worklet_options.h"
#include "third_party/blink/renderer/core/dom/dom_exception.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/modules/shared_storage/shared_storage_append_method.h"
#include "third_party/blink/renderer/modules/shared_storage/shared_storage_clear_method.h"
#include "third_party/blink/renderer/modules/shared_storage/shared_storage_delete_method.h"
#include "third_party/blink/renderer/modules/shared_storage/shared_storage_set_method.h"
#include "third_party/blink/renderer/modules/shared_storage/shared_storage_window_supplement.h"
#include "third_party/blink/renderer/modules/shared_storage/shared_storage_worklet.h"
#include "third_party/blink/renderer/modules/shared_storage/shared_storage_worklet_global_scope.h"
#include "third_party/blink/renderer/modules/shared_storage/util.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_deque.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_receiver.h"
#include "third_party/blink/renderer/platform/weborigin/security_origin.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/deque.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

enum class SharedStorage::SharedStorageSetterMethod : uint8_t {
  kSet = 0,
  kAppend = 1,
  kDelete = 2,
  kClear = 3,
  kBatchUpdate = 4,
};

namespace {

enum class GlobalScope {
  kWindow,
  kSharedStorageWorklet,
};

void LogTimingHistogramForSetterMethod(
    SharedStorage::SharedStorageSetterMethod method,
    GlobalScope global_scope,
    base::TimeTicks start_time) {
  base::TimeDelta elapsed_time = base::TimeTicks::Now() - start_time;

  std::string histogram_prefix = (global_scope == GlobalScope::kWindow)
                                     ? "Storage.SharedStorage.Document."
                                     : "Storage.SharedStorage.Worklet.";

  switch (method) {
    case SharedStorage::SharedStorageSetterMethod::kSet:
      base::UmaHistogramMediumTimes(
          base::StrCat({histogram_prefix, "Timing.Set"}), elapsed_time);
      break;
    case SharedStorage::SharedStorageSetterMethod::kAppend:
      base::UmaHistogramMediumTimes(
          base::StrCat({histogram_prefix, "Timing.Append"}), elapsed_time);
      break;
    case SharedStorage::SharedStorageSetterMethod::kDelete:
      base::UmaHistogramMediumTimes(
          base::StrCat({histogram_prefix, "Timing.Delete"}), elapsed_time);
      break;
    case SharedStorage::SharedStorageSetterMethod::kClear:
      base::UmaHistogramMediumTimes(
          base::StrCat({histogram_prefix, "Timing.Clear"}), elapsed_time);
      break;
    case SharedStorage::SharedStorageSetterMethod::kBatchUpdate:
      base::UmaHistogramMediumTimes(
          base::StrCat({histogram_prefix, "Timing.BatchUpdate"}), elapsed_time);
      break;
    default:
      NOTREACHED();
  }
}

void OnSharedStorageUpdateFinished(
    ScriptPromiseResolver<IDLAny>* resolver,
    SharedStorage* shared_storage,
    SharedStorage::SharedStorageSetterMethod method,
    GlobalScope global_scope,
    base::TimeTicks start_time,
    const String& error_message) {
  DCHECK(resolver);
  ScriptState* script_state = resolver->GetScriptState();

  if (!error_message.empty()) {
    if (IsInParallelAlgorithmRunnable(resolver->GetExecutionContext(),
                                      script_state)) {
      ScriptState::Scope scope(script_state);
      resolver->Reject(V8ThrowDOMException::CreateOrEmpty(
          script_state->GetIsolate(), DOMExceptionCode::kOperationError,
          error_message));
    }
    return;
  }

  LogTimingHistogramForSetterMethod(method, global_scope, start_time);
  resolver->Resolve();
}

mojom::blink::SharedStorageDocumentService* GetSharedStorageDocumentService(
    ExecutionContext* execution_context) {
  CHECK(execution_context->IsWindow());
  return SharedStorageWindowSupplement::From(
             To<LocalDOMWindow>(*execution_context))
      ->GetSharedStorageDocumentService();
}

mojom::blink::SharedStorageWorkletServiceClient*
GetSharedStorageWorkletServiceClient(ExecutionContext* execution_context) {
  CHECK(execution_context->IsSharedStorageWorkletGlobalScope());
  return To<SharedStorageWorkletGlobalScope>(execution_context)
      ->GetSharedStorageWorkletServiceClient();
}

bool CanGetOutsideWorklet(ScriptState* script_state) {
  ExecutionContext* execution_context = ExecutionContext::From(script_state);
  CHECK(execution_context->IsWindow());

  LocalFrame* frame = To<LocalDOMWindow>(execution_context)->GetFrame();
  DCHECK(frame);

  if (!blink::features::IsFencedFramesEnabled()) {
    return false;
  }

  // Calling get() is only allowed in fenced frame trees where network access
  // has been restricted. We can't check the network access part in the
  // renderer, so we'll defer to the browser for that.
  return frame->IsInFencedFrameTree();
}

std::tuple<SharedStorageDataOrigin, scoped_refptr<SecurityOrigin>>
ParseDataOrigin(const String& data_origin_value) {
  String data_origin_value_lower = data_origin_value.LowerASCII();
  if (data_origin_value_lower == "context-origin") {
    return std::make_tuple(SharedStorageDataOrigin::kContextOrigin, nullptr);
  }
  if (data_origin_value_lower == "script-origin") {
    return std::make_tuple(SharedStorageDataOrigin::kScriptOrigin, nullptr);
  }
  KURL data_origin_url(data_origin_value);
  if (!data_origin_url.IsValid()) {
    return std::make_tuple(SharedStorageDataOrigin::kInvalid, nullptr);
  }
  return std::make_tuple(SharedStorageDataOrigin::kCustomOrigin,
                         SecurityOrigin::Create(data_origin_url));
}

}  // namespace

class SharedStorage::IterationSource final
    : public PairAsyncIterable<SharedStorage>::IterationSource,
      public mojom::blink::SharedStorageEntriesListener {
 public:
  IterationSource(ScriptState* script_state,
                  ExecutionContext* execution_context,
                  Kind kind,
                  mojom::blink::SharedStorageWorkletServiceClient* client)
      : PairAsyncIterable<SharedStorage>::IterationSource(script_state, kind),
        receiver_(this, execution_context) {
    if (GetKind() == Kind::kKey) {
      client->SharedStorageKeys(receiver_.BindNewPipeAndPassRemote(
          execution_context->GetTaskRunner(TaskType::kMiscPlatformAPI)));
    } else {
      bool values_only = GetKind() == Kind::kValue;
      client->SharedStorageEntries(
          receiver_.BindNewPipeAndPassRemote(
              execution_context->GetTaskRunner(TaskType::kMiscPlatformAPI)),
          values_only);
    }

    base::UmaHistogramExactLinear(
        "Storage.SharedStorage.AsyncIterator.IteratedEntriesBenchmarks", 0,
        101);
  }

  void DidReadEntries(
      bool success,
      const String& error_message,
      Vector<mojom::blink::SharedStorageKeyAndOrValuePtr> entries,
      bool has_more_entries,
      int total_queued_to_send) override {
    CHECK(is_waiting_for_more_entries_);
    CHECK(error_message_.IsNull());
    CHECK(!(success && entries.empty() && has_more_entries));

    if (!success) {
      if (error_message.IsNull()) {
        error_message_ = g_empty_string;
      } else {
        error_message_ = error_message;
      }
    }

    for (auto& entry : entries) {
      shared_storage_entry_queue_.push_back(std::move(entry));
    }
    is_waiting_for_more_entries_ = has_more_entries;

    // Benchmark
    if (!total_entries_queued_) {
      total_entries_queued_ = total_queued_to_send;
      base::UmaHistogramCounts10000(
          "Storage.SharedStorage.AsyncIterator.EntriesQueuedCount",
          total_entries_queued_);
    }
    base::CheckedNumeric<int> count = entries_received_;
    count += entries.size();
    entries_received_ = count.ValueOrDie();
    while (next_benchmark_for_receipt_ <= 100 &&
           MeetsBenchmark(entries_received_, next_benchmark_for_receipt_)) {
      base::UmaHistogramExactLinear(
          "Storage.SharedStorage.AsyncIterator.ReceivedEntriesBenchmarks",
          next_benchmark_for_receipt_, 101);
      next_benchmark_for_receipt_ += kBenchmarkStep;
    }

    ScriptState::Scope script_state_scope(GetScriptState());
    TryResolvePromise();
  }

  void Trace(Visitor* visitor) const override {
    visitor->Trace(receiver_);
    PairAsyncIterable<SharedStorage>::IterationSource::Trace(visitor);
  }

 protected:
  void GetNextIterationResult() override {
    DCHECK(!next_start_time_);
    next_start_time_ = base::TimeTicks::Now();

    TryResolvePromise();
  }

 private:
  void TryResolvePromise() {
    if (!HasPendingPromise()) {
      return;
    }

    if (!shared_storage_entry_queue_.empty()) {
      mojom::blink::SharedStorageKeyAndOrValuePtr entry =
          shared_storage_entry_queue_.TakeFirst();
      TakePendingPromiseResolver()->Resolve(
          MakeIterationResult(entry->key, entry->value));
      LogElapsedTime();

      base::CheckedNumeric<int> count = entries_iterated_;
      entries_iterated_ = (++count).ValueOrDie();

      while (next_benchmark_for_iteration_ <= 100 &&
             MeetsBenchmark(entries_iterated_, next_benchmark_for_iteration_)) {
        base::UmaHistogramExactLinear(
            "Storage.SharedStorage.AsyncIterator.IteratedEntriesBenchmarks",
            next_benchmark_for_iteration_, 101);
        next_benchmark_for_iteration_ += kBenchmarkStep;
      }

      return;
    }

    if (!error_message_.IsNull()) {
      TakePendingPromiseResolver()->Reject(V8ThrowDOMException::CreateOrEmpty(
          GetScriptState()->GetIsolate(), DOMExceptionCode::kOperationError,
          error_message_));
      // We only record timing histograms when there is no error. Discard the
      // start time for this call.
      DCHECK(next_start_time_);
      next_start_time_.reset();
      return;
    }

    if (!is_waiting_for_more_entries_) {
      TakePendingPromiseResolver()->Resolve(MakeEndOfIteration());
      LogElapsedTime();
      return;
    }
  }

  bool MeetsBenchmark(int value, int benchmark) {
    CHECK_GE(benchmark, 0);
    CHECK_LE(benchmark, 100);
    CHECK_EQ(benchmark % kBenchmarkStep, 0);
    CHECK_GE(total_entries_queued_, 0);

    if (benchmark == 0 || (total_entries_queued_ == 0 && value == 0)) {
      return true;
    }

    CHECK_GT(total_entries_queued_, 0);
    return (100 * value) / total_entries_queued_ >= benchmark;
  }

  void LogElapsedTime() {
    CHECK(next_start_time_);
    base::TimeDelta elapsed_time = base::TimeTicks::Now() - *next_start_time_;
    next_start_time_.reset();
    switch (GetKind()) {
      case Kind::kKey:
        base::UmaHistogramMediumTimes(
            "Storage.SharedStorage.Worklet.Timing.Keys.Next", elapsed_time);
        break;
      case Kind::kValue:
        base::UmaHistogramMediumTimes(
            "Storage.SharedStorage.Worklet.Timing.Values.Next", elapsed_time);
        break;
      case Kind::kKeyValue:
        base::UmaHistogramMediumTimes(
            "Storage.SharedStorage.Worklet.Timing.Entries.Next", elapsed_time);
        break;
    }
  }

  HeapMojoReceiver<mojom::blink::SharedStorageEntriesListener, IterationSource>
      receiver_;
  // Queue of the successful results.
  Deque<mojom::blink::SharedStorageKeyAndOrValuePtr>
      shared_storage_entry_queue_;
  String error_message_;  // Non-null string means error.
  bool is_waiting_for_more_entries_ = true;

  // Benchmark
  //
  // The total number of entries that the database has queued to send via this
  // iterator.
  int total_entries_queued_ = 0;
  // The number of entries that the iterator has received from the database so
  // far.
  int entries_received_ = 0;
  // The number of entries that the iterator has iterated through.
  int entries_iterated_ = 0;
  // The lowest benchmark for received entries that is currently unmet and so
  // has not been logged.
  int next_benchmark_for_receipt_ = 0;
  // The lowest benchmark for iterated entries that is currently unmet and so
  // has not been logged.
  int next_benchmark_for_iteration_ = kBenchmarkStep;
  // The step size of received / iterated entries.
  static constexpr int kBenchmarkStep = 10;
  // Start time of each call to GetTheNextIterationResult. Used to record a
  // timing histogram.
  std::optional<base::TimeTicks> next_start_time_;
};

SharedStorage::SharedStorage() = default;
SharedStorage::~SharedStorage() = default;

void SharedStorage::Trace(Visitor* visitor) const {
  visitor->Trace(shared_storage_worklet_);
  ScriptWrappable::Trace(visitor);
}

void SharedStorage::UpdateDocumentSharedStorage(
    ExecutionContext* execution_context,
    network::mojom::blink::SharedStorageModifierMethodWithOptionsPtr method,
    ScriptPromiseResolver<IDLAny>* resolver,
    SharedStorageSetterMethod setter_method,
    base::TimeTicks start_time) {
  GetSharedStorageDocumentService(execution_context)
      ->SharedStorageUpdate(
          std::move(method),
          WTF::BindOnce(&OnSharedStorageUpdateFinished,
                        WrapPersistent(resolver), WrapWeakPersistent(this),
                        setter_method, GlobalScope::kWindow, start_time));
}

void SharedStorage::BatchUpdateDocumentSharedStorage(
    ExecutionContext* execution_context,
    std::optional<String> optional_with_lock,
    Vector<network::mojom::blink::SharedStorageModifierMethodWithOptionsPtr>
        methods,
    ScriptPromiseResolver<IDLAny>* resolver,
    base::TimeTicks start_time) {
  GetSharedStorageDocumentService(execution_context)
      ->SharedStorageBatchUpdate(
          std::move(methods), std::move(optional_with_lock),
          WTF::BindOnce(&OnSharedStorageUpdateFinished,
                        WrapPersistent(resolver), WrapWeakPersistent(this),
                        SharedStorageSetterMethod::kBatchUpdate,
                        GlobalScope::kWindow, start_time));
}

ScriptPromise<IDLAny> SharedStorage::set(ScriptState* script_state,
                                         const String& key,
                                         const String& value,
                                         ExceptionState& exception_state) {
  return set(script_state, key, value, SharedStorageSetMethodOptions::Create(),
             exception_state);
}

ScriptPromise<IDLAny> SharedStorage::set(
    ScriptState* script_state,
    const String& key,
    const String& value,
    const SharedStorageSetMethodOptions* options,
    ExceptionState& exception_state) {
  base::TimeTicks start_time = base::TimeTicks::Now();

  SharedStorageSetMethod* method = SharedStorageSetMethod::Create(
      script_state, key, value, options, exception_state);

  if (exception_state.HadException()) {
    return EmptyPromise();
  }

  ExecutionContext* execution_context = ExecutionContext::From(script_state);
  CHECK(execution_context->IsWindow() ||
        execution_context->IsSharedStorageWorkletGlobalScope());

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLAny>>(
      script_state, exception_state.GetContext());
  auto promise = resolver->Promise();

  if (auto* window = DynamicTo<LocalDOMWindow>(execution_context)) {
    if (window->document() && window->document()->IsPrerendering()) {
      window->document()->AddPostPrerenderingActivationStep(WTF::BindOnce(
          &SharedStorage::UpdateDocumentSharedStorage, WrapWeakPersistent(this),
          WrapWeakPersistent(execution_context), method->TakeMojomMethod(),
          WrapPersistent(resolver), SharedStorageSetterMethod::kSet,
          start_time));
    } else {
      UpdateDocumentSharedStorage(execution_context, method->TakeMojomMethod(),
                                  resolver, SharedStorageSetterMethod::kSet,
                                  start_time);
    }
  } else {
    GetSharedStorageWorkletServiceClient(execution_context)
        ->SharedStorageUpdate(
            method->TakeMojomMethod(),
            WTF::BindOnce(&OnSharedStorageUpdateFinished,
                          WrapPersistent(resolver), WrapPersistent(this),
                          SharedStorageSetterMethod::kSet,
                          GlobalScope::kSharedStorageWorklet, start_time));
  }

  return promise;
}

ScriptPromise<IDLAny> SharedStorage::append(ScriptState* script_state,
                                            const String& key,
                                            const String& value,
                                            ExceptionState& exception_state) {
  return append(script_state, key, value,
                SharedStorageModifierMethodOptions::Create(), exception_state);
}

ScriptPromise<IDLAny> SharedStorage::append(
    ScriptState* script_state,
    const String& key,
    const String& value,
    const SharedStorageModifierMethodOptions* options,
    ExceptionState& exception_state) {
  base::TimeTicks start_time = base::TimeTicks::Now();

  SharedStorageAppendMethod* method = SharedStorageAppendMethod::Create(
      script_state, key, value, options, exception_state);

  if (exception_state.HadException()) {
    return EmptyPromise();
  }

  ExecutionContext* execution_context = ExecutionContext::From(script_state);
  CHECK(execution_context->IsWindow() ||
        execution_context->IsSharedStorageWorkletGlobalScope());

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLAny>>(
      script_state, exception_state.GetContext());
  auto promise = resolver->Promise();

  if (auto* window = DynamicTo<LocalDOMWindow>(execution_context)) {
    if (window->document() && window->document()->IsPrerendering()) {
      window->document()->AddPostPrerenderingActivationStep(WTF::BindOnce(
          &SharedStorage::UpdateDocumentSharedStorage, WrapWeakPersistent(this),
          WrapWeakPersistent(execution_context), method->TakeMojomMethod(),
          WrapPersistent(resolver), SharedStorageSetterMethod::kAppend,
          start_time));
    } else {
      UpdateDocumentSharedStorage(execution_context, method->TakeMojomMethod(),
                                  resolver, SharedStorageSetterMethod::kAppend,
                                  start_time);
    }
  } else {
    GetSharedStorageWorkletServiceClient(execution_context)
        ->SharedStorageUpdate(
            method->TakeMojomMethod(),
            WTF::BindOnce(&OnSharedStorageUpdateFinished,
                          WrapPersistent(resolver), WrapPersistent(this),
                          SharedStorageSetterMethod::kAppend,
                          GlobalScope::kSharedStorageWorklet, start_time));
  }

  return promise;
}

ScriptPromise<IDLAny> SharedStorage::Delete(ScriptState* script_state,
                                            const String& key,
                                            ExceptionState& exception_state) {
  return Delete(script_state, key, SharedStorageModifierMethodOptions::Create(),
                exception_state);
}

ScriptPromise<IDLAny> SharedStorage::Delete(
    ScriptState* script_state,
    const String& key,
    const SharedStorageModifierMethodOptions* options,
    ExceptionState& exception_state) {
  base::TimeTicks start_time = base::TimeTicks::Now();

  SharedStorageDeleteMethod* method = SharedStorageDeleteMethod::Create(
      script_state, key, options, exception_state);

  if (exception_state.HadException()) {
    return EmptyPromise();
  }

  ExecutionContext* execution_context = ExecutionContext::From(script_state);
  CHECK(execution_context->IsWindow() ||
        execution_context->IsSharedStorageWorkletGlobalScope());

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLAny>>(
      script_state, exception_state.GetContext());
  auto promise = resolver->Promise();

  if (auto* window = DynamicTo<LocalDOMWindow>(execution_context)) {
    if (window->document() && window->document()->IsPrerendering()) {
      window->document()->AddPostPrerenderingActivationStep(WTF::BindOnce(
          &SharedStorage::UpdateDocumentSharedStorage, WrapWeakPersistent(this),
          WrapWeakPersistent(execution_context), method->TakeMojomMethod(),
          WrapPersistent(resolver), SharedStorageSetterMethod::kDelete,
          start_time));
    } else {
      UpdateDocumentSharedStorage(execution_context, method->TakeMojomMethod(),
                                  resolver, SharedStorageSetterMethod::kDelete,
                                  start_time);
    }
  } else {
    GetSharedStorageWorkletServiceClient(execution_context)
        ->SharedStorageUpdate(
            method->TakeMojomMethod(),
            WTF::BindOnce(&OnSharedStorageUpdateFinished,
                          WrapPersistent(resolver), WrapPersistent(this),
                          SharedStorageSetterMethod::kDelete,
                          GlobalScope::kSharedStorageWorklet, start_time));
  }

  return promise;
}

ScriptPromise<IDLAny> SharedStorage::clear(ScriptState* script_state,
                                           ExceptionState& exception_state) {
  return clear(script_state, SharedStorageModifierMethodOptions::Create(),
               exception_state);
}

ScriptPromise<IDLAny> SharedStorage::clear(
    ScriptState* script_state,
    const SharedStorageModifierMethodOptions* options,
    ExceptionState& exception_state) {
  base::TimeTicks start_time = base::TimeTicks::Now();

  SharedStorageClearMethod* method =
      SharedStorageClearMethod::Create(script_state, options, exception_state);

  if (exception_state.HadException()) {
    return EmptyPromise();
  }

  ExecutionContext* execution_context = ExecutionContext::From(script_state);
  CHECK(execution_context->IsWindow() ||
        execution_context->IsSharedStorageWorkletGlobalScope());

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLAny>>(
      script_state, exception_state.GetContext());
  auto promise = resolver->Promise();

  if (auto* window = DynamicTo<LocalDOMWindow>(execution_context)) {
    if (window->document() && window->document()->IsPrerendering()) {
      window->document()->AddPostPrerenderingActivationStep(WTF::BindOnce(
          &SharedStorage::UpdateDocumentSharedStorage, WrapWeakPersistent(this),
          WrapWeakPersistent(execution_context), method->TakeMojomMethod(),
          WrapPersistent(resolver), SharedStorageSetterMethod::kClear,
          start_time));
    } else {
      UpdateDocumentSharedStorage(execution_context, method->TakeMojomMethod(),
                                  resolver, SharedStorageSetterMethod::kClear,
                                  start_time);
    }
  } else {
    GetSharedStorageWorkletServiceClient(execution_context)
        ->SharedStorageUpdate(
            method->TakeMojomMethod(),
            WTF::BindOnce(&OnSharedStorageUpdateFinished,
                          WrapPersistent(resolver), WrapPersistent(this),
                          SharedStorageSetterMethod::kClear,
                          GlobalScope::kSharedStorageWorklet, start_time));
  }

  return promise;
}

ScriptPromise<IDLAny> SharedStorage::batchUpdate(
    ScriptState* script_state,
    const HeapVector<Member<SharedStorageModifierMethod>>& methods,
    ExceptionState& exception_state) {
  return batchUpdate(script_state, methods,
                     SharedStorageModifierMethodOptions::Create(),
                     exception_state);
}

ScriptPromise<IDLAny> SharedStorage::batchUpdate(
    ScriptState* script_state,
    const HeapVector<Member<SharedStorageModifierMethod>>& methods,
    const SharedStorageModifierMethodOptions* options,
    ExceptionState& exception_state) {
  base::TimeTicks start_time = base::TimeTicks::Now();

  ExecutionContext* execution_context = ExecutionContext::From(script_state);
  CHECK(execution_context->IsWindow() ||
        execution_context->IsSharedStorageWorkletGlobalScope());

  if (!CheckBrowsingContextIsValid(*script_state, exception_state)) {
    return EmptyPromise();
  }

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLAny>>(
      script_state, exception_state.GetContext());
  auto promise = resolver->Promise();

  if (execution_context->IsWindow() &&
      execution_context->GetSecurityOrigin()->IsOpaque()) {
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidAccessError,
                                      kOpaqueContextOriginCheckErrorMessage);
    return promise;
  }

  if (!CheckSharedStoragePermissionsPolicy(*execution_context,
                                           exception_state)) {
    return promise;
  }

  Vector<network::mojom::blink::SharedStorageModifierMethodWithOptionsPtr>
      mojom_methods;
  for (auto& method : methods) {
    mojom_methods.push_back(method->CloneMojomMethod());
  }

  if (!IsValidSharedStorageBatchUpdateMethodsArgument(mojom_methods)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kDataError,
        network::kBatchUpdateMethodsArgumentValidationErrorMessage);
    return promise;
  }

  String with_lock = options->getWithLockOr(/*fallback_value=*/String());
  if (IsReservedLockName(with_lock)) {
    exception_state.ThrowDOMException(DOMExceptionCode::kDataError,
                                      network::kReservedLockNameErrorMessage);
    return promise;
  }

  std::optional<String> optional_with_lock =
      with_lock ? std::optional(with_lock) : std::nullopt;

  if (auto* window = DynamicTo<LocalDOMWindow>(execution_context)) {
    if (window->document() && window->document()->IsPrerendering()) {
      window->document()->AddPostPrerenderingActivationStep(WTF::BindOnce(
          &SharedStorage::BatchUpdateDocumentSharedStorage,
          WrapWeakPersistent(this), WrapWeakPersistent(execution_context),
          optional_with_lock, std::move(mojom_methods),
          WrapPersistent(resolver), start_time));
    } else {
      BatchUpdateDocumentSharedStorage(
          execution_context, std::move(optional_with_lock),
          std::move(mojom_methods), resolver, start_time);
    }
  } else {
    GetSharedStorageWorkletServiceClient(execution_context)
        ->SharedStorageBatchUpdate(
            std::move(mojom_methods), optional_with_lock,
            WTF::BindOnce(&OnSharedStorageUpdateFinished,
                          WrapPersistent(resolver), WrapPersistent(this),
                          SharedStorageSetterMethod::kBatchUpdate,
                          GlobalScope::kSharedStorageWorklet, start_time));
  }

  return promise;
}

ScriptPromise<IDLString> SharedStorage::get(ScriptState* script_state,
                                            const String& key,
                                            ExceptionState& exception_state) {
  base::TimeTicks start_time = base::TimeTicks::Now();
  ExecutionContext* execution_context = ExecutionContext::From(script_state);
  CHECK(execution_context->IsWindow() ||
        execution_context->IsSharedStorageWorkletGlobalScope());

  if (!CheckBrowsingContextIsValid(*script_state, exception_state)) {
    return EmptyPromise();
  }

  ScriptPromiseResolver<IDLString>* resolver =
      MakeGarbageCollected<ScriptPromiseResolver<IDLString>>(
          script_state, exception_state.GetContext());
  auto promise = resolver->Promise();

  if (execution_context->IsWindow()) {
    if (execution_context->GetSecurityOrigin()->IsOpaque()) {
      resolver->Reject(V8ThrowDOMException::CreateOrEmpty(
          script_state->GetIsolate(), DOMExceptionCode::kInvalidAccessError,
          kOpaqueContextOriginCheckErrorMessage));
      return promise;
    }

    if (!CanGetOutsideWorklet(script_state)) {
      resolver->Reject(V8ThrowDOMException::CreateOrEmpty(
          script_state->GetIsolate(), DOMExceptionCode::kOperationError,
          "Cannot call get() outside of a fenced frame."));
      return promise;
    }

    // By this point, we know we are inside a fenced frame, so log the use
    // counter here.
    UseCounter::Count(execution_context,
                      WebFeature::kSharedStorageGetInFencedFrame);

    if (!base::FeatureList::IsEnabled(
            blink::features::kFencedFramesLocalUnpartitionedDataAccess)) {
      RecordSharedStorageGetInFencedFrameOutcome(
          SharedStorageGetInFencedFrameOutcome::kFeatureDisabled);
      resolver->Reject(V8ThrowDOMException::CreateOrEmpty(
          script_state->GetIsolate(), DOMExceptionCode::kOperationError,
          "Cannot call get() in a fenced frame with feature "
          "FencedFramesLocalUnpartitionedDataAccess disabled."));
      return promise;
    }

    if (!execution_context->IsFeatureEnabled(
            network::mojom::PermissionsPolicyFeature::
                kFencedUnpartitionedStorageRead)) {
      RecordSharedStorageGetInFencedFrameOutcome(
          SharedStorageGetInFencedFrameOutcome::kPermissionDisabled);
      resolver->Reject(V8ThrowDOMException::CreateOrEmpty(
          script_state->GetIsolate(), DOMExceptionCode::kOperationError,
          "Cannot call get() in a fenced frame without the "
          "fenced-unpartitioned-storage-read Permissions Policy feature "
          "enabled."));
      return promise;
    }
  }

  CHECK(
      CheckSharedStoragePermissionsPolicy(*execution_context, exception_state));

  if (!network::IsValidSharedStorageKeyStringLength(key.length())) {
    resolver->Reject(V8ThrowDOMException::CreateOrEmpty(
        script_state->GetIsolate(), DOMExceptionCode::kDataError,
        "Length of the \"key\" parameter is not valid."));
    return promise;
  }

  std::string histogram_name = execution_context->IsWindow()
                                   ? "Storage.SharedStorage.Document.Timing.Get"
                                   : "Storage.SharedStorage.Worklet.Timing.Get";
  auto callback = WTF::BindOnce(
      [](ScriptPromiseResolver<IDLString>* resolver,
         SharedStorage* shared_storage, base::TimeTicks start_time,
         const std::string& histogram_name,
         mojom::blink::SharedStorageGetStatus status,
         const String& error_message, const String& value) {
        DCHECK(resolver);
        ScriptState* script_state = resolver->GetScriptState();

        if (status == mojom::blink::SharedStorageGetStatus::kError) {
          if (IsInParallelAlgorithmRunnable(resolver->GetExecutionContext(),
                                            script_state)) {
            ScriptState::Scope scope(script_state);
            resolver->Reject(V8ThrowDOMException::CreateOrEmpty(
                script_state->GetIsolate(), DOMExceptionCode::kOperationError,
                error_message));
          }
          return;
        }

        base::UmaHistogramMediumTimes(histogram_name,
                                      base::TimeTicks::Now() - start_time);

        if (status == mojom::blink::SharedStorageGetStatus::kSuccess) {
          resolver->Resolve(value);
          return;
        }

        CHECK_EQ(status, mojom::blink::SharedStorageGetStatus::kNotFound);
        resolver->Resolve();
      },
      WrapPersistent(resolver), WrapPersistent(this), start_time,
      histogram_name);

  if (execution_context->IsWindow()) {
    GetSharedStorageDocumentService(execution_context)
        ->SharedStorageGet(key, std::move(callback));
  } else {
    GetSharedStorageWorkletServiceClient(execution_context)
        ->SharedStorageGet(key, std::move(callback));
  }

  return promise;
}

ScriptPromise<IDLUnsignedLong> SharedStorage::length(
    ScriptState* script_state,
    ExceptionState& exception_state) {
  base::TimeTicks start_time = base::TimeTicks::Now();
  ExecutionContext* execution_context = ExecutionContext::From(script_state);
  CHECK(execution_context->IsSharedStorageWorkletGlobalScope());

  if (!CheckBrowsingContextIsValid(*script_state, exception_state)) {
    return EmptyPromise();
  }

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLUnsignedLong>>(
      script_state, exception_state.GetContext());
  auto promise = resolver->Promise();

  CHECK(
      CheckSharedStoragePermissionsPolicy(*execution_context, exception_state));

  GetSharedStorageWorkletServiceClient(execution_context)
      ->SharedStorageLength(WTF::BindOnce(
          [](ScriptPromiseResolver<IDLUnsignedLong>* resolver,
             SharedStorage* shared_storage, base::TimeTicks start_time,
             bool success, const String& error_message, uint32_t length) {
            DCHECK(resolver);
            ScriptState* script_state = resolver->GetScriptState();

            if (!success) {
              if (IsInParallelAlgorithmRunnable(resolver->GetExecutionContext(),
                                                script_state)) {
                ScriptState::Scope scope(script_state);
                resolver->Reject(V8ThrowDOMException::CreateOrEmpty(
                    script_state->GetIsolate(),
                    DOMExceptionCode::kOperationError, error_message));
              }
              return;
            }

            base::UmaHistogramMediumTimes(
                "Storage.SharedStorage.Worklet.Timing.Length",
                base::TimeTicks::Now() - start_time);

            resolver->Resolve(length);
          },
          WrapPersistent(resolver), WrapPersistent(this), start_time));

  return promise;
}

ScriptPromise<IDLDouble> SharedStorage::remainingBudget(
    ScriptState* script_state,
    ExceptionState& exception_state) {
  base::TimeTicks start_time = base::TimeTicks::Now();
  ExecutionContext* execution_context = ExecutionContext::From(script_state);
  CHECK(execution_context->IsSharedStorageWorkletGlobalScope());

  if (!CheckBrowsingContextIsValid(*script_state, exception_state)) {
    return EmptyPromise();
  }

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLDouble>>(
      script_state, exception_state.GetContext());
  ScriptPromise<IDLDouble> promise = resolver->Promise();

  CHECK(
      CheckSharedStoragePermissionsPolicy(*execution_context, exception_state));

  GetSharedStorageWorkletServiceClient(execution_context)
      ->SharedStorageRemainingBudget(WTF::BindOnce(
          [](ScriptPromiseResolver<IDLDouble>* resolver,
             SharedStorage* shared_storage, base::TimeTicks start_time,
             bool success, const String& error_message, double bits) {
            DCHECK(resolver);
            ScriptState* script_state = resolver->GetScriptState();

            if (!success) {
              if (IsInParallelAlgorithmRunnable(resolver->GetExecutionContext(),
                                                script_state)) {
                ScriptState::Scope scope(script_state);
                resolver->Reject(V8ThrowDOMException::CreateOrEmpty(
                    script_state->GetIsolate(),
                    DOMExceptionCode::kOperationError, error_message));
              }
              return;
            }

            base::UmaHistogramMediumTimes(
                "Storage.SharedStorage.Worklet.Timing.RemainingBudget",
                base::TimeTicks::Now() - start_time);

            resolver->Resolve(bits);
          },
          WrapPersistent(resolver), WrapPersistent(this), start_time));

  return promise;
}

ScriptValue SharedStorage::context(ScriptState* script_state,
                                   ExceptionState& exception_state) const {
  ExecutionContext* execution_context = ExecutionContext::From(script_state);
  CHECK(execution_context->IsSharedStorageWorkletGlobalScope());

  if (!CheckBrowsingContextIsValid(*script_state, exception_state)) {
    return ScriptValue();
  }

  const String& embedder_context =
      To<SharedStorageWorkletGlobalScope>(execution_context)
          ->embedder_context();

  if (!embedder_context) {
    base::UmaHistogramBoolean("Storage.SharedStorage.Worklet.Context.IsDefined",
                              false);
    return ScriptValue();
  }

  base::UmaHistogramBoolean("Storage.SharedStorage.Worklet.Context.IsDefined",
                            true);
  return ScriptValue(script_state->GetIsolate(),
                     V8String(script_state->GetIsolate(), embedder_context));
}

ScriptPromise<V8SharedStorageResponse> SharedStorage::selectURL(
    ScriptState* script_state,
    const String& name,
    HeapVector<Member<SharedStorageUrlWithMetadata>> urls,
    ExceptionState& exception_state) {
  return selectURL(script_state, name, urls,
                   SharedStorageRunOperationMethodOptions::Create(),
                   exception_state);
}

ScriptPromise<V8SharedStorageResponse> SharedStorage::selectURL(
    ScriptState* script_state,
    const String& name,
    HeapVector<Member<SharedStorageUrlWithMetadata>> urls,
    const SharedStorageRunOperationMethodOptions* options,
    ExceptionState& exception_state) {
  SharedStorageWorklet* shared_storage_worklet =
      worklet(script_state, exception_state);
  CHECK(shared_storage_worklet);

  return shared_storage_worklet->selectURL(script_state, name, urls, options,
                                           exception_state);
}

ScriptPromise<IDLAny> SharedStorage::run(ScriptState* script_state,
                                         const String& name,
                                         ExceptionState& exception_state) {
  return run(script_state, name,
             SharedStorageRunOperationMethodOptions::Create(), exception_state);
}

ScriptPromise<IDLAny> SharedStorage::run(
    ScriptState* script_state,
    const String& name,
    const SharedStorageRunOperationMethodOptions* options,
    ExceptionState& exception_state) {
  SharedStorageWorklet* shared_storage_worklet =
      worklet(script_state, exception_state);
  CHECK(shared_storage_worklet);

  return shared_storage_worklet->run(script_state, name, options,
                                     exception_state);
}

ScriptPromise<SharedStorageWorklet> SharedStorage::createWorklet(
    ScriptState* script_state,
    const String& module_url,
    const SharedStorageWorkletOptions* options,
    ExceptionState& exception_state) {
  SharedStorageWorklet* worklet = SharedStorageWorklet::Create(script_state);
  auto* resolver =
      MakeGarbageCollected<ScriptPromiseResolver<SharedStorageWorklet>>(
          script_state);
  auto promise = resolver->Promise();
  std::tuple<SharedStorageDataOrigin, scoped_refptr<SecurityOrigin>>
      parse_results = ParseDataOrigin(options->dataOrigin());
  SharedStorageDataOrigin data_origin_type = std::get<0>(parse_results);
  if (data_origin_type == SharedStorageDataOrigin::kInvalid) {
    resolver->Reject(v8::Exception::TypeError(V8String(
        script_state->GetIsolate(),
        "The \"dataOrigin\" parameter is not a valid keyword or URL.")));
    return promise;
  }

  // We intentionally allow the implicit downcast of `options` to a
  // `WorkletOptions*` here.
  //
  // Note that we currently ignore the `dataOrigin` option that we've parsed
  // into `data_origin_type`, except to gate a use counter invoked in
  // `SharedStorageWorklet::AddModuleHelper()`.
  worklet->AddModuleHelper(script_state, resolver, module_url, options,
                           exception_state, /*resolve_to_worklet=*/true,
                           data_origin_type,
                           std::move(std::get<1>(parse_results)));
  return promise;
}

SharedStorageWorklet* SharedStorage::worklet(ScriptState* script_state,
                                             ExceptionState& exception_state) {
  if (!shared_storage_worklet_) {
    shared_storage_worklet_ = SharedStorageWorklet::Create(script_state);
  }

  return shared_storage_worklet_.Get();
}

PairAsyncIterable<SharedStorage>::IterationSource*
SharedStorage::CreateIterationSource(
    ScriptState* script_state,
    typename PairAsyncIterable<SharedStorage>::IterationSource::Kind kind,
    ExceptionState& exception_state) {
  ExecutionContext* execution_context = ExecutionContext::From(script_state);

  if (!CheckBrowsingContextIsValid(*script_state, exception_state)) {
    return nullptr;
  }

  return MakeGarbageCollected<IterationSource>(
      script_state, execution_context, kind,
      GetSharedStorageWorkletServiceClient(execution_context));
}

}  // namespace blink
