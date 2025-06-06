// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ZOOM_ZOOM_CONTROLLER_H_
#define COMPONENTS_ZOOM_ZOOM_CONTROLLER_H_

#include <memory>
#include <optional>

#include "base/compiler_specific.h"
#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/observer_list.h"
#include "components/prefs/pref_member.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

class ZoomControllerTest;

namespace content {
class WebContents;
}

namespace zoom {
class ZoomObserver;

class ZoomRequestClient : public base::RefCounted<ZoomRequestClient> {
 public:
  ZoomRequestClient() = default;

  ZoomRequestClient(const ZoomRequestClient&) = delete;
  ZoomRequestClient& operator=(const ZoomRequestClient&) = delete;

  virtual bool ShouldSuppressBubble() const = 0;

 protected:
  virtual ~ZoomRequestClient() = default;

 private:
  friend class base::RefCounted<ZoomRequestClient>;
};

// ZoomController manages zoom changes and the Omnibox zoom icon. It can be
// created for main frames and subframes. Creating for subframes allows those
// frames to have zoom behavior independent from the main frame's. Lives on the
// UI thread.
class ZoomController : public content::WebContentsObserver {
 public:
  // Defines how zoom changes are handled.
  enum ZoomMode {
    // Results in default zoom behavior, i.e. zoom changes are handled
    // automatically and on a per-origin basis, meaning that other tabs
    // navigated to the same origin will also zoom.
    ZOOM_MODE_DEFAULT,
    // Results in zoom changes being handled automatically, but on a per-tab
    // basis. Tabs in this zoom mode will not be affected by zoom changes in
    // other tabs, and vice versa.
    ZOOM_MODE_ISOLATED,
    // Overrides the automatic handling of zoom changes. The |onZoomChange|
    // event will still be dispatched, but the page will not actually be zoomed.
    // These zoom changes can be handled manually by listening for the
    // |onZoomChange| event. Zooming in this mode is also on a per-tab basis.
    ZOOM_MODE_MANUAL,
    // Disables all zooming in this tab. The tab will revert to the default
    // zoom level, and all attempted zoom changes will be ignored.
    ZOOM_MODE_DISABLED,
  };

  enum RelativeZoom {
    ZOOM_BELOW_DEFAULT_ZOOM,
    ZOOM_AT_DEFAULT_ZOOM,
    ZOOM_ABOVE_DEFAULT_ZOOM
  };

  struct ZoomChangedEventData {
    ZoomChangedEventData(content::WebContents* web_contents,
                         content::FrameTreeNodeId ftn_id,
                         double old_zoom_level,
                         double new_zoom_level,
                         ZoomController::ZoomMode zoom_mode,
                         bool can_show_bubble)
        : web_contents(web_contents),
          frame_tree_node_id(ftn_id),
          old_zoom_level(old_zoom_level),
          new_zoom_level(new_zoom_level),
          zoom_mode(zoom_mode),
          can_show_bubble(can_show_bubble) {}
    raw_ptr<content::WebContents> web_contents;
    // Since there can be multiple ZoomControllers for a given WebContents,
    // include the FrameTreeNodeId to uniquely identify which one created this
    // struct. One case where a single WebContents will have multiple
    // ZoomControllers is in a GuestView with features::kGuestViewMPArch
    // enabled.
    content::FrameTreeNodeId frame_tree_node_id;
    double old_zoom_level;
    double new_zoom_level;
    ZoomController::ZoomMode zoom_mode;
    bool can_show_bubble;
  };

  // Since it's possible for a WebContents to not have a ZoomController, provide
  // a simple, safe and reliable method to find the current zoom level for a
  // given WebContents*.
  static double GetZoomLevelForWebContents(content::WebContents* web_contents);

  // Used to create a ZoomController for the primary mainframe of
  // `web_contents`.
  static ZoomController* CreateForWebContents(
      content::WebContents* web_contents);

  // Use to create a ZoomController for a subframe in `web_contents`. The
  // specified `rfh_id` must be for a local-root RenderFrameHost.
  static ZoomController* CreateForWebContentsAndRenderFrameHost(
      content::WebContents* web_contents,
      content::GlobalRenderFrameHostId rfh_id);

  // Retrieves the ZoomController for `web_contents` primary mainframe if it
  // exists, otherwise returns nullptr.
  static ZoomController* FromWebContents(
      const content::WebContents* web_contents);

  // Retrieves the ZoomController for `web_contents` and the specified `rfh_id`
  // if it exists, otherwise returns nullptr.
  static ZoomController* FromWebContentsAndRenderFrameHost(
      const content::WebContents* web_contents,
      content::GlobalRenderFrameHostId rfh_id);

  ZoomController(const ZoomController&) = delete;
  ZoomController& operator=(const ZoomController&) = delete;

  ~ZoomController() override;

  ZoomMode zoom_mode() const { return zoom_mode_; }

  // Convenience method to get default zoom level. Implemented here for
  // inlining.
  double GetDefaultZoomLevel() const {
    return content::HostZoomMap::GetForWebContents(web_contents())
        ->GetDefaultZoomLevel();
  }

  // Convenience method to quickly check if the tab's at default zoom.
  // Virtual for testing.
  virtual bool IsAtDefaultZoom() const;

  // Returns which image should be loaded for the current zoom level.
  RelativeZoom GetZoomRelativeToDefault() const;

  const ZoomRequestClient* last_client() const { return last_client_.get(); }

  void AddObserver(ZoomObserver* observer);
  void RemoveObserver(ZoomObserver* observer);

  // Used to set whether the zoom notification bubble can be shown when the
  // zoom level is changed for this controller. Default behavior is to show
  // the bubble.
  void SetShowsNotificationBubble(bool can_show_bubble) {
    can_show_bubble_ = can_show_bubble;
  }

  // Gets the current zoom level by querying HostZoomMap (if not in manual zoom
  // mode) or from the ZoomController local value otherwise.
  double GetZoomLevel() const;
  // Calls GetZoomLevel() then converts the returned value to a percentage
  // zoom factor.
  // Virtual for testing.
  virtual int GetZoomPercent() const;

  // Sets the zoom level through HostZoomMap.
  // Returns true on success.
  bool SetZoomLevel(double zoom_level);

  // Sets the zoom level via HostZoomMap (or stores it locally if in manual zoom
  // mode), and attributes the zoom to |client|. Returns true on success.
  bool SetZoomLevelByClient(
      double zoom_level,
      const scoped_refptr<const ZoomRequestClient>& client);

  // Sets the zoom mode, which defines zoom behavior (see enum ZoomMode).
  void SetZoomMode(ZoomMode zoom_mode);

  // Set and query whether or not the page scale factor is one.
  void SetPageScaleFactorIsOneForTesting(bool is_one);
  bool PageScaleFactorIsOne() const;

  // content::WebContentsObserver overrides:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void WebContentsDestroyed() override;
  void RenderFrameHostChanged(content::RenderFrameHost* old_host,
                              content::RenderFrameHost* new_host) override;
  void OnPageScaleFactorChanged(float page_scale_factor) override;
  void FrameDeleted(content::FrameTreeNodeId ftn_id) override;

 protected:
  // Protected for testing.
  explicit ZoomController(content::WebContents* web_contents,
                          content::RenderFrameHost* rfh);

 private:
  friend class ::ZoomControllerTest;

  // A class to (i) be owned by WebContents as UserData, and (ii) own and manage
  // all the ZoomControllers in that WebContents.
  class Manager : public content::WebContentsUserData<Manager> {
   public:
    explicit Manager(content::WebContents* web_contents);
    ~Manager() override;

    ZoomController* GetZoomController(
        const content::GlobalRenderFrameHostId& rfh_id) const;

    void AddZoomControllerIfNecessary(
        content::WebContents* web_contents,
        const content::GlobalRenderFrameHostId& rfh_id);

    // Called from ZoomController to notify that one of the ZoomControllers has
    // had its frame deleted, meaning the ZoomCOntroller itself should be
    // deleted.
    void FrameDeleted(content::FrameTreeNodeId ftn_id);

   private:
    friend class content::WebContentsUserData<Manager>;

    // The map is keyed on FrameTreeNodeId, but this class will
    // have to update the map to account for frames being deleted. The
    // ZoomController object will call out to the manager to provide the
    // details.
    base::flat_map<content::FrameTreeNodeId, std::unique_ptr<ZoomController>>
        zoom_controller_map_;

    WEB_CONTENTS_USER_DATA_KEY_DECL();
  };

  // Note: this function uses WebContents::UnsafeFindFrameByFrameTreeNodeId,
  // so the RenderFrameHost* returned should be used immediately, and not
  // stored.
  content::RenderFrameHost* GetRenderFrameHost() const;
  void ResetZoomModeOnNavigationIfNeeded(const GURL& url);
  void OnZoomLevelChanged(const content::HostZoomMap::ZoomLevelChange& change);

  // Updates the zoom icon and zoom percentage based on current values and
  // notifies the observer if changes have occurred. |host| may be empty,
  // meaning the change should apply to ~all sites. If it is not empty, the
  // change only affects sites with the given host.
  void UpdateState(const std::string& host);

  // Stores the FrameTreeNodeId of the RenderFrameHost this ZoomController was
  // created with.
  const content::FrameTreeNodeId frame_tree_node_id_;

  // True if changes to zoom level can trigger the zoom notification bubble.
  bool can_show_bubble_ = true;

  // The current zoom mode.
  ZoomMode zoom_mode_ = ZOOM_MODE_DEFAULT;

  // Current zoom level.
  double zoom_level_;

  std::unique_ptr<ZoomChangedEventData> event_data_;

  // Keeps track of the extension (if any) that initiated the last zoom change
  // that took effect.
  scoped_refptr<const ZoomRequestClient> last_client_;

  // Observer receiving notifications on state changes.
  base::ObserverList<ZoomObserver> observers_;

  raw_ptr<content::BrowserContext> browser_context_;
  // Keep track of the HostZoomMap we're currently subscribed to.
  raw_ptr<content::HostZoomMap> host_zoom_map_;

  base::CallbackListSubscription zoom_subscription_;

  // Whether the page scale factor was one the last time we notified our
  // observers of a change to PageScaleFactorIsOne.
  bool last_page_scale_factor_was_one_ = true;

  // If set, this value is returned in PageScaleFactorIsOne.
  std::optional<bool> page_scale_factor_is_one_for_testing_;
};

}  // namespace zoom

#endif  // COMPONENTS_ZOOM_ZOOM_CONTROLLER_H_
