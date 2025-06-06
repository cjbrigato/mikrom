// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/tiles/image_controller.h"

#include <utility>

#include "base/auto_reset.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/trace_event/trace_event.h"
#include "cc/base/completion_event.h"
#include "cc/tiles/tile_task_manager.h"

namespace cc {

ImageController::ImageDecodeRequestId
    ImageController::s_next_image_decode_queue_id_ = 1;

ImageController::ImageController(
    scoped_refptr<base::SequencedTaskRunner> origin_task_runner,
    scoped_refptr<base::SequencedTaskRunner> worker_task_runner,
    base::RepeatingCallback<void(scoped_refptr<TileTask>)>
        notify_external_dependent)
    : worker_task_runner_(std::move(worker_task_runner)),
      notify_external_dependent_(std::move(notify_external_dependent)) {
  worker_state_ = std::make_unique<WorkerState>(std::move(origin_task_runner),
                                                weak_ptr_factory_.GetWeakPtr());
  // base::Unretained is safe because `worker_state_` is guaranteed to be
  // deleted from a task posted to `worker_task_runner_` after any scheduled
  // invocation of worker_task_ is finished (see ~ImageController).
  worker_task_ = base::BindRepeating(
      &ImageController::ProcessNextImageDecodeOnWorkerThread,
      base::Unretained(worker_state_.get()));
}

ImageController::~ImageController() {
  StopWorkerTasks();
  for (auto& request : orphaned_decode_requests_)
    std::move(request.callback).Run(request.id, ImageDecodeResult::FAILURE);
  if (worker_task_runner_) {
    // Delete `worker_state_` on `worker_task_runner_` (or elsewhere via the
    // callback's destructor if `worker_task_runner_` stopped accepting tasks).
    worker_task_runner_->PostTask(
        FROM_HERE, base::DoNothingWithBoundArgs(std::move(worker_state_)));
  }
}

ImageController::WorkerState::WorkerState(
    scoped_refptr<base::SequencedTaskRunner> origin_task_runner,
    base::WeakPtr<ImageController> weak_ptr)
    : origin_task_runner(std::move(origin_task_runner)), weak_ptr(weak_ptr) {}
ImageController::WorkerState::~WorkerState() = default;

void ImageController::ForEachDecodeRequest(
    base::FunctionRef<void(ImageDecodeRequest&)> func) {
  worker_state_->lock.AssertAcquired();
  std::ranges::for_each(
      worker_state_->image_decode_queue.begin(),
      worker_state_->image_decode_queue.end(), func,
      &std::pair<const ImageDecodeRequestId, ImageDecodeRequest>::second);
  std::ranges::for_each(orphaned_decode_requests_.begin(),
                        orphaned_decode_requests_.end(), func);
}

void ImageController::StopWorkerTasks() {
  // We can't have worker threads without a cache_ or a worker_task_runner_, so
  // terminate early.
  if (!cache_ || !worker_task_runner_)
    return;

  TileTask::Vector external_dependents;

  {
    base::AutoLock hold(worker_state_->lock);

    // If a worker task is running, post a task and wait for its completion to
    // "flush" the queue.
    while (worker_state_->task_state == WorkerTaskState::kRunningTask) {
      base::AutoUnlock release(worker_state_->lock);
      CompletionEvent completion_event;
      worker_task_runner_->PostTask(
          FROM_HERE, base::BindOnce(&CompletionEvent::Signal,
                                    base::Unretained(&completion_event)));
      completion_event.Wait();
    }

    // Now, begin cleanup.

    // Unlock all of the locked images (note that this vector would only be
    // populated if we actually need to unref the image.
    for (auto& image_pair : requested_locked_images_) {
      cache_->UnrefImage(image_pair.second);
    }
    requested_locked_images_.clear();

    // Now, complete the tasks that already ran but haven't completed. These
    // would be posted in the run loop, but since we invalidated the weak ptrs,
    // we need to run everything manually.
    for (auto& request_to_complete :
         worker_state_->requests_needing_completion) {
      ImageDecodeRequest& request = request_to_complete.second;

      // The task (if one exists) would have run already, we just need to make
      // sure it was completed. Multiple requests for the same image use the
      // same task so it could have already been completed.
      if (request.task && !request.task->HasCompleted()) {
        request.task->OnTaskCompleted();
        request.task->DidComplete();
        if (auto& dependent = request.task->external_dependent()) {
          external_dependents.push_back(std::move(dependent));
        }
      }

      if (request.need_unref) {
        cache_->UnrefImage(request.draw_image);
      }

      // Orphan the request so that we can still run it when a new cache is set.
      request.task = nullptr;
      request.need_unref = false;
      orphaned_decode_requests_.push_back(std::move(request));
    }
    worker_state_->requests_needing_completion.clear();

    // Finally, complete all of the tasks that never started running. This is
    // similar to the |requests_needing_completion_|, but happens at a different
    // stage in the pipeline.
    for (auto& request_pair : worker_state_->image_decode_queue) {
      ImageDecodeRequest& request = request_pair.second;

      if (request.task) {
        // This task may have run via a different request, so only cancel it if
        // it's "new". That is, the same task could have been referenced by
        // several different image deque requests for the same image.
        if (request.task->state().IsNew()) {
          request.task->state().DidCancel();
        }

        if (!request.task->HasCompleted()) {
          request.task->OnTaskCompleted();
          request.task->DidComplete();
          if (auto& dependent = request.task->external_dependent()) {
            external_dependents.push_back(std::move(dependent));
          }
        }
      }

      if (request.need_unref) {
        cache_->UnrefImage(request.draw_image);
      }

      // Orphan the request so that we can still run it when a new cache is set.
      request.task = nullptr;
      request.need_unref = false;
      orphaned_decode_requests_.push_back(std::move(request));
    }
    worker_state_->image_decode_queue.clear();
  }

  for (auto& dependent : external_dependents) {
    dependent->ExternalDependencyCompleted();
    notify_external_dependent_.Run(dependent);
  }
}

bool ImageController::HasReadyToRunTask() const {
  worker_state_->lock.AssertAcquired();
  return std::ranges::any_of(
      worker_state_->image_decode_queue.begin(),
      worker_state_->image_decode_queue.end(),
      [](const ImageDecodeRequest& request) -> bool {
        return !request.has_external_dependency;
      },
      &std::pair<const ImageDecodeRequestId, ImageDecodeRequest>::second);
}

bool ImageController::HasReadyToRunTaskForTesting() const {
  base::AutoLock hold(worker_state_->lock);
  return HasReadyToRunTask();
}

void ImageController::FlushDecodeTasksForTesting() {
  TileTask::Vector external_dependents;
  std::vector<base::OnceClosure> callbacks;
  {
    base::AutoLock hold(worker_state_->lock);
    // If a worker task is running, post a task and wait for its completion to
    // "flush" the queue.
    while (worker_state_->task_state != WorkerTaskState::kNoTask) {
      base::AutoUnlock release(worker_state_->lock);
      CompletionEvent completion_event;
      worker_task_runner_->PostTask(
          FROM_HERE, base::BindOnce(&CompletionEvent::Signal,
                                    base::Unretained(&completion_event)));
      completion_event.Wait();
    }
    while (HasReadyToRunTask()) {
      ImageController::ProcessNextImageDecodeWithLock(worker_state_.get());
    }
    for (auto& request_to_complete :
         worker_state_->requests_needing_completion) {
      ImageDecodeRequest& request = request_to_complete.second;
      ImageDecodeResult result = CompleteTaskForRequest(request);
      if (request.task && request.task->external_dependent()) {
        external_dependents.emplace_back(
            std::move(request.task->external_dependent()));
      }
      callbacks.emplace_back(
          base::BindOnce(std::move(request.callback), request.id, result));
    }
    worker_state_->requests_needing_completion.clear();
  }
  for (auto& dependent : external_dependents) {
    dependent->ExternalDependencyCompleted();
    notify_external_dependent_.Run(dependent);
  }
  for (auto& callback : callbacks) {
    std::move(callback).Run();
  }
}

void ImageController::SetImageDecodeCache(ImageDecodeCache* cache) {
  DCHECK(!cache_ || !cache);

  if (!cache) {
    SetPredecodeImages(std::vector<DrawImage>(),
                       ImageDecodeCache::TracingInfo());
    StopWorkerTasks();
    image_cache_max_limit_bytes_ = 0u;
    image_cache_client_id_ = 0u;
  }

  cache_ = cache;

  if (cache_) {
    DCHECK_EQ(image_cache_client_id_, 0u);
    image_cache_client_id_ = cache_->GenerateClientId();
    image_cache_max_limit_bytes_ = cache_->GetMaximumMemoryLimitBytes();
    GenerateTasksForOrphanedRequests();
  }
}

void ImageController::ConvertImagesToTasks(
    std::vector<DrawImage>* sync_decoded_images,
    std::vector<scoped_refptr<TileTask>>* tasks,
    bool* has_at_raster_images,
    bool* has_hardware_accelerated_jpeg_candidates,
    bool* has_hardware_accelerated_webp_candidates,
    const ImageDecodeCache::TracingInfo& tracing_info) {
  DCHECK(cache_);
  *has_at_raster_images = false;
  *has_hardware_accelerated_jpeg_candidates = false;
  *has_hardware_accelerated_webp_candidates = false;

  // We may read/write stand-alone decode image tasks if they are duplicates of
  // raster tasks.
  base::AutoLock hold(worker_state_->lock);

  for (auto it = sync_decoded_images->begin();
       it != sync_decoded_images->end();) {
    // PaintWorklet images should not be included in this set; they have already
    // been painted before raster and so do not need raster-time work.
    DCHECK(!it->paint_image().IsPaintWorklet());

    ImageDecodeCache::TaskResult result = cache_->GetTaskForImageAndRef(
        image_cache_client_id_, *it, tracing_info);
    *has_at_raster_images |= result.is_at_raster_decode;

    ImageType image_type =
        it->paint_image().GetImageHeaderMetadata()
            ? it->paint_image().GetImageHeaderMetadata()->image_type
            : ImageType::kInvalid;
    *has_hardware_accelerated_jpeg_candidates |=
        (result.can_do_hardware_accelerated_decode &&
         image_type == ImageType::kJPEG);
    *has_hardware_accelerated_webp_candidates |=
        (result.can_do_hardware_accelerated_decode &&
         image_type == ImageType::kWEBP);

    if (result.task) {
      if (scoped_refptr<TileTask>& dependent =
              result.task->external_dependent()) {
        ForEachDecodeRequest([&dependent](ImageDecodeRequest& request) -> void {
          if (request.task == dependent) {
            request.has_external_dependency = true;
          }
        });
      }
      tasks->push_back(std::move(result.task));
    }
    if (result.need_unref) {
      ++it;
    } else {
      it = sync_decoded_images->erase(it);
    }
  }
}

void ImageController::UnrefImages(const std::vector<DrawImage>& images) {
  for (auto& image : images)
    cache_->UnrefImage(image);
}

void ImageController::ReduceMemoryUsage() {
  DCHECK(cache_);
  cache_->ReduceCacheUsage();
}

std::vector<scoped_refptr<TileTask>> ImageController::SetPredecodeImages(
    std::vector<DrawImage> images,
    const ImageDecodeCache::TracingInfo& tracing_info) {
  std::vector<scoped_refptr<TileTask>> new_tasks;
  // The images here are in a pre-decode area: we decode them in advance, but
  // they're not dependencies for raster tasks. If these images do end up
  // getting rasterized, we will still have a chance to record the raster
  // scheduling delay UMAs when we create and run the raster task.
  bool has_at_raster_images = false;
  bool has_hardware_accelerated_jpeg_candidates = false;
  bool has_hardware_accelerated_webp_candidates = false;
  ConvertImagesToTasks(&images, &new_tasks, &has_at_raster_images,
                       &has_hardware_accelerated_jpeg_candidates,
                       &has_hardware_accelerated_webp_candidates, tracing_info);
  UnrefImages(predecode_locked_images_);
  predecode_locked_images_ = std::move(images);
  return new_tasks;
}

ImageController::ImageDecodeRequestId ImageController::QueueImageDecode(
    const DrawImage& draw_image,
    ImageDecodedCallback callback,
    bool speculative) {
  // We must not receive any image requests if we have no worker.
  CHECK(worker_task_runner_);

  // Generate the next id.
  ImageDecodeRequestId id = s_next_image_decode_queue_id_++;

  DCHECK(draw_image.paint_image());
  bool is_image_lazy = draw_image.paint_image().IsLazyGenerated();

  // Get the tasks for this decode.
  ImageDecodeCache::TaskResult result(
      /*need_unref=*/false,
      /*is_at_raster_decode=*/false,
      /*can_do_hardware_accelerated_decode=*/false);
  if (is_image_lazy) {
    if (!cache_) {
      orphaned_decode_requests_.emplace_back(
          id, draw_image, std::move(callback), /*task=*/nullptr,
          /*need_unref=*/false, /*has_external_dependency=*/false);
      return id;
    }
    result = cache_->GetOutOfRasterDecodeTaskForImageAndRef(
        image_cache_client_id_, draw_image, speculative);
  }
  // If we don't need to unref this, we don't actually have a task.
  DCHECK(result.need_unref || !result.task);

  // Schedule the task and signal that there is more work.
  base::AutoLock hold(worker_state_->lock);
  bool has_external_dependency =
      result.task && !result.task->dependencies().empty();
  CHECK(!has_external_dependency ||
        result.task->dependencies()[0]->IsRasterTask());
  worker_state_->image_decode_queue[id] = ImageDecodeRequest(
      id, draw_image, std::move(callback), std::move(result.task),
      result.need_unref, has_external_dependency);
  ScheduleImageDecodeOnWorkerIfNeeded();

  return id;
}

void ImageController::ExternalDependencyCompletedForTask(
    scoped_refptr<TileTask> task) {
  base::AutoLock hold(worker_state_->lock);
  ForEachDecodeRequest([&task](ImageDecodeRequest& request) -> void {
    if (request.task == task) {
      request.has_external_dependency = false;
    }
  });
  ScheduleImageDecodeOnWorkerIfNeeded();
}

void ImageController::UnlockImageDecode(ImageDecodeRequestId id) {
  // If the image exists, ie we actually need to unlock it, then do so.
  auto it = requested_locked_images_.find(id);
  if (it == requested_locked_images_.end())
    return;

  UnrefImages({std::move(it->second)});
  requested_locked_images_.erase(it);
}

// static
void ImageController::ProcessNextImageDecodeOnWorkerThread(
    WorkerState* worker_state) {
  TRACE_EVENT0("cc", "ImageController::ProcessNextImageDecodeOnWorkerThread");

  base::AutoLock hold(worker_state->lock);
  DCHECK_EQ(worker_state->task_state, WorkerTaskState::kQueuedTask);
  ImageController::ProcessNextImageDecodeWithLock(worker_state);
  worker_state->task_state = WorkerTaskState::kNoTask;
}

void ImageController::ProcessNextImageDecodeWithLock(
    WorkerState* worker_state) {
  worker_state->lock.AssertAcquired();

  // If we don't have any work, abort.
  if (worker_state->image_decode_queue.empty()) {
    return;
  }

  // Take the next request from the queue.
  auto decode_it = worker_state->image_decode_queue.begin();
  CHECK(decode_it != worker_state->image_decode_queue.end());
  // Skip tasks that have an unmet external dependency.
  while (decode_it != worker_state->image_decode_queue.end() &&
         decode_it->second.has_external_dependency) {
    decode_it++;
  }
  if (decode_it == worker_state->image_decode_queue.end()) {
    return;
  }

  // Notify that the task will need completion. Note that there are two cases
  // where we process this. First, we might complete this task as a response to
  // the posted task below. Second, we might complete it in StopWorkerTasks().
  // In either case, the task would have already run (either post task happens
  // after running, or the thread was already joined which means the task ran).
  // This means that we can put the decode into |requests_needing_completion_|
  // here before actually running the task.
  scoped_refptr<TileTask> decode_task = decode_it->second.task;
  ImageDecodeRequestId decode_id = decode_it->second.id;
  worker_state->requests_needing_completion[decode_id] =
      std::move(decode_it->second);

  worker_state->image_decode_queue.erase(decode_it);

  // Run the task if we need to run it. If the task state isn't new, then there
  // is another task that is responsible for finishing it and cleaning up (and
  // it already ran); we just need to post a completion callback. Note that the
  // other tasks's completion will also run first, since the requests are
  // ordered. So, when we process this task's completion, we won't actually do
  // anything with the task and simply issue the callback.
  if (decode_task && decode_task->state().IsNew()) {
    decode_task->state().DidSchedule();
    decode_task->state().DidStart();
    {
      base::AutoReset<WorkerTaskState> reset_state(
          &worker_state->task_state, WorkerTaskState::kRunningTask);
      base::AutoUnlock release(worker_state->lock);
      decode_task->RunOnWorkerThread();
    }
    decode_task->state().DidFinish();
  }

  worker_state->origin_task_runner->PostTask(
      FROM_HERE, base::BindOnce(&ImageController::ImageDecodeCompleted,
                                worker_state->weak_ptr, decode_id));
}

void ImageController::ImageDecodeCompleted(ImageDecodeRequestId id) {
  ImageDecodedCallback callback;
  ImageDecodeResult result;
  scoped_refptr<TileTask> external_dependent;
  {
    base::AutoLock hold(worker_state_->lock);

    auto request_it = worker_state_->requests_needing_completion.find(id);
    // The request may have been completed by StopWorkerTasks().
    if (request_it == worker_state_->requests_needing_completion.end())
      return;
    id = request_it->first;
    ImageDecodeRequest& request = request_it->second;
    result = CompleteTaskForRequest(request);
    if (request.task) {
      external_dependent = std::move(request.task->external_dependent());
    }

    // Finally, save the callback so we can run it without the lock, and erase
    // the request from |requests_needing_completion_|.
    callback = std::move(request.callback);
    worker_state_->requests_needing_completion.erase(request_it);

    ScheduleImageDecodeOnWorkerIfNeeded();
  }

  if (external_dependent) {
    external_dependent->ExternalDependencyCompleted();
    notify_external_dependent_.Run(std::move(external_dependent));
  }

  // Finally run the requested callback.
  std::move(callback).Run(id, result);
}

ImageController::ImageDecodeResult ImageController::CompleteTaskForRequest(
    ImageDecodeRequest& request) {
  worker_state_->lock.AssertAcquired();
  // First, Determine the status of the decode. This has to happen here, since
  // we conditionally move from the draw image below.
  // Also note that if we don't need an unref for a lazy decoded images, it
  // implies that we never attempted the decode. Some of the reasons for this
  // would be that the image is of an empty size, or if the image doesn't fit
  // into memory. In all cases, this implies that the decode was a failure.
  ImageDecodeResult result;
  if (!request.draw_image.paint_image().IsLazyGenerated()) {
    result = ImageDecodeResult::DECODE_NOT_REQUIRED;
  } else if (!request.need_unref) {
    result = ImageDecodeResult::FAILURE;
  } else {
    result = ImageDecodeResult::SUCCESS;
  }

  // If we need to unref this decode, then we have to put it into the locked
  // images vector.
  if (request.need_unref) {
    requested_locked_images_[request.id] = std::move(request.draw_image);
  }

  // If we have a task that isn't completed yet, we need to complete it.
  if (request.task) {
    if (!request.task->HasCompleted()) {
      request.task->OnTaskCompleted();
      request.task->DidComplete();
    }
  }
  return result;
}

void ImageController::GenerateTasksForOrphanedRequests() {
  base::AutoLock hold(worker_state_->lock);
  DCHECK_EQ(0u, worker_state_->image_decode_queue.size());
  DCHECK_EQ(0u, worker_state_->requests_needing_completion.size());
  DCHECK(cache_);

  for (auto& request : orphaned_decode_requests_) {
    DCHECK(!request.task);
    DCHECK(!request.need_unref);
    if (request.draw_image.paint_image().IsLazyGenerated()) {
      // Get the task for this decode.
      ImageDecodeCache::TaskResult result =
          cache_->GetOutOfRasterDecodeTaskForImageAndRef(image_cache_client_id_,
                                                         request.draw_image);
      request.need_unref = result.need_unref;
      request.task = result.task;
      request.has_external_dependency =
          result.task && !result.task->dependencies().empty();
    }
    worker_state_->image_decode_queue[request.id] = std::move(request);
  }

  orphaned_decode_requests_.clear();
  ScheduleImageDecodeOnWorkerIfNeeded();
}

void ImageController::ScheduleImageDecodeOnWorkerIfNeeded() {
  if (worker_state_->task_state == WorkerTaskState::kNoTask &&
      HasReadyToRunTask()) {
    worker_state_->task_state = WorkerTaskState::kQueuedTask;
    worker_task_runner_->PostTask(FROM_HERE, worker_task_);
  }
}

ImageController::ImageDecodeRequest::ImageDecodeRequest() = default;
ImageController::ImageDecodeRequest::ImageDecodeRequest(
    ImageDecodeRequestId id,
    const DrawImage& draw_image,
    ImageDecodedCallback callback,
    scoped_refptr<TileTask> task,
    bool need_unref,
    bool has_external_dependency)
    : id(id),
      draw_image(draw_image),
      callback(std::move(callback)),
      task(std::move(task)),
      need_unref(need_unref),
      has_external_dependency(has_external_dependency) {}
ImageController::ImageDecodeRequest::ImageDecodeRequest(
    ImageDecodeRequest&& other) = default;
ImageController::ImageDecodeRequest::~ImageDecodeRequest() = default;

ImageController::ImageDecodeRequest& ImageController::ImageDecodeRequest::
operator=(ImageDecodeRequest&& other) = default;

}  // namespace cc
