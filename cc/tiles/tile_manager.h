// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TILES_TILE_MANAGER_H_
#define CC_TILES_TILE_MANAGER_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/cancelable_callback.h"
#include "base/memory/raw_ptr.h"
#include "base/rand_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/memory_dump_provider.h"
#include "cc/base/unique_notifier.h"
#include "cc/cc_export.h"
#include "cc/paint/target_color_params.h"
#include "cc/resources/memory_history.h"
#include "cc/resources/resource_pool.h"
#include "cc/tiles/checker_image_tracker.h"
#include "cc/tiles/decoded_image_tracker.h"
#include "cc/tiles/image_controller.h"
#include "cc/tiles/raster_tile_priority_queue.h"
#include "cc/tiles/tile.h"
#include "cc/tiles/tile_draw_info.h"
#include "cc/tiles/tile_manager_settings.h"
#include "gpu/command_buffer/client/client_shared_image.h"
#include "url/gurl.h"

namespace base {
namespace trace_event {
class ConvertableToTraceFormat;
class TracedValue;
}
}

namespace cc {
class EvictionTilePriorityQueue;
class ImageDecodeCache;
class RasterBufferProvider;
class RasterQueryQueue;
class TaskGraphRunner;
class TileManagerClient;
class TileTaskManager;

struct RasterTaskCompletionStats {
  RasterTaskCompletionStats();

  size_t completed_count;
  size_t canceled_count;
};
std::unique_ptr<base::trace_event::ConvertableToTraceFormat>
RasterTaskCompletionStatsAsValue(const RasterTaskCompletionStats& stats);

// This class manages tiles, deciding which should get rasterized and which
// should no longer have any memory assigned to them. Tile objects are "owned"
// by layers; they automatically register with the manager when they are
// created, and unregister from the manager when they are deleted.
//
// The TileManager coordinates scheduling of prioritized raster and decode work
// across 2 different subsystems, namely the TaskGraphRunner used primarily for
// raster work and images which must be decoded before rasterization of a tile
// can proceed, and the CheckerImageTracker used for images decoded
// asynchronously from raster using the |image_worker_task_runner|. The order in
// which work is scheduled across these systems is as follows:
//
// 1) RequiredForActivation/Draw Tiles: These are the highest priority tiles
// which block scheduling of any decode work for checkered-images.
//
// 2) Pre-paint Tiles: These are offscreen tiles which fall within the
// pre-raster distance. The work for these tiles continues in parallel with the
// decode work for checkered images from visible/pre-paint tiles.
//
// 3) Pre-decode Tiles: These are offscreen tiles which are outside the
// pre-raster distance but have their images pre-decoded and locked. Finishing
// work for these tiles on the TaskGraph blocks starting decode work for
// checker-imaged pre-decode tiles.

class CC_EXPORT TileManager : CheckerImageTrackerClient,
                              public base::trace_event::MemoryDumpProvider {
 public:
  TileManager(TileManagerClient* client,
              base::SequencedTaskRunner* origin_task_runner,
              scoped_refptr<base::SequencedTaskRunner> image_worker_task_runner,
              size_t scheduled_raster_task_limit,
              bool running_on_renderer_process,
              const TileManagerSettings& tile_manager_settings);

  TileManager(const TileManager&) = delete;
  ~TileManager() override;

  TileManager& operator=(const TileManager&) = delete;

  // Assigns tile memory and schedules work to prepare tiles for drawing.
  // This step occurs after Commit and at most once per BeginFrame. It can be
  // called on its own, that is, outside of Commit.
  //
  // - Runs client_->NotifyReadyToActivate() when all tiles required for
  // activation are prepared, or failed to prepare due to OOM.
  // - Runs client_->NotifyReadyToDraw() when all tiles required draw are
  // prepared, or failed to prepare due to OOM.
  bool PrepareTiles(const GlobalStateThatImpactsTilePriority& state);

  // Synchronously finish any in progress work, cancel the rest, and clean up as
  // much resources as possible. Also, prevents any future work until a
  // SetResources call.
  void FinishTasksAndCleanUp();

  // Set the new given resource pool and tile task runner. Note that
  // FinishTasksAndCleanUp must be called in between consecutive calls to
  // SetResources.
  void SetResources(ResourcePool* resource_pool,
                    ImageDecodeCache* image_decode_cache,
                    TaskGraphRunner* task_graph_runner,
                    RasterBufferProvider* raster_buffer_provider,
                    bool use_gpu_rasterization,
                    RasterQueryQueue* pending_raster_queries);

  // This causes any completed raster work to finalize, so that tiles get up to
  // date draw information.
  void PrepareToDraw();

  // Called when the required-for-activation/required-for-draw state of tiles
  // may have changed.
  void DidModifyTilePriorities();

  std::unique_ptr<Tile> CreateTile(const Tile::CreateInfo& info,
                                   int layer_id,
                                   int source_frame_number,
                                   int flags);

  bool IsReadyToActivate() const;
  bool IsReadyToDraw() const;

  const PaintImageIdFlatSet& TakeImagesToInvalidateOnSyncTree();
  void DidActivateSyncTree();
  void ClearCheckerImageTracking(bool can_clear_decode_policy_tracking);
  void SetCheckerImagingForceDisabled(bool force_disable);

  std::unique_ptr<base::trace_event::ConvertableToTraceFormat>
  BasicStateAsValue() const;
  void BasicStateAsValueInto(base::trace_event::TracedValue* dict) const;
  const MemoryHistory::Entry& memory_stats_from_last_assign() const {
    return memory_stats_from_last_assign_;
  }

  // Public methods for testing.
  void InitializeTilesWithResourcesForTesting(const std::vector<Tile*>& tiles);
  void ReleaseTileResourcesForTesting(const std::vector<Tile*>& tiles);

  void SetGlobalStateForTesting(
      const GlobalStateThatImpactsTilePriority& state) {
    global_state_ = state;
  }

  void SetTileTaskManagerForTesting(
      std::unique_ptr<TileTaskManager> tile_task_manager);

  void SetRasterBufferProviderForTesting(
      RasterBufferProvider* raster_buffer_provider) {
    raster_buffer_provider_ = raster_buffer_provider;
  }

  void SetPendingRasterQueriesForTesting(
      RasterQueryQueue* pending_raster_queries) {
    pending_raster_queries_ = pending_raster_queries;
  }

  std::vector<Tile*> AllTilesForTesting() const;

  void SetScheduledRasterTaskLimitForTesting(size_t limit) {
    scheduled_raster_task_limit_ = limit;
  }

  void CheckIfMoreTilesNeedToBePreparedForTesting() {
    CheckIfMoreTilesNeedToBePrepared();
  }

  void SetMoreTilesNeedToBeRasterizedForTesting() {
    all_tiles_that_need_to_be_rasterized_are_scheduled_ = false;
  }

  void ResetSignalsForTesting();

  bool HasScheduledTileTasksForTesting() const {
    return has_scheduled_tile_tasks_;
  }

  void FlushImageControllerTasksForTesting();

  void DisbleMetricsSubsamplingForTesting() { metrics_sampling_rate_ = 1.; }

  void OnRasterTaskCompleted(Tile::Id tile_id,
                             ResourcePool::InUsePoolResource resource,
                             bool was_canceled);

  // CheckerImageTrackerClient implementation.
  void NeedsInvalidationForCheckerImagedTiles() override;

  // This method can only be used for debugging information, since it performs a
  // non trivial amount of work.
  std::unique_ptr<base::trace_event::ConvertableToTraceFormat>
  ActivationStateAsValue();

  void ActivationStateAsValueInto(base::trace_event::TracedValue* state) const;
  int num_of_tiles_with_checker_images() const {
    return num_of_tiles_with_checker_images_;
  }

  CheckerImageTracker& checker_image_tracker() {
    return checker_image_tracker_;
  }
  DecodedImageTracker& decoded_image_tracker() {
    return decoded_image_tracker_;
  }

  const std::vector<DrawImage>& decode_tasks_for_testing(Tile::Id id) {
    return scheduled_draw_images_[id];
  }

  void set_active_url(const GURL& url) { active_url_ = url; }

  void SetOverridesForTesting(
      scoped_refptr<base::TaskRunner> task_runner_for_testing,
      const base::TickClock* clock);

  bool OnMemoryDump(const base::trace_event::MemoryDumpArgs& args,
                    base::trace_event::ProcessMemoryDump* pmd) override;

 protected:
  friend class Tile;
  // Must be called by tile during destruction.
  void Release(Tile* tile);
  Tile::Id GetUniqueTileId() { return ++next_tile_id_; }

 private:
  class MemoryUsage {
   public:
    MemoryUsage();
    MemoryUsage(size_t memory_bytes, size_t resource_count);

    static MemoryUsage FromConfig(const gfx::Size& size,
                                  viz::SharedImageFormat format);
    static MemoryUsage FromTile(const Tile* tile);

    MemoryUsage& operator+=(const MemoryUsage& other);
    MemoryUsage& operator-=(const MemoryUsage& other);
    MemoryUsage operator-(const MemoryUsage& other);

    bool Exceeds(const MemoryUsage& limit) const;
    int64_t memory_bytes() const { return memory_bytes_; }

   private:
    int64_t memory_bytes_;
    int resource_count_;
  };

  struct Signals {
    bool activate_tile_tasks_completed = false;
    bool draw_tile_tasks_completed = false;
    bool all_tile_tasks_completed = false;

    bool activate_gpu_work_completed = false;
    bool draw_gpu_work_completed = false;

    bool did_notify_ready_to_activate = false;
    bool did_notify_ready_to_draw = false;
    bool did_notify_all_tile_tasks_completed = false;
  };

  struct PrioritizedWorkToSchedule {
    PrioritizedWorkToSchedule();
    PrioritizedWorkToSchedule(PrioritizedWorkToSchedule&& other);
    ~PrioritizedWorkToSchedule();

    std::vector<PrioritizedTile> tiles_to_raster;
    std::vector<PrioritizedTile> tiles_to_process_for_images;
    // A vector of additional images to be decoded for prepaint, but that
    // are not necessarily associated with any tile.
    std::vector<DrawImage> extra_prepaint_images;
    CheckerImageTracker::ImageDecodeQueue checker_image_decode_queue;
  };

  // Frees the resources of all occluded tiles.
  void FreeResourcesForOccludedTiles();

  // If we haven't produced a frame in a while, drop the "nice to have" tiles.
  void ScheduleReduceTileMemoryWhenIdle(base::TimeDelta time_since_last_active);
  void ScheduleTrimPrepaintTiles();
  void ReduceTileMemoryWhenIdle();
  void TrimPrepaintTiles();

  // True if tile resources are present and freed.
  void FreeResourcesForTile(Tile* tile);
  void FreeResourcesForTileAndNotifyClientIfTileWasReadyToDraw(Tile* tile);
  scoped_refptr<TileTask> CreateRasterTask(
      const PrioritizedTile& prioritized_tile,
      const TargetColorParams& target_color_params,
      PrioritizedWorkToSchedule* work_to_schedule);

  std::unique_ptr<EvictionTilePriorityQueue>
  FreeTileResourcesUntilUsageIsWithinLimit(
      std::unique_ptr<EvictionTilePriorityQueue> eviction_priority_queue,
      const MemoryUsage& limit,
      MemoryUsage* usage);
  std::unique_ptr<EvictionTilePriorityQueue>
  FreeTileResourcesWithLowerPriorityUntilUsageIsWithinLimit(
      std::unique_ptr<EvictionTilePriorityQueue> eviction_priority_queue,
      const MemoryUsage& limit,
      const TilePriority& oother_priority,
      MemoryUsage* usage);
  bool TilePriorityViolatesMemoryPolicy(const TilePriority& priority);
  bool AreRequiredTilesReadyToDraw(RasterTilePriorityQueue::Type type) const;
  void CheckIfMoreTilesNeedToBePrepared();
  void MarkTilesOutOfMemory(
      std::unique_ptr<RasterTilePriorityQueue> queue) const;

  void DidFinishRunningTileTasksRequiredForActivation();
  void DidFinishRunningTileTasksRequiredForDraw();
  void DidFinishRunningAllTileTasks(base::TimeTicks start_time,
                                    bool has_pending_queries);
  void ExternalDependencyCompletedForRasterTask(
      scoped_refptr<TileTask> dependent);
  void ExternalDependencyCompletedForNonRasterTask(
      scoped_refptr<TileTask> dependent);

  scoped_refptr<TileTask> CreateTaskSetFinishedTask(
      void (TileManager::*callback)());
  PrioritizedWorkToSchedule AssignGpuMemoryToTiles();
  void ScheduleTasks(PrioritizedWorkToSchedule work_to_schedule);

  void PartitionImagesForCheckering(
      const PrioritizedTile& prioritized_tile,
      const TargetColorParams& target_color_params,
      std::vector<DrawImage>* sync_decoded_images,
      std::vector<PaintImage>* checkered_images,
      const gfx::Rect* invalidated_rect,
      base::flat_map<PaintImage::Id, size_t>* image_to_frame_index = nullptr);
  void AddCheckeredImagesToDecodeQueue(
      const PrioritizedTile& prioritized_tile,
      const TargetColorParams& target_color_params,
      CheckerImageTracker::DecodeType decode_type,
      CheckerImageTracker::ImageDecodeQueue* image_decode_queue);

  std::unique_ptr<base::trace_event::ConvertableToTraceFormat>
  ScheduledTasksStateAsValue() const;

  bool UsePartialRaster(int msaa_sample_count) const;

  void CheckForCompletedTasksAndIssueSignals();
  void CheckPendingGpuWorkAndIssueSignals();
  void IssueSignals();
  void ScheduleCheckRasterFinishedQueries();
  void CheckRasterFinishedQueries();

  bool ShouldRasterOccludedTiles() const;
  base::TimeTicks NowWithOverride() const;
  base::TaskRunner* TaskRunnerWithOverride() const;

 public:
  static base::TimeDelta GetTrimPrepaintTilesDelay();
  static constexpr base::TimeDelta kDelayBeforeTimeReclaim = base::Minutes(5);

 private:
  void InsertNodesForRasterTask(TileTask* raster_task,
                                uint16_t priority,
                                bool use_foreground_category);
  void InsertNodeForDecodeTask(TileTask* task,
                               uint16_t priority,
                               bool use_foreground_category);

  raw_ptr<TileManagerClient, DanglingUntriaged> client_;
  raw_ptr<base::SequencedTaskRunner> task_runner_;
  raw_ptr<ResourcePool, DanglingUntriaged> resource_pool_;
  std::unique_ptr<TileTaskManager> tile_task_manager_;
  raw_ptr<RasterBufferProvider, DanglingUntriaged> raster_buffer_provider_;
  GlobalStateThatImpactsTilePriority global_state_;
  size_t scheduled_raster_task_limit_;
  const bool running_on_renderer_process_;

  const TileManagerSettings tile_manager_settings_;
  bool use_gpu_rasterization_;
  raw_ptr<RasterQueryQueue> pending_raster_queries_ = nullptr;

  std::unordered_map<Tile::Id, raw_ptr<Tile, CtnExperimental>> tiles_;

  bool all_tiles_that_need_to_be_rasterized_are_scheduled_;
  MemoryHistory::Entry memory_stats_from_last_assign_;

  bool did_check_for_completed_tasks_since_last_schedule_tasks_;
  bool did_oom_on_last_assign_;

  ImageController image_controller_;
  DecodedImageTracker decoded_image_tracker_;
  CheckerImageTracker checker_image_tracker_;

  RasterTaskCompletionStats raster_task_completion_stats_;

  TaskGraph graph_;

  UniqueNotifier more_tiles_need_prepare_check_notifier_;

  Signals signals_;

  UniqueNotifier signals_check_notifier_;

  bool has_scheduled_tile_tasks_;

  uint64_t prepare_tiles_count_;
  uint64_t next_tile_id_;

  std::unordered_set<raw_ptr<Tile, CtnExperimental>> pending_gpu_work_tiles_;
  uint64_t pending_required_for_activation_callback_id_ = 0;
  uint64_t pending_required_for_draw_callback_id_ = 0;
  // If true, we should re-compute tile requirements in
  // CheckPendingGpuWorkAndIssueSignals.
  bool pending_tile_requirements_dirty_ = false;

  std::unordered_map<Tile::Id, std::vector<DrawImage>> scheduled_draw_images_;
  std::vector<scoped_refptr<TileTask>> locked_image_tasks_;

  // Number of tiles with a checker-imaged resource or active raster tasks which
  // will create a checker-imaged resource.
  int num_of_tiles_with_checker_images_ = 0;

  GURL active_url_;

  base::TimeTicks last_active_time_;
  bool has_pending_idle_task_ = false;
  bool has_pending_tile_trimming_task_ = false;
  scoped_refptr<base::TaskRunner> task_runner_for_testing_ = nullptr;
  raw_ptr<const base::TickClock> tick_clock_for_testing_ = nullptr;

  base::MetricsSubSampler metrics_sub_sampler_;
  float metrics_sampling_rate_ = .01;

  // The callback scheduled to poll whether the GPU side work for pending tiles
  // has completed.
  bool has_pending_queries_ = false;
  base::CancelableOnceClosure check_pending_tile_queries_callback_;

  // We need two WeakPtrFactory objects as the invalidation pattern of each is
  // different. The |task_set_finished_weak_ptr_factory_| is invalidated any
  // time new tasks are scheduled, preventing a race when the callback has
  // been scheduled but not yet executed.
  base::WeakPtrFactory<TileManager> task_set_finished_weak_ptr_factory_{this};
  // The |ready_to_draw_callback_weak_ptr_factory_| is never invalidated.
  base::WeakPtrFactory<TileManager> ready_to_draw_callback_weak_ptr_factory_{
      this};
};

}  // namespace cc

#endif  // CC_TILES_TILE_MANAGER_H_
