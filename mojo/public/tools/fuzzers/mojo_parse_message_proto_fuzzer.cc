// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/390223051): Remove C-library calls to fix the errors.
#pragma allow_unsafe_libc_calls
#endif

// Implementation of a proto version of mojo_parse_message_fuzzer that sends
// multiple messages per run.

#include "build/build_config.h"
#if BUILDFLAG(IS_WIN)
#include "base/at_exit.h"
#endif  // BUILDFLAG(IS_WIN)
#include "base/functional/bind.h"
#include "base/message_loop/message_pump_type.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/system/message.h"
#include "mojo/public/tools/fuzzers/fuzz_impl.h"
#include "mojo/public/tools/fuzzers/mojo_fuzzer.pb.h"
#include "testing/libfuzzer/proto/lpm_interface.h"

namespace mojo_proto_fuzzer {

void FuzzMessage(const MojoFuzzerMessages& mojo_fuzzer_messages,
                 base::RunLoop* run) {
  mojo::PendingRemote<fuzz::mojom::FuzzInterface> fuzz;
  auto impl = std::make_unique<FuzzImpl>(fuzz.InitWithNewPipeAndPassReceiver());
  auto router = impl->receiver_.internal_state()->RouterForTesting();

  for (auto& message_str : mojo_fuzzer_messages.messages()) {
    // Create a mojo message with the appropriate payload size.
    mojo::ScopedMessageHandle handle;
    mojo::CreateMessage(&handle, MOJO_CREATE_MESSAGE_FLAG_NONE);
    MojoAppendMessageDataOptions options = {
        .struct_size = sizeof(options),
        .flags = MOJO_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE};
    void* buffer;
    uint32_t buffer_size;
    MojoAppendMessageData(handle->value(),
                          static_cast<uint32_t>(message_str.size()), nullptr, 0,
                          &options, &buffer, &buffer_size);
    CHECK_GE(buffer_size, static_cast<uint32_t>(message_str.size()));
    memcpy(buffer, message_str.data(), message_str.size());

    // Run the message through header validation, payload validation, and
    // dispatch to the impl.
    router->SimulateReceivingMessageForTesting(std::move(handle));
  }

  // Allow the harness function to return now.
  run->Quit();
}

// Environment for the fuzzer. Initializes the mojo EDK and sets up a
// ThreadPool, because Mojo messages must be sent and processed from
// TaskRunners.
struct Environment {
  Environment() : main_task_executor(base::MessagePumpType::UI) {
    base::ThreadPoolInstance::CreateAndStartWithDefaultParams(
        "MojoParseMessageFuzzerProcess");
    mojo::core::Init();
  }

#if BUILDFLAG(IS_WIN)
  // Windows thread executor has a dependency on AtExitManager.
  std::unique_ptr<base::AtExitManager> at_exit_manager_ =
      std::make_unique<base::AtExitManager>();
#endif  // BUILDFLAG(IS_WIN)

  // Task executor to send and handle messages on.
  base::SingleThreadTaskExecutor main_task_executor;

  // Suppress mojo validation failure logs.
  mojo::internal::ScopedSuppressValidationErrorLoggingForTests log_suppression;
};

DEFINE_PROTO_FUZZER(const MojoFuzzerMessages& mojo_fuzzer_messages) {
  static Environment* env = new Environment();
  // Pass the data along to run on a SingleThreadTaskExecutor, and wait for it
  // to finish.
  base::RunLoop run;
  env->main_task_executor.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&FuzzMessage, mojo_fuzzer_messages, &run));
  run.Run();
}
}  // namespace mojo_proto_fuzzer
