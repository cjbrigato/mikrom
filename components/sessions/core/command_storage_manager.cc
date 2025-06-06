// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sessions/core/command_storage_manager.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/threading/thread.h"
#include "base/values.h"
#include "components/sessions/core/command_storage_backend.h"
#include "components/sessions/core/command_storage_manager_delegate.h"
#include "components/sessions/core/session_command.h"
#include "crypto/random.h"

namespace sessions {
namespace {

// Delay between when a command is received, and when we save it to the
// backend.
constexpr base::TimeDelta kSaveDelay = base::Milliseconds(2500);

void AdaptGetLastSessionCommands(
    CommandStorageManager::GetCommandsCallback callback,
    CommandStorageBackend::ReadCommandsResult result) {
  std::move(callback).Run(std::move(result.commands), result.error_reading);
}

#if DCHECK_IS_ON()
base::Value CommandToDebugValue(const SessionCommand& command) {
  // There is no convenience function to transform the pickled contents into the
  // payload struct itself, so just output the id for now.
  return base::Value(command.id());
}
#endif  // DCHECK_IS_ON()

}  // namespace

CommandStorageManager::CommandStorageManager(
    SessionType type,
    const base::FilePath& path,
    CommandStorageManagerDelegate* delegate,
    bool enable_crypto,
    const std::vector<uint8_t>& decryption_key,
    scoped_refptr<base::SequencedTaskRunner> backend_task_runner)
    : backend_(base::MakeRefCounted<CommandStorageBackend>(
          backend_task_runner ? backend_task_runner
                              : CreateDefaultBackendTaskRunner(),
          path,
          type,
          decryption_key)),
      use_crypto_(enable_crypto),
      delegate_(delegate),
      backend_task_runner_(backend_->owning_task_runner()) {}

CommandStorageManager::~CommandStorageManager() = default;

// static
scoped_refptr<base::SequencedTaskRunner>
CommandStorageManager::CreateDefaultBackendTaskRunner() {
  return base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskShutdownBehavior::BLOCK_SHUTDOWN});
}

// static
std::vector<uint8_t> CommandStorageManager::CreateCryptoKey() {
  return crypto::RandBytesAsVector(32);
}

void CommandStorageManager::ScheduleCommand(
    std::unique_ptr<SessionCommand> command) {
  DCHECK(command);
  commands_since_reset_++;
  pending_commands_.push_back(std::move(command));
  StartSaveTimer();
}

void CommandStorageManager::AppendRebuildCommand(
    std::unique_ptr<SessionCommand> command) {
  std::vector<std::unique_ptr<SessionCommand>> commands;
  commands.push_back(std::move(command));
  AppendRebuildCommands(std::move(commands));
}

void CommandStorageManager::AppendRebuildCommands(
    std::vector<std::unique_ptr<SessionCommand>> commands) {
  commands_since_reset_ += commands.size();
  pending_commands_.insert(pending_commands_.end(),
                           std::make_move_iterator(commands.begin()),
                           std::make_move_iterator(commands.end()));
}

void CommandStorageManager::EraseCommand(SessionCommand* old_command) {
  auto it = std::ranges::find(pending_commands_, old_command,
                              &std::unique_ptr<SessionCommand>::get);
  CHECK(it != pending_commands_.end());
  pending_commands_.erase(it);
  DCHECK_GT(commands_since_reset_, 0);
  --commands_since_reset_;
}

void CommandStorageManager::SwapCommand(
    SessionCommand* old_command,
    std::unique_ptr<SessionCommand> new_command) {
  auto it = std::ranges::find(pending_commands_, old_command,
                              &std::unique_ptr<SessionCommand>::get);
  CHECK(it != pending_commands_.end());
  *it = std::move(new_command);
}

void CommandStorageManager::ClearPendingCommands() {
  DCHECK_GE(commands_since_reset_, static_cast<int>(pending_commands_.size()));
  commands_since_reset_ -= static_cast<int>(pending_commands_.size());
  pending_commands_.clear();
}

void CommandStorageManager::StartSaveTimer() {
  // Don't start a timer when testing.
  if (delegate_->ShouldUseDelayedSave() &&
      base::SingleThreadTaskRunner::HasCurrentDefault() && !HasPendingSave()) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&CommandStorageManager::Save,
                       weak_factory_for_timer_.GetWeakPtr()),
        kSaveDelay);
  }
}

void CommandStorageManager::Save() {
  weak_factory_for_timer_.InvalidateWeakPtrs();

  // Inform the delegate that we will save the commands now, giving it the
  // opportunity to append more commands.
  delegate_->OnWillSaveCommands();

  if (pending_commands_.empty())
    return;

#if DCHECK_IS_ON()
  for (const std::unique_ptr<SessionCommand>& command : pending_commands_) {
    written_commands_reverse_debug_log_.push_front(
        CommandToDebugValue(*command));
  }
  if (written_commands_reverse_debug_log_.size() > kMaxLogSize) {
    written_commands_reverse_debug_log_.resize(kMaxLogSize);
  }
#endif  // DCHECK_IS_ON()

  std::vector<uint8_t> crypto_key;
  if (use_crypto_ && pending_reset_) {
    crypto_key = CreateCryptoKey();
    delegate_->OnGeneratedNewCryptoKey(crypto_key);
  }
  auto error_callback = base::BindOnce(
      &CommandStorageManager::OnErrorWritingToFile, weak_factory_.GetWeakPtr());
  backend_task_runner_->PostNonNestableTask(
      FROM_HERE,
      base::BindOnce(&CommandStorageBackend::AppendCommands, backend_,
                     std::move(pending_commands_), pending_reset_,
                     std::move(error_callback), crypto_key));
  if (pending_reset_) {
    commands_since_reset_ = 0;
    pending_reset_ = false;
  }
}

bool CommandStorageManager::HasPendingSave() const {
  return weak_factory_for_timer_.HasWeakPtrs();
}

void CommandStorageManager::MoveCurrentSessionToLastSession() {
  Save();
  backend_task_runner_->PostNonNestableTask(
      FROM_HERE,
      base::BindOnce(&CommandStorageBackend::MoveCurrentSessionToLastSession,
                     backend()));
}

void CommandStorageManager::DeleteLastSession() {
  backend_task_runner_->PostNonNestableTask(
      FROM_HERE,
      base::BindOnce(&CommandStorageBackend::DeleteLastSession, backend()));
}

void CommandStorageManager::GetLastSessionCommands(
    GetCommandsCallback callback) {
  backend_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&CommandStorageBackend::ReadLastSessionCommands,
                     backend()),
      base::BindOnce(&AdaptGetLastSessionCommands, std::move(callback)));
}

#if DCHECK_IS_ON()
base::Value CommandStorageManager::ToDebugValue() const {
  base::Value::Dict debug_value;
  debug_value.Set("use_crypto", use_crypto_);
  for (const std::unique_ptr<SessionCommand>& command : pending_commands_) {
    debug_value.EnsureList("pending_commands")
        ->Append(CommandToDebugValue(*command));
  }
  debug_value.Set("pending_reset", pending_reset_);
  debug_value.Set("commands_since_reset", commands_since_reset_);
  for (const base::Value& log_item : written_commands_reverse_debug_log_) {
    debug_value.EnsureList("written_commands_reverse_debug_log")
        ->Append(log_item.Clone());
  }
  return base::Value(std::move(debug_value));
}
#endif  // DCHECK_IS_ON()

void CommandStorageManager::OnErrorWritingToFile() {
  delegate_->OnErrorWritingSessionCommands();
}

}  // namespace sessions
