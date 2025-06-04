// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/glic/host/glic_actor_controller.h"

#include <memory>

#include "base/feature_list.h"
#include "base/location.h"
#include "base/metrics/histogram_macros.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/actor/actor_coordinator.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/actor/actor_task.h"
#include "chrome/browser/glic/host/context/glic_page_context_fetcher.h"
#include "chrome/browser/glic/host/glic.mojom.h"
#include "chrome/common/actor.mojom.h"
#include "chrome/common/actor/action_result.h"
#include "chrome/common/chrome_features.h"
#include "components/optimization_guide/proto/features/actions_data.pb.h"
#include "components/tabs/public/tab_interface.h"

namespace glic {

namespace {

using optimization_guide::proto::ActionInformation;

mojom::ActInFocusedTabResultPtr MakeActErrorResult(
    mojom::ActInFocusedTabErrorReason error_reason) {
  mojom::ActInFocusedTabResultPtr result =
      mojom::ActInFocusedTabResult::NewErrorReason(error_reason);
  UMA_HISTOGRAM_ENUMERATION("Glic.Action.ActInFocusedTabErrorReason",
                            result->get_error_reason());
  return result;
}

void PostTaskForActCallback(
    mojom::WebClientHandler::ActInFocusedTabCallback callback,
    mojom::ActInFocusedTabErrorReason error_reason) {
  mojom::ActInFocusedTabResultPtr result = MakeActErrorResult(error_reason);

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), std::move(result)));
}

void PostTaskForActionResultCallback(
    actor::ActorCoordinator::ActionResultCallback callback,
    actor::mojom::ActionResultPtr result) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), std::move(result)));
}

void OnGetContextFromFocusedTab(
    mojom::WebClientHandler::ActInFocusedTabCallback callback,
    mojom::GetContextResultPtr tab_context_result) {
  if (tab_context_result->is_error_reason()) {
    mojom::ActInFocusedTabResultPtr result = MakeActErrorResult(
        mojom::ActInFocusedTabErrorReason::kGetContextFailed);
    std::move(callback).Run(std::move(result));
    return;
  }

  mojom::ActInFocusedTabResultPtr result =
      mojom::ActInFocusedTabResult::NewActInFocusedTabResponse(
          mojom::ActInFocusedTabResponse::New(
              std::move(tab_context_result->get_tab_context())));

  std::move(callback).Run(std::move(result));
}

void MaybeWarnMultiTaskNotImplemented(actor::TaskId task_id) {
  if (task_id) {
    NOTIMPLEMENTED() << "Multi-task not implemented.";
  }
}

}  // namespace

GlicActorController::GlicActorController(Profile* profile) : profile_(profile) {
  CHECK(profile_);
  actor::ActorCoordinator::RegisterWithProfile(profile_);
}

GlicActorController::~GlicActorController() = default;

void GlicActorController::Act(
    const FocusedTabData& focused_tab_data,
    const optimization_guide::proto::BrowserAction& action,
    const mojom::GetTabContextOptions& options,
    mojom::WebClientHandler::ActInFocusedTabCallback callback) {
  if (!actor_task_ ||
      actor_task_->GetState() == actor::ActorTask::State::kFinished) {
    auto task = std::make_unique<actor::ActorTask>(
        std::make_unique<actor::ActorCoordinator>(profile_));
    actor_task_ = task.get();
    actor::ActorKeyedService::Get(profile_.get())->AddTask(std::move(task));
  }

  if (!GetActorCoordinator() || !GetActorCoordinator()->HasTask()) {
    // Start of a new task, which must begin with a navigate action.
    // The OnTaskStarted callback will perform the action after the task is
    // setup by the coordinator.
    // TODO(https://crbug.com/407860715): Implement a separate host API to start
    // a task, and remove action handling here.
    GetActorCoordinator()->StartTask(
        action,
        base::BindOnce(&GlicActorController::OnTaskStarted, GetWeakPtr(),
                       action, options, std::move(callback)),
        /*tab_id=*/std::nullopt);
    return;
  }

  ActImpl(focused_tab_data.is_focus() ? focused_tab_data.focus()->GetWeakPtr()
                                      : nullptr,
          action, options, std::move(callback));
}

// TODO(mcnee): Determine if we need additional mechanisms, within the browser,
// to stop a task.
void GlicActorController::StopTask(actor::TaskId task_id) {
  MaybeWarnMultiTaskNotImplemented(task_id);
  if (!GetActorCoordinator() ||
      actor_task_->GetState() == actor::ActorTask::State::kFinished) {
    return;
  }
  GetActorCoordinator()->StopTask();
  actor_task_->SetState(actor::ActorTask::State::kFinished);
}

void GlicActorController::PauseTask(actor::TaskId task_id) {
  MaybeWarnMultiTaskNotImplemented(task_id);
  if (!GetActorCoordinator() ||
      actor_task_->GetState() == actor::ActorTask::State::kFinished) {
    return;
  }
  GetActorCoordinator()->PauseTask();
  actor_task_->SetState(actor::ActorTask::State::kPausedByClient);
}

void GlicActorController::ResumeTask(
    actor::TaskId task_id,
    const mojom::GetTabContextOptions& context_options,
    glic::mojom::WebClientHandler::ResumeActorTaskCallback callback) {
  MaybeWarnMultiTaskNotImplemented(task_id);
  if (!GetActorCoordinator() ||
      actor_task_->GetState() == actor::ActorTask::State::kFinished) {
    std::move(callback).Run(mojom::GetContextResult::NewErrorReason(
        std::string("task does not exist")));
    return;
  }

  if (!actor_task_->IsPaused()) {
    std::move(callback).Run(mojom::GetContextResult::NewErrorReason(
        std::string("task is not paused")));
    return;
  }

  tabs::TabInterface* tab_of_resumed_task =
      GetActorCoordinator()->GetTabOfCurrentTask();
  if (!tab_of_resumed_task) {
    std::move(callback).Run(mojom::GetContextResult::NewErrorReason(
        std::string("tab does not exist")));
    return;
  }

  actor_task_->SetState(actor::ActorTask::State::kReflecting);
  FetchPageContext(tab_of_resumed_task, context_options,
                   /*include_actionable_data=*/true, std::move(callback));
}

bool GlicActorController::IsActorCoordinatorActingOnTab(
    const content::WebContents* tab) const {
  return GetActorCoordinator() && GetActorCoordinator()->HasTaskForTab(tab) &&
         actor_task_->GetState() != actor::ActorTask::State::kFinished;
}

actor::ActorCoordinator& GlicActorController::GetActorCoordinatorForTesting() {
  if (!actor_task_) {
    auto task = std::make_unique<actor::ActorTask>(
        std::make_unique<actor::ActorCoordinator>(profile_));
    actor_task_ = task.get();
    actor::ActorKeyedService::Get(profile_.get())->AddTask(std::move(task));
  }
  return *actor_task_->GetActorCoordinator();
}

void GlicActorController::OnTaskStarted(
    const optimization_guide::proto::BrowserAction& action,
    const mojom::GetTabContextOptions& options,
    mojom::WebClientHandler::ActInFocusedTabCallback act_callback,
    base::WeakPtr<tabs::TabInterface> tab) const {
  if (!tab) {
    PostTaskForActCallback(std::move(act_callback),
                           mojom::ActInFocusedTabErrorReason::kTargetNotFound);
    return;
  }
  // TODO(https://crbug.com/402086398): Check that the tab is valid for action,
  // maybe reusing the validation in GlicFocusedTabManager.
  ActImpl(tab, action, options, std::move(act_callback));
}

void GlicActorController::ActImpl(
    base::WeakPtr<tabs::TabInterface> tab,
    const optimization_guide::proto::BrowserAction& action,
    const mojom::GetTabContextOptions& options,
    mojom::WebClientHandler::ActInFocusedTabCallback callback) const {
  actor::ActorCoordinator::ActionResultCallback action_callback =
      base::BindOnce(&GlicActorController::OnActionFinished, GetWeakPtr(), tab,
                     options, std::move(callback));

  if (actor_task_->IsPaused()) {
    VLOG(1) << "Unable to perform action: task is paused";
    PostTaskForActionResultCallback(
        std::move(action_callback),
        actor::MakeResult(actor::mojom::ActionResultCode::kError,
                          "Task is paused"));
    return;
  }

  GetActorCoordinator()->Act(action, std::move(action_callback));
}

void GlicActorController::OnActionFinished(
    base::WeakPtr<tabs::TabInterface> tab,
    const mojom::GetTabContextOptions& options,
    mojom::WebClientHandler::ActInFocusedTabCallback callback,
    actor::mojom::ActionResultPtr result) const {
  if (!actor::IsOk(*result)) {
    PostTaskForActCallback(std::move(callback),
                           mojom::ActInFocusedTabErrorReason::kTargetNotFound);
    return;
  }

  // TODO(https://crbug.com/398271171): Remove when the actor coordinator
  // handles getting a new observation.
  // TODO(https://crbug.com/402086398): Figure out if/how this can be shared
  // with GlicKeyedService::GetContextFromFocusedTab(). It's not clear yet if
  // the same permission checks, etc. should apply here.
  if (tab) {
    FetchPageContext(
        tab.get(), options, /*include_actionable_data=*/true,
        base::BindOnce(OnGetContextFromFocusedTab, std::move(callback)));
  } else {
    PostTaskForActCallback(std::move(callback),
                           mojom::ActInFocusedTabErrorReason::kTargetNotFound);
  }
}

base::WeakPtr<const GlicActorController> GlicActorController::GetWeakPtr()
    const {
  return weak_ptr_factory_.GetWeakPtr();
}

base::WeakPtr<GlicActorController> GlicActorController::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

actor::ActorCoordinator* GlicActorController::GetActorCoordinator() const {
  if (!actor_task_) {
    return nullptr;
  }
  return actor_task_->GetActorCoordinator();
}
}  // namespace glic
