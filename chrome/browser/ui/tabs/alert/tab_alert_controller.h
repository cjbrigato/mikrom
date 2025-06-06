// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_TABS_ALERT_TAB_ALERT_CONTROLLER_H_
#define CHROME_BROWSER_UI_TABS_ALERT_TAB_ALERT_CONTROLLER_H_

#include <optional>
#include <vector>

#include "base/callback_list.h"
#include "base/containers/flat_set.h"
#include "base/scoped_observation.h"
#include "chrome/browser/media/webrtc/media_stream_capture_indicator.h"
#include "chrome/browser/ui/tabs/alert/tab_alert.h"
#include "chrome/browser/ui/tabs/contents_observing_tab_feature.h"

namespace content {
enum class WebContentsCapabilityType;
}  // namespace content

namespace tabs {
// Comparator used to determine which tab alert has a higher priority to be
// shown.
struct CompareAlerts {
  bool operator()(TabAlert first, TabAlert second) const;
};

// Observes the corresponding web contents for the tab to keep track of all
// active alerts. Callers can subscribe and be notified when the tab alert that
// should be shown changes.
class TabAlertController : public tabs::ContentsObservingTabFeature,
                           public MediaStreamCaptureIndicator::Observer {
 public:
  explicit TabAlertController(TabInterface& tab);
  TabAlertController(const TabAlertController&) = delete;
  TabAlertController& operator=(const TabAlertController&) = delete;
  ~TabAlertController() override;

  using AlertToShowChangedCallback =
      base::RepeatingCallback<void(std::optional<TabAlert>)>;
  base::CallbackListSubscription AddAlertToShowChangedCallback(
      AlertToShowChangedCallback callback);

  std::optional<TabAlert> GetAlertToShow() const;
  // Gets all active tab alerts that is sorted from highest priority
  // to lowest priority to be shown.
  std::vector<TabAlert> GetAllActiveAlerts();

  // WebContentsObserver override:
  void OnCapabilityTypesChanged(
      content::WebContentsCapabilityType capability_type,
      bool used) override;
  void MediaPictureInPictureChanged(bool is_picture_in_picture) override;
  void DidUpdateAudioMutingState(bool muted) override;
  void OnAudioStateChanged(bool audible) override;

  // MediaStreamCaptureIndicator::Observer override:
  void OnIsCapturingVideoChanged(content::WebContents* contents,
                                 bool is_capturing_video) override;
  void OnIsCapturingAudioChanged(content::WebContents* contents,
                                 bool is_capturing_audio) override;
  void OnIsBeingMirroredChanged(content::WebContents* contents,
                                bool is_being_mirrored) override;
  void OnIsCapturingWindowChanged(content::WebContents* contents,
                                  bool is_capturing_window) override;
  void OnIsCapturingDisplayChanged(content::WebContents* contents,
                                   bool is_capturing_display) override;

 private:
  // Adds `alert` to the set of already active alerts for this tab if it isn't
  // currently active. Otherwise, removes `alert` from the set and is considered
  // inactive.
  void UpdateAlertState(TabAlert alert, bool is_active);

  using AlertToShowChangedCallbackList =
      base::RepeatingCallbackList<void(std::optional<TabAlert>)>;
  AlertToShowChangedCallbackList alert_to_show_changed_callbacks_;

  // Maintains a sorted collection of all active tab alerts from highest
  // priority to lowest priority to be shown.
  base::flat_set<TabAlert, CompareAlerts> active_alerts_;

  base::ScopedObservation<MediaStreamCaptureIndicator,
                          MediaStreamCaptureIndicator::Observer>
      media_stream_capture_indicator_observation_{this};
};
}  // namespace tabs

#endif  // CHROME_BROWSER_UI_TABS_ALERT_TAB_ALERT_CONTROLLER_H_
