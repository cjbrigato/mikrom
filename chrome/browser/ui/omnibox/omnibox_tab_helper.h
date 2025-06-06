// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_OMNIBOX_OMNIBOX_TAB_HELPER_H_
#define CHROME_BROWSER_UI_OMNIBOX_OMNIBOX_TAB_HELPER_H_

#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/scoped_observation.h"
#include "chrome/browser/page_content_annotations/page_content_extraction_service.h"
#include "components/omnibox/common/omnibox_focus_state.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class WebContents;
}

class Profile;

// Serves as a bridge between omnibox and other UIs. Allows registration of
// observers to listen for omnibox updates. Also allows the omnibox to change
// rendering based on the web contents.
class OmniboxTabHelper
    : public content::WebContentsUserData<OmniboxTabHelper>,
      public page_content_annotations::PageContentExtractionService::Observer,
      public content::WebContentsObserver {
 public:
  // Observer to listen for updates from OmniboxTabHelper.
  class Observer : public base::CheckedObserver {
   public:
    // Invoked when the omnibox input state is changed.
    virtual void OnOmniboxInputStateChanged() = 0;

    // Invoked when the omnibox input is in progress.
    virtual void OnOmniboxInputInProgress(bool in_progress) = 0;

    // Called to indicate that the omnibox focus state changed with the given
    // |reason|.
    virtual void OnOmniboxFocusChanged(OmniboxFocusState state,
                                       OmniboxFocusChangeReason reason) = 0;

    // Invoked when the omnibox popup visibility changes.
    virtual void OnOmniboxPopupVisibilityChanged(bool popup_is_open) = 0;
  };

  ~OmniboxTabHelper() override;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Calls forwarded from omnibox. See OmniboxClient for details.
  void OnInputStateChanged();
  void OnInputInProgress(bool in_progress);
  void OnFocusChanged(OmniboxFocusState state, OmniboxFocusChangeReason reason);
  void OnPopupVisibilityChanged(bool popup_is_open);

  // Returns true if the current page has the paywall signal in the Annotated
  // Page Content. Returns false if the page does not have the paywall signal.
  // Returns std::nullopt if the page content wasn't yet extracted and therefore
  // the signal could not be calculated.
  std::optional<bool> IsPagePaywalled();

 private:
  OmniboxTabHelper(content::WebContents* contents, Profile* profile);
  friend class content::WebContentsUserData<OmniboxTabHelper>;
  WEB_CONTENTS_USER_DATA_KEY_DECL();

  // page_content_annotations::PageContentExtractionService::Observer
  void OnPageContentExtracted(
      content::Page& page,
      const optimization_guide::proto::AnnotatedPageContent& page_content)
      override;

  // content::WebContentsObserver
  void PrimaryPageChanged(content::Page& page) override;

  // Whether the current page has a paywall signal in the Annotated Page
  // Content. std::nullopt if the page content wasn't yet extracted and
  // therefore the signal could not be calculated.
  std::optional<bool> page_has_apc_paywall_signal_;

  // Observer to observer Annotated Page Content updates. Updates are fire on
  // every page, not only the current tab. The page content is generated a few
  // seconds after page load, once the page has stabilized.
  base::ScopedObservation<
      page_content_annotations::PageContentExtractionService,
      page_content_annotations::PageContentExtractionService::Observer>
      page_content_service_observation_{this};

  base::ObserverList<Observer> observers_;
};

#endif  // CHROME_BROWSER_UI_OMNIBOX_OMNIBOX_TAB_HELPER_H_
