// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40285824): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include "chrome/browser/ash/app_list/search/local_image_search/image_annotation_worker.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "ash/public/cpp/image_util.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_path_watcher.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/ash/app_list/search/local_image_search/annotation_storage.h"
#include "chrome/browser/ash/app_list/search/local_image_search/search_utils.h"
#include "chrome/browser/ash/app_list/search/search_features.h"
#include "chrome/browser/profiles/profile.h"
#include "chromeos/ash/components/string_matching/tokenized_string.h"
#include "chromeos/services/machine_learning/public/mojom/image_content_annotation.mojom.h"
#include "services/screen_ai/public/mojom/screen_ai_service.mojom.h"
#include "ui/gfx/image/image_skia_operations.h"

namespace app_list {
namespace {

using TokenizedString = ::ash::string_matching::TokenizedString;
using Mode = ::ash::string_matching::TokenizedString::Mode;

constexpr int kMaxFileSizeBytes = 2e+7;  // ~ 20MiB
constexpr float kConfidenceThreshold = 0.3f;
constexpr int kOcrMinWordLength = 3;
constexpr int kRetryDelay = 2;              // For exponential delays.
constexpr int kMaxNumRetries = 12;          // Over 2 hrs.
constexpr int kDefaultIndexingLimit = 500;  // 500 images per user session.
constexpr base::TimeDelta kInitialIndexingDelay = base::Seconds(1);
constexpr int kMaxOcrImageSize = 1000;  // The max allowed width&height in DIP.
constexpr int kMaxDisconnection =
    3;  // The max allowed disconnection for both ICA and OCR.
constexpr base::TimeDelta kIcaReconnectDelay = base::Seconds(10);

// These values persist to logs. Entries should not be renumbered and numeric
// values should never be reused.
enum class Status {
  kOk = 0,
  kFailedToInitializeIca = 1,
  kFailedToInitializeOcr = 2,
  kFailedToDecodeImage = 3,
  kImageProcessingTimeOut = 4,  // Deprecated
  kFailedToProcessIca = 5,
  kFailedToProcessOcr = 6,
  kIcaDisconnectionLimit = 7,
  kOcrDisconnectionLimit = 8,
  kMaxValue = kOcrDisconnectionLimit,
};

void LogStatusUma(Status status) {
  base::UmaHistogramEnumeration(
      "Apps.AppList.AnnotationStorage.ImageAnnotationWorker.Status", status);
}

// These values persist to logs. Entries should not be renumbered and numeric
// values should never be reused.
enum class IndexingStatus {
  kStart = 0,
  kOcrStart = 1,
  kOcrSucceed = 2,
  kIcaStart = 3,
  kIcaSucceed = 4,
  kMaxValue = kIcaSucceed,
};

void LogIndexingUma(IndexingStatus status) {
  base::UmaHistogramEnumeration(
      "Apps.AppList.AnnotationStorage.ImageAnnotationWorker.IndexingStatus",
      status);
}

// Exclude animated WebPs.
bool IsStaticWebp(const base::FilePath& path) {
  std::ifstream file(path.value(), std::ios::binary);
  if (!file) {
    LOG(ERROR) << "Unable to open file: " << path;
    return false;
  }

  char buffer[30];
  file.read(buffer, sizeof(buffer));
  file.close();

  // Checking for RIFF header and WebP identifier as in the
  // https://developers.google.com/speed/webp/docs/riff_container
  if (std::string(buffer, 4) == "RIFF" &&
      std::string(buffer + 8, 4) == "WEBP") {
    // Checking the VP8X chunk for animation
    if (std::string(buffer + 12, 4) == "VP8X") {
      // VP8X header is 8 bytes then the flags byte.
      const char flags = buffer[20];
      // The second bit indicates if it's animated.
      return !static_cast<bool>(flags & 0x02);
    }

    return true;
  }

  return false;
}

bool IsJpeg(const base::FilePath& path) {
  std::ifstream file(path.value(), std::ios::binary);
  if (!file) {
    LOG(ERROR) << "Unable to open file: " << path;
    return false;
  }

  char buffer[4];
  file.read(buffer, sizeof(buffer));
  file.close();

  // Check for JPEG magic numbers
  return (buffer[0] == (char)0xFF && buffer[1] == (char)0xD8 &&
          buffer[2] == (char)0xFF &&
          (buffer[3] == (char)0xE0 || buffer[3] == (char)0xE1));
}

bool IsPng(const base::FilePath& path) {
  std::ifstream file(path.value(), std::ios::binary);
  if (!file) {
    LOG(ERROR) << "Unable to open file: " << path;
    return false;
  }

  uint8_t buffer[8];
  file.read(reinterpret_cast<char*>(buffer), sizeof(buffer));
  file.close();

  const uint8_t pngSignature[8] = {0x89, 0x50, 0x4E, 0x47,
                                   0x0D, 0x0A, 0x1A, 0x0A};
  for (int i = 0; i < 8; ++i) {
    if (buffer[i] != pngSignature[i]) {
      return false;
    }
  }

  return true;
}

// Checks for supported extensions.
bool IsImage(const base::FilePath& path) {
  DVLOG(1) << "IsImage? " << path.Extension();
  const std::string extension = base::ToLowerASCII(path.Extension());
  // Note: The UI design stipulates jpg, png, gif, and svg, but we use
  // the subset that ICA can handle
  return extension == ".jpeg" || extension == ".jpg" || extension == ".png" ||
         extension == ".webp";
}

// Check headers for correctness.
bool IsSupportedImage(const base::FilePath& path) {
  DVLOG(1) << "IsSupportedImage? " << path.Extension();
  const std::string extension = base::ToLowerASCII(path.Extension());
  if (extension == ".jpeg" || extension == ".jpg") {
    return IsJpeg(path);
  } else if (extension == ".png") {
    return IsPng(path);
  } else if (extension == ".webp") {
    return IsStaticWebp(path);
  } else {
    return false;
  }
}

bool IsPathExcluded(const base::FilePath& path,
                    const std::vector<base::FilePath>& excluded_paths) {
  DVLOG(1) << "IsPathExcluded: " << path;
  return std::any_of(excluded_paths.begin(), excluded_paths.end(),
                     [&path](const base::FilePath& prefix) {
                       return base::StartsWith(path.value(), prefix.value(),
                                               base::CompareCase::SENSITIVE);
                     });
}

// The same as the static function of `ScreenAIServiceRouter` as we are not
// allowed to include it directly. It's not expected to change this function and
// to sync it to `ScreenAIServiceRouter`. Instead, we would like this function
// to sync when there is an update in `ScreenAIServiceRouter` so that we can
// re-try OCR with the right delay.
// LINT.IfChange(SuggestedWaitTimeBeforeReAttempt)
base::TimeDelta SuggestedWaitTimeBeforeReAttempt(uint32_t reattempt_number) {
  return base::Minutes(reattempt_number * reattempt_number);
}
// LINT.ThenChange(//chrome/browser/screen_ai/screen_ai_service_router.cc:SuggestedWaitTimeBeforeReAttempt)

}  // namespace

ImageAnnotationWorker::ImageAnnotationWorker(
    const base::FilePath& root_path,
    const std::vector<base::FilePath>& excluded_paths,
    Profile* profile,
    bool use_file_watchers,
    bool use_ocr,
    bool use_ica)
    : root_path_(root_path),
      excluded_paths_(excluded_paths),
      use_file_watchers_(use_file_watchers),
      use_ica_(use_ica),
      use_ocr_(use_ocr),
      indexing_limit_(base::GetFieldTrialParamByFeatureAsInt(
          search_features::kLauncherImageSearchIndexingLimit,
          "indexing_limit",
          kDefaultIndexingLimit)),
      task_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN})) {
  DETACH_FROM_SEQUENCE(sequence_checker_);

  if (use_ocr_) {
    CHECK(profile);
    // `OpticalCharacterRecognizer` should be created on the UI thread.
    optical_character_recognizer_ =
        screen_ai::OpticalCharacterRecognizer::Create(
            profile, screen_ai::mojom::OcrClientType::kLocalSearch);
  }
}

ImageAnnotationWorker::~ImageAnnotationWorker() = default;

void ImageAnnotationWorker::Initialize(AnnotationStorage* annotation_storage) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(annotation_storage);
  annotation_storage_ = annotation_storage;
  // This function is called from `AnnotationStorage`. Thus, we will the task
  // runner `AnnotationStorage` currently runs on.
  main_task_runner_ = base::SequencedTaskRunner::GetCurrentDefault();

  on_file_change_callback_ = base::BindRepeating(
      &ImageAnnotationWorker::OnFileChange, weak_ptr_factory_.GetWeakPtr());

  if (use_ocr_) {
    optical_character_recognizer_->SetDisconnectedCallback(
        base::BindRepeating(&ImageAnnotationWorker::OnOcrDisconnected,
                            ocr_weak_ptr_factory_.GetWeakPtr()));
  }

  if (use_ica_) {
    DVLOG(1) << "Initializing ICA DLC.";
    image_content_annotator_ = std::make_unique<ImageContentAnnotator>(
        base::BindRepeating(&ImageAnnotationWorker::OnIcaDisconnected,
                            weak_ptr_factory_.GetWeakPtr()));
    image_content_annotator_->EnsureAnnotatorIsConnected();
  }

  main_task_runner_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&ImageAnnotationWorker::OnDlcInstalled,
                     weak_ptr_factory_.GetWeakPtr()),
      kInitialIndexingDelay);
}

void ImageAnnotationWorker::OnDlcInstalled() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  bool is_ica_dlc_installed =
      image_content_annotator_ && image_content_annotator_->IsDlcInitialized();
  bool is_ocr_dlc_installed = optical_character_recognizer_ &&
                              optical_character_recognizer_->is_ready();

  if ((use_ocr_ && !is_ocr_dlc_installed) ||
      (use_ica_ && !is_ica_dlc_installed)) {
    DVLOG(1) << "DLCs are not ready. OCR: " << is_ocr_dlc_installed << "/"
             << use_ocr_ << " ICA: " << is_ica_dlc_installed << "/" << use_ica_
             << ". Waiting.";

    if (num_retries_passed_ > kMaxNumRetries) {
      if (use_ica_ && !is_ica_dlc_installed) {
        LOG(ERROR) << "Failed to initialize ICA.";
        LogStatusUma(Status::kFailedToInitializeIca);
      }
      if (use_ocr_ && !is_ocr_dlc_installed) {
        LOG(ERROR) << "Failed to initialize OCR.";
        LogStatusUma(Status::kFailedToInitializeOcr);
      }
      return;
    }

    // The installation status of `image_content_annotator_` only updates when
    // function `EnsureAnnotatorIsConnected()` is called. Thus, at each retry we
    // should trigger it again so that it attempts to bind the annotator.
    if (use_ica_) {
      image_content_annotator_->EnsureAnnotatorIsConnected();
    }

    // It is expected to be ready on a first try. Also, it is not a time
    // sensitive task, so we do not need to implement a full-fledged observer.
    main_task_runner_->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ImageAnnotationWorker::OnDlcInstalled,
                       weak_ptr_factory_.GetWeakPtr()),
        base::Seconds(std::pow(kRetryDelay, num_retries_passed_)));
    num_retries_passed_ += 1;
    image_content_annotator_->set_num_retries_passed(num_retries_passed_);
    return;
  }

  if (use_file_watchers_) {
    DVLOG(1) << "DLCs are ready. Watching for file changes: " << root_path_;
    file_watcher_ = std::make_unique<base::FilePathWatcher>();

    // `file_watcher_` needs to be deleted in the same sequence it was
    // initialized.
    file_watcher_->WatchWithOptions(
        root_path_,
        {.type = base::FilePathWatcher::Type::kRecursive,
         .report_modified_path = true},
        on_file_change_callback_);
  }

  if (search_features::IsLauncherImageSearchDebugEnabled()) {
    LOG(ERROR) << "DLC initialization succeed.";
  }

  LogStatusUma(Status::kOk);
  OnFileChange(root_path_, /*error=*/false);
  FindAndRemoveDeletedFiles(annotation_storage_->GetAllFiles());
}

void ImageAnnotationWorker::OnFileChange(const base::FilePath& path,
                                         bool error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOG(1) << "OnFileChange: " << path;
  if (error || IsPathExcluded(path, excluded_paths_)) {
    DVLOG(1) << "Skipping.";
    return;
  }

  DVLOG(1) << "Adding to a queue";
  files_to_process_.push(std::move(path));
  // To keep the process sequential, `OnFileChange` should only start the
  // processing if there is no active processing.
  if (!queue_processing_started_) {
    // Queue processing started. Sets the flag.
    queue_processing_started_ = true;
    queue_processing_start_time_ = base::TimeTicks::Now();
    ProcessItems();
  }
  return;
}

void ImageAnnotationWorker::ProcessItems() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::UmaHistogramCounts100000(
      "Apps.AppList.AnnotationStorage.ImageAnnotationWorker."
      "QueueNumberOfObjectsToProcess",
      files_to_process_.size());

  while (files_to_process_.size() > 0) {
    const base::FilePath path = files_to_process_.front();
    files_to_process_.pop();

    DVLOG(1) << "ProcessNextItem " << path;
    if (base::PathExists(path)) {
      if (base::DirectoryExists(path)) {  // It's a directory.
        ProcessNextDirectory(path);
      } else if (IsImage(path)) {
        bool image_is_decoded = ProcessNextImage(path);
        // If the image is decoded. stop the process as we need to keep the
        // image processing sequential. The process will be continued by the
        // callback (either `OnPerformOcr` or `OnPerformIca`).
        if (image_is_decoded) {
          return;
        }
      }
      // Don't do anything if it's a non-image file and continue on the next
      // item.
    } else {
      if (path.FinalExtension().empty()) {
        RemoveOldDirectory(path);
      } else {
        annotation_storage_->Remove(path);
      }
    }
  }

  DVLOG(1) << "The queue is empty.";
  base::UmaHistogramLongTimes100(
      "Apps.AppList.AnnotationStorage.ImageAnnotationWorker."
      "QueueProcessingTime",
      base::TimeTicks::Now() - queue_processing_start_time_);
  // As the queue processing is finished, disconnects the annotators and resets
  // the flag.
  DisconnectAnnotators();
  queue_processing_started_ = false;
}

void ImageAnnotationWorker::ProcessNextDirectory(
    const base::FilePath& directory_path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOG(1) << "ProcessNextDirectory " << directory_path;

  // We need to re-index all the files in the directory.
  auto file_enumerator = base::FileEnumerator(
      directory_path,
      /*recursive=*/false, base::FileEnumerator::NAMES_ONLY, "*",
      base::FileEnumerator::FolderSearchPolicy::ALL);

  for (base::FilePath file_path = file_enumerator.Next(); !file_path.empty();
       file_path = file_enumerator.Next()) {
    DVLOG(1) << "Found file: " << file_path;
    OnFileChange(std::move(file_path), /*error=*/false);
  }
  return;
}

void ImageAnnotationWorker::RemoveOldDirectory(
    const base::FilePath& directory_path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOG(1) << "RemoveOldDirectory " << directory_path;

  auto files = annotation_storage_->SearchByDirectory(directory_path);
  for (const auto& file : files) {
    OnFileChange(file, /*error=*/false);
  }
  return;
}

bool ImageAnnotationWorker::ProcessNextImage(const base::FilePath& image_path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (search_features::IsLauncherImageSearchDebugEnabled()) {
    LOG(ERROR) << "ProcessNextImage " << image_path;
  }

  auto file_info = std::make_unique<base::File::Info>();
  if (!base::GetFileInfo(image_path, file_info.get()) || file_info->size == 0 ||
      file_info->size > kMaxFileSizeBytes || !IsSupportedImage(image_path)) {
    annotation_storage_->Remove(image_path);
    return false;
  }
  DCHECK(file_info);

  // If all conditions meet:
  //  1. The image exists in the database and has not been modified since
  //  the last indexing.
  //  2. The ocr is not enabled or the ocr indexing in database is up-to-date.
  //  3. The ica is not enabled or the ica indexing in database is up-to-date.
  //  4. It's not in the test environment.
  // Then an indexing is not expected and skip the process. Otherwise, clear
  // this image from database and redo the indexing.
  const ImageStatus image_status =
      annotation_storage_->GetImageStatus(image_path);
  bool ocr_up_to_date = !use_ocr_ || image_status.ocr_version == kOcrVersion;
  bool ica_up_to_date = !use_ica_ || image_status.ica_version == kIcaVersion;
  if (file_info->last_modified ==
          image_status.last_modified.value_or(base::Time()) &&
      ocr_up_to_date && ica_up_to_date &&
      !image_processing_delay_for_test_.has_value()) {
    return false;
  }

  DVLOG(1) << "Processing new " << image_path << " "
           << file_info->last_modified;
  annotation_storage_->Remove(image_path);
  ImageInfo image_info(/*annotations=*/{}, image_path, file_info->last_modified,
                       file_info->size);

  // Early return if:
  // 1. Reaching the indexing limit.
  // 2. Reaching the maximum ica disconnection event.
  // 3. Reaching the maximum ocr disconnection event.
  // Continue the process as we still need to deal with deleted images.
  if (search_features::IsLauncherImageSearchIndexingLimitEnabled()) {
    if (num_indexing_images_ >= indexing_limit_) {
      // When reaching indexing limit, no more image processing is expected.
      // Thus, disconnects annotators.
      DisconnectAnnotators();
      return false;
    }
    num_indexing_images_ += 1;
  }
  if (num_ica_disconnection_ >= kMaxDisconnection ||
      num_ocr_disconnection_ >= kMaxDisconnection) {
    // When reaching disconnection limit, no more image processing is expected.
    // Thus, disconnects annotators.
    DisconnectAnnotators();
    return false;
  }

  if (use_ocr_ || use_ica_) {
    LogIndexingUma(IndexingStatus::kStart);
    ash::image_util::DecodeImageFile(
        base::BindOnce(&ImageAnnotationWorker::OnDecodeImageFile,
                       weak_ptr_factory_.GetWeakPtr(), image_info),
        image_info.path);
    return true;
  }

  // The fake logic should only work in tests.
  if (image_processing_delay_for_test_.has_value()) {
    // Call `OnDecodeImageFile` so that we can test the logic of timer.
    ImageAnnotationWorker::OnDecodeImageFile(std::move(image_info),
                                             gfx::ImageSkia());
    return true;
  }
  return false;
}

void ImageAnnotationWorker::OnDecodeImageFile(
    ImageInfo image_info,
    const gfx::ImageSkia& image_skia) {
  if (search_features::IsLauncherImageSearchDebugEnabled()) {
    LOG(ERROR) << "OnDecodeImageFile.";
  }
  // `image_skia` can be empty in tests.
  if (image_skia.size().IsEmpty() &&
      !image_processing_delay_for_test_.has_value()) {
    LOG(ERROR) << "Failed to decode image.";
    LogStatusUma(Status::kFailedToDecodeImage);
    ProcessItems();
    return;
  }
  image_info.width = image_skia.width();
  image_info.height = image_skia.height();

  if (search_features::IsLauncherImageSearchDebugEnabled()) {
    LOG(ERROR) << "Image decoding succeed.";
  }

  if (use_ocr_) {
    LogIndexingUma(IndexingStatus::kOcrStart);
    LogIcaUma(IcaStatus::kStartWithOcr);
    CHECK(!ocr_in_use_);

    // Downsamples Image if it is too big for OCR, and it helps reduce the
    // memory
    // usage and ocr processing time. We scale to the smaller ratio to ensure
    // that the resized width and height won't exceed the max value.
    gfx::ImageSkia resized_image;

    if (image_skia.height() > kMaxOcrImageSize ||
        image_skia.width() > kMaxOcrImageSize) {
      float scale = static_cast<float>(kMaxOcrImageSize) /
                    std::max(image_skia.width(), image_skia.height());
      gfx::Size target_size = gfx::ScaleToRoundedSize(image_skia.size(), scale);
      resized_image = gfx::ImageSkiaOperations::CreateResizedImage(
          image_skia, skia::ImageOperations::RESIZE_BEST, target_size);
    }

    ocr_in_use_ = true;
    optical_character_recognizer_->PerformOCR(
        resized_image.isNull() ? *image_skia.bitmap() : *resized_image.bitmap(),
        base::BindOnce(&ImageAnnotationWorker::OnPerformOcr,
                       weak_ptr_factory_.GetWeakPtr(), std::move(image_info)));
    return;
  }

  if (use_ica_) {
    LogIndexingUma(IndexingStatus::kIcaStart);
    LogIcaUma(IcaStatus::kStartWithoutOcr);
    CHECK(!ica_in_use_);

    ica_in_use_ = true;
    image_content_annotator_->AnnotateEncodedImage(
        image_info.path,
        base::BindOnce(&ImageAnnotationWorker::OnPerformIca,
                       weak_ptr_factory_.GetWeakPtr(), std::move(image_info)));
    return;
  }

  // The fake logic should only work in tests.
  if (image_processing_delay_for_test_.has_value()) {
    // Post a delayed task to simulate the image processing delay.
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ImageAnnotationWorker::RunFakeImageAnnotator,
                       weak_ptr_factory_.GetWeakPtr(), std::move(image_info)),
        image_processing_delay_for_test_.value());
    return;
  }
}

void ImageAnnotationWorker::OnPerformOcr(
    ImageInfo image_info,
    screen_ai::mojom::VisualAnnotationPtr visual_annotation) {
  LogIndexingUma(IndexingStatus::kOcrSucceed);
  LogIcaUma(IcaStatus::kOcrSucceed);
  ocr_in_use_ = false;
  if (search_features::IsLauncherImageSearchDebugEnabled()) {
    LOG(ERROR) << "OnPerformOcr with line size: "
               << visual_annotation->lines.size();
  }
  for (const auto& text_line : visual_annotation->lines) {
    TokenizedString tokens(base::UTF8ToUTF16(text_line->text_line),
                           Mode::kWords);
    for (const auto& word : tokens.tokens()) {
      std::string lower_case_word = base::UTF16ToUTF8(word);
      if (word.size() >= kOcrMinWordLength && !IsStopWord(lower_case_word) &&
          base::IsAsciiAlpha(lower_case_word[0])) {
        image_info.annotations.insert(std::move(lower_case_word));
      }
    }
  }
  // Always insert the `image_info` because even if there is no annotation for
  // this image, we need to update the image status in the database so that we
  // won't re-process this image in the next user session.
  annotation_storage_->Insert(image_info, IndexingSource::kOcr);
  LogIcaUma(IcaStatus::kOcrInserted);

  // OCR is the first in the pipeline.
  if (use_ica_) {
    LogIndexingUma(IndexingStatus::kIcaStart);
    CHECK(!ica_in_use_);

    // Clear `annotations` as it's for ocr only.
    image_info.annotations.clear();

    ica_in_use_ = true;
    image_content_annotator_->AnnotateEncodedImage(
        image_info.path,
        base::BindOnce(&ImageAnnotationWorker::OnPerformIca,
                       weak_ptr_factory_.GetWeakPtr(), std::move(image_info)));
  } else {
    LogIcaUma(IcaStatus::kIcaDisabled);
    ProcessItems();
  }
}

void ImageAnnotationWorker::OnPerformIca(
    ImageInfo image_info,
    chromeos::machine_learning::mojom::ImageAnnotationResultPtr ptr) {
  if (ptr->status ==
      chromeos::machine_learning::mojom::ImageAnnotationResult::Status::OK) {
    LogIndexingUma(IndexingStatus::kIcaSucceed);
    LogIcaUma(IcaStatus::kIcaSucceed);
  } else {
    LogIcaUma(IcaStatus::kIcaFailed);
  }
  ica_in_use_ = false;
  DVLOG(1) << "OnPerformIca. Status: " << ptr->status
           << " Size: " << ptr->annotations.size();
  for (const auto& a : ptr->annotations) {
    if (a->score < kConfidenceThreshold || !a->name.has_value() ||
        a->name->empty()) {
      continue;
    }

    TokenizedString tokens(base::UTF8ToUTF16(a->name.value()), Mode::kWords);
    for (const auto& word : tokens.tokens()) {
      DVLOG(1) << "Id: " << a->id << " MId: " << a->mid
               << " Confidence score: " << static_cast<float>(a->score)
               << " Name: " << word;
      std::string annotation = base::UTF16ToUTF8(word);
      // If duplication occurs, keep the one with higher confidence score.
      if (image_info.annotation_map.contains(annotation) &&
          image_info.annotation_map[annotation].score >= a->score) {
        continue;
      }

      AnnotationInfo annotation_info;
      annotation_info.score = a->score;
      if (a->bounding_box.has_value() && image_info.width > 0 &&
          image_info.height > 0) {
        annotation_info.x =
            static_cast<float>(a->bounding_box->x()) / image_info.width;
        annotation_info.y =
            static_cast<float>(a->bounding_box->y()) / image_info.height;
        annotation_info.area =
            static_cast<float>(a->bounding_box->width()) / image_info.width *
            static_cast<float>(a->bounding_box->height()) / image_info.height;
      }
      image_info.annotation_map[annotation] = annotation_info;
    }
  }
  // Always insert the `image_info` because even if there is no annotation for
  // this image, we need to update the image status in the database so that we
  // won't re-process this image in the next user session.
  annotation_storage_->Insert(image_info, IndexingSource::kIca);
  LogIcaUma(IcaStatus::kIcaInserted);

  // ICA is the last in the pipeline.
  ProcessItems();
}

void ImageAnnotationWorker::FindAndRemoveDeletedFiles(
    std::vector<base::FilePath> files) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOG(1) << "FindAndRemoveDeletedImages.";
  task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](const std::vector<base::FilePath>& files) {
            std::vector<base::FilePath> deleted_paths;
            for (const auto& file_path : files) {
              if (!base::PathExists(file_path)) {
                deleted_paths.push_back(file_path);
              }
            }
            return deleted_paths;
          },
          std::move(files)),
      base::BindOnce(
          [](AnnotationStorage* const annotation_storage,
             std::vector<base::FilePath> paths) {
            std::for_each(paths.begin(), paths.end(),
                          [&](auto path) { annotation_storage->Remove(path); });
          },
          annotation_storage_));
}

void ImageAnnotationWorker::OnIcaDisconnected() {
  // Ensures this function is executed in the `main_task_runner_` only.
  if (!main_task_runner_->RunsTasksInCurrentSequence()) {
    main_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&ImageAnnotationWorker::OnIcaDisconnected,
                                  weak_ptr_factory_.GetWeakPtr()));
    return;
  }

  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // If this is fired during ica processing, we have called
  // `image_content_annotator_->AnnotateEncodedImage()` but have not received
  // the callback yet. The callback won't be triggered in this case and ica has
  // disconnected, thus we skip the current image.
  if (ica_in_use_) {
    LOG(ERROR) << "ICA disconnected during processing.";
    LogStatusUma(Status::kFailedToProcessIca);

    // If disconnection reaches the limit, we should no longer make ica
    // processing request.
    CHECK(num_ica_disconnection_ < kMaxDisconnection);
    num_ica_disconnection_++;
    if (num_ica_disconnection_ == kMaxDisconnection) {
      LogStatusUma(Status::kIcaDisconnectionLimit);
    }
    ica_in_use_ = false;
    // Restarts the process with a short delay to allow ica to recover.
    // This queue deals with image changes, both addition and deletion. Even
    // when reaching the limit and addition is not expected, we still need to
    // continue the process to remove deleted images from the db.
    // If `kMaxDisconnection` reached, we don't need to wait before continue the
    // process.
    main_task_runner_->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ImageAnnotationWorker::ProcessItems,
                       weak_ptr_factory_.GetWeakPtr()),
        num_ica_disconnection_ == kMaxDisconnection ? base::TimeDelta()
                                                    : kIcaReconnectDelay);
  }
  // If disconnection happens while ica is not processing, no-op.
}

void ImageAnnotationWorker::OnOcrDisconnected() {
  // Ensures this function is executed in the `main_task_runner_` only.
  if (!main_task_runner_->RunsTasksInCurrentSequence()) {
    main_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&ImageAnnotationWorker::OnOcrDisconnected,
                                  weak_ptr_factory_.GetWeakPtr()));
    return;
  }

  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // If this is fired during ocr processing, we have called
  // `optical_character_recognizer_->PerformOCR()` but have not received the
  // callback yet. The callback won't be triggered in this case and ocr has
  // disconnected, thus we skip the current image.
  if (ocr_in_use_) {
    LOG(ERROR) << "OCR disconnected during processing. ";
    LogStatusUma(Status::kFailedToProcessOcr);

    // If disconnection reaches the limit, we should no longer make ocr
    // processing request.
    CHECK(num_ocr_disconnection_ < kMaxDisconnection);
    num_ocr_disconnection_++;
    if (num_ocr_disconnection_ == kMaxDisconnection) {
      LogStatusUma(Status::kOcrDisconnectionLimit);
    }
    ocr_in_use_ = false;
    // Restarts the process with the suggested delay to allow ocr to recover.
    // This queue deals with image changes, both addition and deletion. Even
    // when reaching the limit and addition is not expected, we still need to
    // continue the process to remove deleted images from the db.
    // If `kMaxDisconnection` reached, we don't need to wait before continue the
    // process.
    main_task_runner_->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ImageAnnotationWorker::ProcessItems,
                       weak_ptr_factory_.GetWeakPtr()),
        num_ocr_disconnection_ == kMaxDisconnection
            ? base::TimeDelta()
            : SuggestedWaitTimeBeforeReAttempt(num_ocr_disconnection_));
    // As the waiting period for OCR is long, shut down ICA to save resources.
    // The connection will be resumed on the next ICA request through
    // `EnsureAnnotatorIsConnected()`.
    if (use_ica_) {
      image_content_annotator_->DisconnectAnnotator();
    }
  }
  // If disconnection happens while ocr is not processing, no-op. The
  // `OnOcrDisconnected` event will be triggered again if we attempt to make
  // request to the screen ai service before it's resumed.
}

void ImageAnnotationWorker::DisconnectAnnotators() {
  if (use_ica_) {
    image_content_annotator_->DisconnectAnnotator();
  }
  if (use_ocr_) {
    optical_character_recognizer_->DisconnectAnnotator();
  }
}

void ImageAnnotationWorker::RunFakeImageAnnotator(ImageInfo image_info) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOG(1) << "Run FilePathAnnotator.";
  const std::string annotation =
      image_info.path.BaseName().RemoveFinalExtension().value();
  image_info.annotations.insert(std::move(annotation));
  annotation_storage_->Insert(std::move(image_info), source_for_test_);
  ProcessItems();
}

void ImageAnnotationWorker::TriggerOnFileChangeForTests(
    const base::FilePath& path,
    bool error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  on_file_change_callback_.Run(path, error);
}

}  // namespace app_list
