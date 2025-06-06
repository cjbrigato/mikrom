// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/zoom/zoom_controller.h"

#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/observer_list.h"
#include "components/zoom/zoom_event_manager.h"
#include "components/zoom/zoom_observer.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/page_type.h"
#include "net/base/url_util.h"
#include "third_party/blink/public/common/page/page_zoom.h"

using content::BrowserThread;

namespace zoom {

double ZoomController::GetZoomLevelForWebContents(
    content::WebContents* web_contents) {
  if (!web_contents)
    return 0.0;

  auto* zoom_controller = FromWebContents(web_contents);
  if (zoom_controller)
    return zoom_controller->GetZoomLevel();

  return content::HostZoomMap::GetZoomLevel(web_contents);
}

// static
ZoomController* ZoomController::CreateForWebContents(
    content::WebContents* web_contents) {
  return CreateForWebContentsAndRenderFrameHost(
      web_contents, web_contents->GetPrimaryMainFrame()->GetGlobalId());
}

// static
ZoomController* ZoomController::CreateForWebContentsAndRenderFrameHost(
    content::WebContents* web_contents,
    content::GlobalRenderFrameHostId rfh_id) {
  if (!Manager::FromWebContents(web_contents)) {
    Manager::CreateForWebContents(web_contents);
  }
  auto* manager = Manager::FromWebContents(web_contents);
  manager->AddZoomControllerIfNecessary(web_contents, rfh_id);
  return manager->GetZoomController(rfh_id);
}

// static
ZoomController* ZoomController::FromWebContents(
    const content::WebContents* web_contents) {
  return FromWebContentsAndRenderFrameHost(
      web_contents, web_contents->GetPrimaryMainFrame()->GetGlobalId());
}

// static
ZoomController* ZoomController::FromWebContentsAndRenderFrameHost(
    const content::WebContents* web_contents,
    content::GlobalRenderFrameHostId rfh_id) {
  auto* manager = ZoomController::Manager::FromWebContents(web_contents);
  return manager ? manager->GetZoomController(rfh_id) : nullptr;
}

ZoomController::Manager::Manager(content::WebContents* web_contents)
    : content::WebContentsUserData<ZoomController::Manager>(*web_contents) {
  // Note: can't use make_unique<> below since ZoomController's constructor is
  // protected.
  auto* rfh = web_contents->GetPrimaryMainFrame();
  const content::FrameTreeNodeId ftn_id = rfh->GetFrameTreeNodeId();
  // A simple insertion is safe here as the map has just been created and is
  // thus empty.
  zoom_controller_map_[ftn_id] =
      base::WrapUnique(new ZoomController(web_contents, rfh));
}

ZoomController::Manager::~Manager() = default;

ZoomController* ZoomController::Manager::GetZoomController(
    const content::GlobalRenderFrameHostId& rfh_id) const {
  auto ftn_id = content::RenderFrameHost::FromID(rfh_id)->GetFrameTreeNodeId();
  auto it = zoom_controller_map_.find(ftn_id);
  if (it == zoom_controller_map_.end()) {
    return nullptr;
  }
  return it->second.get();
}

void ZoomController::Manager::AddZoomControllerIfNecessary(
    content::WebContents* web_contents,
    const content::GlobalRenderFrameHostId& rfh_id) {
  auto* rfh = content::RenderFrameHost::FromID(rfh_id);
  // `rfh` should always be defined at this point. The two callers of this
  // are (1) ZoomController::CreateForWebContents, in which case the `rfh` is
  // the primary main frame, and although it can be null during shutdown that
  // shouldn't be the case here, and (2) GuestViewBase::InitWithGuestPageHolder,
  // in which case we've already derefed another pointer to the same `rfh` to
  // get the ID to pass here. In (2) it's possible that `FromId(rfh_id)` could
  // be null if the `rfh` is in its destructor, but again that shouldn't be the
  // case here.
  CHECK(rfh);
  auto ftn_id = rfh->GetFrameTreeNodeId();
  if (zoom_controller_map_.contains(ftn_id)) {
    return;
  }
  zoom_controller_map_[ftn_id] =
      base::WrapUnique(new ZoomController(web_contents, rfh));
}

void ZoomController::Manager::FrameDeleted(content::FrameTreeNodeId ftn_id) {
  zoom_controller_map_.erase(ftn_id);
}

ZoomController::ZoomController(content::WebContents* web_contents,
                               content::RenderFrameHost* rfh)
    : content::WebContentsObserver(web_contents),
      frame_tree_node_id_(rfh->GetFrameTreeNodeId()),
      browser_context_(web_contents->GetBrowserContext()) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  host_zoom_map_ = content::HostZoomMap::Get(rfh->GetSiteInstance());
  host_zoom_map_->SetIndependentZoomForFrameTreeNode(web_contents,
                                                     frame_tree_node_id_);
  zoom_level_ = host_zoom_map_->GetDefaultZoomLevel();

  zoom_subscription_ =
      host_zoom_map_->AddZoomLevelChangedCallback(base::BindRepeating(
          &ZoomController::OnZoomLevelChanged, base::Unretained(this)));

  UpdateState(std::string());
}

ZoomController::~ZoomController() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  host_zoom_map_->ClearIndependentZoomForFrameTreeNode(frame_tree_node_id_);
  for (auto& observer : observers_) {
    observer.OnZoomControllerDestroyed(this);
  }
}

content::RenderFrameHost* ZoomController::GetRenderFrameHost() const {
  return web_contents()->UnsafeFindFrameByFrameTreeNodeId(frame_tree_node_id_);
}

bool ZoomController::IsAtDefaultZoom() const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return blink::ZoomValuesEqual(GetZoomLevel(), GetDefaultZoomLevel());
}

ZoomController::RelativeZoom ZoomController::GetZoomRelativeToDefault() const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  double current_level = GetZoomLevel();
  double default_level = GetDefaultZoomLevel();
  if (blink::ZoomValuesEqual(current_level, default_level)) {
    return ZOOM_AT_DEFAULT_ZOOM;
  }
  if (current_level > default_level)
    return ZOOM_ABOVE_DEFAULT_ZOOM;
  return ZOOM_BELOW_DEFAULT_ZOOM;
}

void ZoomController::AddObserver(ZoomObserver* observer) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  observers_.AddObserver(observer);
}

void ZoomController::RemoveObserver(ZoomObserver* observer) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  observers_.RemoveObserver(observer);
}

double ZoomController::GetZoomLevel() const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  auto result = zoom_mode_ == ZOOM_MODE_MANUAL
                    ? zoom_level_
                    : content::HostZoomMap::GetZoomLevel(
                          web_contents(), GetRenderFrameHost()->GetGlobalId());
  return result;
}

int ZoomController::GetZoomPercent() const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  double zoom_factor = blink::ZoomLevelToZoomFactor(GetZoomLevel());
  // Round double for return.
  return static_cast<int>(zoom_factor * 100 + 0.5);
}

bool ZoomController::SetZoomLevel(double zoom_level) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // A client did not initiate this zoom change.
  return SetZoomLevelByClient(zoom_level, nullptr);
}

bool ZoomController::SetZoomLevelByClient(
    double zoom_level,
    const scoped_refptr<const ZoomRequestClient>& client) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  auto* rfh = GetRenderFrameHost();
  // Cannot zoom in disabled mode. Also, don't allow changing zoom level on
  // a crashed tab, an error page or an interstitial page.
  if (zoom_mode_ == ZOOM_MODE_DISABLED ||
      !GetRenderFrameHost()->IsRenderFrameLive()) {
    return false;
  }

  // Store client data so the |client| can be attributed when the zoom
  // change completes. We expect that by the time this function returns that
  // any observers that require this information will have requested it.
  last_client_ = client;

  // Do not actually rescale the page in manual mode.
  if (zoom_mode_ == ZOOM_MODE_MANUAL) {
    // If the zoom level hasn't changed, early out to avoid sending an event.
    if (blink::ZoomValuesEqual(zoom_level_, zoom_level)) {
      return true;
    }

    double old_zoom_level = zoom_level_;
    zoom_level_ = zoom_level;

    // TODO(wjmaclean) Do we care about filling in host/scheme here?
    content::HostZoomMap::ZoomLevelChange change;
    change.mode = content::HostZoomMap::ZOOM_CHANGED_TEMPORARY_ZOOM;
    change.zoom_level = zoom_level;
    ZoomEventManager::GetForBrowserContext(browser_context_)
        ->OnZoomLevelChanged(change);

    bool can_show_bubble = can_show_bubble_;
    if (client && client->ShouldSuppressBubble())
      can_show_bubble = false;

    ZoomChangedEventData zoom_change_data(
        web_contents(), rfh->GetFrameTreeNodeId(), old_zoom_level, zoom_level_,
        zoom_mode_, can_show_bubble);
    for (auto& observer : observers_)
      observer.OnZoomChanged(zoom_change_data);

    last_client_ = nullptr;
    return true;
  }

  content::HostZoomMap* zoom_map =
      content::HostZoomMap::Get(rfh->GetSiteInstance());
  DCHECK(zoom_map);
  DCHECK(!event_data_);
  CHECK_EQ(zoom_map, host_zoom_map_);
  event_data_ = std::make_unique<ZoomChangedEventData>(
      web_contents(), rfh->GetFrameTreeNodeId(), GetZoomLevel(), zoom_level,
      zoom_mode_, false /* can_show_bubble */);

  if (zoom_mode_ == ZOOM_MODE_ISOLATED ||
      zoom_map->UsesTemporaryZoomLevel(rfh->GetGlobalId())) {
    zoom_map->SetTemporaryZoomLevel(rfh->GetGlobalId(), zoom_level);
  } else {
    const GURL url =
        content::HostZoomMap::GetURLForRenderFrameHost(rfh->GetGlobalId());
    if (url.is_empty()) {
      last_client_ = nullptr;
      // If we exit without triggering an update, we should clear event_data_,
      // else we may later trigger a DCHECK(event_data_).
      event_data_.reset();
      return false;
    }
    std::string host = net::GetHostOrSpecFromURL(url);
    zoom_map->SetZoomLevelForHost(host, zoom_level);
  }

  DCHECK(!event_data_);
  last_client_ = nullptr;
  return true;
}

void ZoomController::SetZoomMode(ZoomMode new_mode) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (new_mode == zoom_mode_)
    return;

  auto* rfh = GetRenderFrameHost();
  auto rfh_id = rfh->GetGlobalId();
  content::HostZoomMap* zoom_map =
      content::HostZoomMap::Get(rfh->GetSiteInstance());
  DCHECK(zoom_map);
  double original_zoom_level = GetZoomLevel();

  DCHECK(!event_data_);
  event_data_ = std::make_unique<ZoomChangedEventData>(
      web_contents(), rfh->GetFrameTreeNodeId(), original_zoom_level,
      original_zoom_level, new_mode, new_mode != ZOOM_MODE_DEFAULT);

  switch (new_mode) {
    case ZOOM_MODE_DEFAULT: {
      const GURL url =
          content::HostZoomMap::GetURLForRenderFrameHost(rfh->GetGlobalId());

      if (!url.is_empty()) {
        std::string host = net::GetHostOrSpecFromURL(url);

        if (zoom_map->HasZoomLevel(url.scheme(), host)) {
          // If there are other tabs with the same origin, then set this tab's
          // zoom level to match theirs. The temporary zoom level will be
          // cleared below, but this call will make sure this tab re-draws at
          // the correct zoom level.
          double origin_zoom_level =
              zoom_map->GetZoomLevelForHostAndScheme(url.scheme(), host);
          event_data_->new_zoom_level = origin_zoom_level;
          zoom_map->SetTemporaryZoomLevel(rfh_id, origin_zoom_level);
        } else {
          // The host will need a level prior to removing the temporary level.
          // We don't want the zoom level to change just because we entered
          // default mode.
          zoom_map->SetZoomLevelForHost(host, original_zoom_level);
        }
      }
      // Remove per-tab zoom data for this tab. No event callback expected.
      zoom_map->ClearTemporaryZoomLevel(rfh_id);
      break;
    }
    case ZOOM_MODE_ISOLATED: {
      // Unless the zoom mode was |ZOOM_MODE_DISABLED| before this call, the
      // page needs an initial isolated zoom back to the same level it was at
      // in the other mode.
      if (zoom_mode_ != ZOOM_MODE_DISABLED) {
        zoom_map->SetTemporaryZoomLevel(rfh_id, original_zoom_level);
      } else {
        // When we don't call any HostZoomMap set functions, we send the event
        // manually.
        for (auto& observer : observers_)
          observer.OnZoomChanged(*event_data_);
        event_data_.reset();
      }
      break;
    }
    case ZOOM_MODE_MANUAL: {
      // Unless the zoom mode was |ZOOM_MODE_DISABLED| before this call, the
      // page needs to be resized to the default zoom. While in manual mode,
      // the zoom level is handled independently.
      if (zoom_mode_ != ZOOM_MODE_DISABLED) {
        zoom_map->SetTemporaryZoomLevel(rfh_id, GetDefaultZoomLevel());
        zoom_level_ = original_zoom_level;
      } else {
        // When we don't call any HostZoomMap set functions, we send the event
        // manually.
        for (auto& observer : observers_)
          observer.OnZoomChanged(*event_data_);
        event_data_.reset();
      }
      break;
    }
    case ZOOM_MODE_DISABLED: {
      // The page needs to be zoomed back to default before disabling the zoom
      double new_zoom_level = GetDefaultZoomLevel();
      event_data_->new_zoom_level = new_zoom_level;
      zoom_map->SetTemporaryZoomLevel(rfh_id, new_zoom_level);
      break;
    }
  }
  // Any event data we've stored should have been consumed by this point.
  DCHECK(!event_data_);

  zoom_mode_ = new_mode;
}

void ZoomController::ResetZoomModeOnNavigationIfNeeded(const GURL& url) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (zoom_mode_ != ZOOM_MODE_ISOLATED && zoom_mode_ != ZOOM_MODE_MANUAL)
    return;

  auto* rfh = GetRenderFrameHost();
  content::HostZoomMap* zoom_map =
      content::HostZoomMap::Get(rfh->GetSiteInstance());
  zoom_level_ = zoom_map->GetDefaultZoomLevel();
  // TODO(https://crbug.com/376084060): Once we allow subframes to have their
  // own ZoomControllers, we'll need to modify the following call to indicate
  // which RenderFrameHost we want the ZoomLevel for.
  double old_zoom_level =
      zoom_map->GetZoomLevel(web_contents(), rfh->GetGlobalId());
  double new_zoom_level = zoom_map->GetZoomLevelForHostAndScheme(
      url.scheme(), net::GetHostOrSpecFromURL(url));
  event_data_ = std::make_unique<ZoomChangedEventData>(
      web_contents(), rfh->GetFrameTreeNodeId(), old_zoom_level, new_zoom_level,
      ZOOM_MODE_DEFAULT, false /* can_show_bubble */);
  // The call to ClearTemporaryZoomLevel() doesn't generate any events from
  // HostZoomMap, but the call to UpdateState() at the end of
  // DidFinishNavigation will notify our observers.
  // Note: it's possible the render_process/frame ids have disappeared (e.g.
  // if we navigated to a new origin), but this won't cause a problem in the
  // call below.
  zoom_map->ClearTemporaryZoomLevel(rfh->GetGlobalId());
  zoom_mode_ = ZOOM_MODE_DEFAULT;
}

void ZoomController::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (navigation_handle->GetFrameTreeNodeId() != frame_tree_node_id_ ||
      !navigation_handle->HasCommitted()) {
    return;
  }

  if (navigation_handle->IsErrorPage())
    content::HostZoomMap::SendErrorPageZoomLevelRefresh(web_contents());

  if (!navigation_handle->IsSameDocument())
    ResetZoomModeOnNavigationIfNeeded(navigation_handle->GetURL());

  // If the main frame's content has changed, the new page may have a different
  // zoom level from the old one.
  UpdateState(std::string());
  DCHECK(!event_data_);
  last_page_scale_factor_was_one_ = PageScaleFactorIsOne();
}

void ZoomController::WebContentsDestroyed() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // At this point we should no longer be sending any zoom events with this
  // WebContents.
  for (auto& observer : observers_) {
    observer.OnZoomControllerDestroyed(this);
  }
}

void ZoomController::RenderFrameHostChanged(
    content::RenderFrameHost* old_host,
    content::RenderFrameHost* new_host) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // This function gets called for all activity on the WebContents, but we are
  // only interested when the change corresponds to our FrameTreeNode.
  if (new_host->GetFrameTreeNodeId() != frame_tree_node_id_) {
    return;
  }

  // If our associated HostZoomMap changes, update our subscription.
  content::HostZoomMap* new_host_zoom_map =
      content::HostZoomMap::Get(new_host->GetSiteInstance());
  if (new_host_zoom_map == host_zoom_map_) {
    return;
  }

  host_zoom_map_->ClearIndependentZoomForFrameTreeNode(frame_tree_node_id_);
  host_zoom_map_ = new_host_zoom_map;
  host_zoom_map_->SetIndependentZoomForFrameTreeNode(web_contents(),
                                                     frame_tree_node_id_);
  zoom_subscription_ =
      host_zoom_map_->AddZoomLevelChangedCallback(base::BindRepeating(
          &ZoomController::OnZoomLevelChanged, base::Unretained(this)));
}

void ZoomController::FrameDeleted(content::FrameTreeNodeId ftn_id) {
  if (ftn_id != frame_tree_node_id_) {
    // This doesn't concern us.
    return;
  }
  Manager::FromWebContents(web_contents())->FrameDeleted(ftn_id);
  // Do not add code past this point, as we have been deleted.
}

void ZoomController::OnPageScaleFactorChanged(float page_scale_factor) {
  const bool is_one = page_scale_factor == 1.f;
  if (is_one != last_page_scale_factor_was_one_) {
    // We send a no-op zoom change to inform observers that PageScaleFactorIsOne
    // has changed.
    UpdateState(std::string());
    last_page_scale_factor_was_one_ = is_one;
  }
}

void ZoomController::OnZoomLevelChanged(
    const content::HostZoomMap::ZoomLevelChange& change) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  UpdateState(change.host);
}

void ZoomController::UpdateState(const std::string& host) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  auto* rfh = GetRenderFrameHost();
  // If |host| is empty, all observers should be updated.
  if (!host.empty()) {
    // Get the (non-virtual) url to be tracked by the HostZoomMap. Getting urls
    // directly from a WebContents may result in a virtual url, so prefer using
    // the value from the `rfh` instead, per https://crbug.com/40290372. There
    // are several cases here:
    // 1) This ZoomController is for a WebContent's main
    //    frame, either a primary main frame or a GuestView main frame when
    //    kGuestViewMPArch is disabled.
    // 2) This ZoomController is for an independently-zoomable main frame (e.g.
    //    corresponding to a FrameTree root for a GuestView when
    //    kGuestViewMPArch is enabled). In this case we want the url from the
    //    `rfh` as it corresponds to what is being zoomed, and should be
    //    recorded in the HostZoomMap accordingly.
    // 3) Although it doesn't happen in practice (yet), in future there will be
    //    support for independently-zoomable subframes that have their own
    //    RenderWidgetHosts (i.e. their RenderFrameHosts are local roots). In
    //    that case we still want the `url` for `rfh` as it corresponds to what
    //    is being zoomed.
    const GURL url =
        content::HostZoomMap::GetURLForRenderFrameHost(rfh->GetGlobalId());
    if (url.is_empty() || host != net::GetHostOrSpecFromURL(url)) {
      return;
    }
  }

  if (event_data_) {
    // For state changes initiated within the ZoomController, information about
    // the change should be sent.
    ZoomChangedEventData zoom_change_data = *event_data_;
    event_data_.reset();
    // The zoom bubble should not be shown for zoom changes where the host
    // is empty or when zoom level is not changed from default.
    const bool changed_from_default =
        zoom_change_data.new_zoom_level != zoom_change_data.old_zoom_level ||
        zoom_change_data.new_zoom_level != GetDefaultZoomLevel();
    zoom_change_data.can_show_bubble =
        can_show_bubble_ && !host.empty() && changed_from_default;
    for (auto& observer : observers_) {
      observer.OnZoomChanged(zoom_change_data);
    }
  } else {
    // TODO(wjmaclean) Should we consider having HostZoomMap send both old and
    // new zoom levels here?
    double zoom_level = GetZoomLevel();
    // We never show a zoom bubble for an event we didn't generate.
    ZoomChangedEventData zoom_change_data(
        web_contents(), rfh->GetFrameTreeNodeId(), zoom_level, zoom_level,
        zoom_mode_, false /* can_show_bubble */);
    for (auto& observer : observers_)
      observer.OnZoomChanged(zoom_change_data);
  }
}

void ZoomController::SetPageScaleFactorIsOneForTesting(bool is_one) {
  page_scale_factor_is_one_for_testing_ = is_one;

  if (is_one != last_page_scale_factor_was_one_) {
    // See OnPageScaleFactorChanged for why this is done.
    UpdateState(std::string());
    last_page_scale_factor_was_one_ = is_one;
  }
}

bool ZoomController::PageScaleFactorIsOne() const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (page_scale_factor_is_one_for_testing_.has_value())
    return page_scale_factor_is_one_for_testing_.value();

  return web_contents()->GetPrimaryPage().IsPageScaleFactorOne();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(ZoomController::Manager);

}  // namespace zoom
