// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_WEBRTC_DESKTOP_MEDIA_LIST_BASE_H_
#define CHROME_BROWSER_MEDIA_WEBRTC_DESKTOP_MEDIA_LIST_BASE_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/media/webrtc/desktop_media_list.h"
#include "chrome/browser/media/webrtc/desktop_media_list_observer.h"
#include "content/public/browser/desktop_media_id.h"
#include "ui/gfx/geometry/size.h"

namespace gfx {
class Image;
}

// Thumbnail size is 100*100 pixels
inline constexpr gfx::Size kDefaultThumbnailSize = {100, 100};

// Base class for DesktopMediaList implementations. Implements logic shared
// between implementations. Specifically it's responsible for keeping current
// list of sources and calling the observer when the list changes.
//
// TODO(crbug.com/40637301): Consider renaming this class.
class DesktopMediaListBase : public DesktopMediaList {
 public:
  explicit DesktopMediaListBase(base::TimeDelta update_period);

  DesktopMediaListBase(const DesktopMediaListBase&) = delete;
  DesktopMediaListBase& operator=(const DesktopMediaListBase&) = delete;

  ~DesktopMediaListBase() override;

  // DesktopMediaList interface.
  void SetUpdatePeriod(base::TimeDelta period) override;
  void SetThumbnailSize(const gfx::Size& thumbnail_size) override;
  void SetViewDialogWindowId(content::DesktopMediaID dialog_id) override;
  void StartUpdating(DesktopMediaListObserver* observer) override;
  void Update(UpdateCallback callback) override;
  int GetSourceCount() const override;
  const Source& GetSource(int index) const override;
  DesktopMediaList::Type GetMediaListType() const override;
  bool IsSourceListDelegated() const override;
  void ClearDelegatedSourceListSelection() override;
  void FocusList() override;
  void HideList() override;
  void ShowDelegatedList() override;

  static uint32_t GetImageHash(const gfx::Image& image);

 protected:
  using RefreshCallback = UpdateCallback;

  struct SourceDescription {
    SourceDescription(content::DesktopMediaID id, const std::u16string& name);

    content::DesktopMediaID id;
    std::u16string name;
  };

  DesktopMediaListBase(base::TimeDelta update_period,
                       DesktopMediaListObserver* observer);

  // Called to allow a capturer with a delegated source list to be started only
  // once the list actually starts updating. Otherwise, child classes are
  // responsible for ensuring their capturer is started at a reasonable time.
  // Only called if IsSourceListDelegated() returns true.
  virtual void StartDelegatedCapturer() {}

  // Before this method is called, |refresh_callback_| must be non-null, and
  // after it completes (usually asychnonrously), |refresh_callback_| must be
  // null.  Since |refresh_callback_| is private, subclasses can check this
  // condition by calling can_refresh().
  virtual void Refresh(bool update_thumnails) = 0;

  // Update source media list to observer.
  void UpdateSourcesList(const std::vector<SourceDescription>& new_sources);

  // Update a thumbnail to observer.
  void UpdateSourceThumbnail(const content::DesktopMediaID& id,
                             const gfx::ImageSkia& image);

  void UpdateSourcePreview(const content::DesktopMediaID& id,
                           const gfx::ImageSkia& image);

  // Called when a refresh is complete.  Invokes |refresh_callback_| unless it
  // is null.  Postcondition: |refresh_callback_| is null.
  void OnRefreshComplete();

  void OnDelegatedSourceListSelection();
  void OnDelegatedSourceListDismissed();

  bool can_refresh() const { return !refresh_callback_.is_null(); }

  void SetPreviewedSource(
      const std::optional<content::DesktopMediaID>& id) override {}

  // Size of thumbnails generated by the model.
  gfx::Size thumbnail_size_ = kDefaultThumbnailSize;

  // ID of the hosting dialog.
  content::DesktopMediaID view_dialog_id_ =
      content::DesktopMediaID(content::DesktopMediaID::TYPE_NONE, -1);

  // Desktop media type of the list.
  DesktopMediaList::Type type_ = DesktopMediaList::Type::kNone;

 private:
  // Post a task for next list update.
  void ScheduleNextRefresh();

  // Time interval between mode updates.
  base::TimeDelta update_period_;

  // Current list of sources.
  std::vector<Source> sources_;

  // The observer passed to StartUpdating().
  raw_ptr<DesktopMediaListObserver> observer_ = nullptr;

  // Called when a refresh operation completes.
  RefreshCallback refresh_callback_;

  base::WeakPtrFactory<DesktopMediaListBase> weak_factory_{this};
};

#endif  // CHROME_BROWSER_MEDIA_WEBRTC_DESKTOP_MEDIA_LIST_BASE_H_
