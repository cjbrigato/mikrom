// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PERFORMANCE_MANAGER_PUBLIC_DECORATORS_PAGE_LIVE_STATE_DECORATOR_H_
#define COMPONENTS_PERFORMANCE_MANAGER_PUBLIC_DECORATORS_PAGE_LIVE_STATE_DECORATOR_H_

#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "components/performance_manager/public/graph/graph.h"
#include "components/performance_manager/public/graph/graph_registered.h"
#include "components/performance_manager/public/graph/node_data_describer.h"
#include "components/performance_manager/public/graph/page_node.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_capability_type.h"

namespace performance_manager {

class PageNode;
class PageLiveStateObserver;

// Used to record some live state information about the PageNode.
// All the functions that take a WebContents* as a parameter should only be
// called from the UI thread, the event will be forwarded to the corresponding
// PageNode on the Performance Manager's sequence.
class PageLiveStateDecorator
    : public GraphOwnedAndRegistered<PageLiveStateDecorator>,
      public NodeDataDescriberDefaultImpl,
      public PageNodeObserver {
 public:
  class Data;

  PageLiveStateDecorator();
  ~PageLiveStateDecorator() override;
  PageLiveStateDecorator(const PageLiveStateDecorator& other) = delete;
  PageLiveStateDecorator& operator=(const PageLiveStateDecorator&) = delete;

  // Starts the given `observer` observing all PageNodes. An observer should not
  // be passed to this function more than once.
  //
  // To observe a specific PageNode, use
  // PageLiveStateDecorator::Data::AddObserver and
  // PageLiveStateDecorator::Data::RemoveObserver.
  static void AddAllPageObserver(PageLiveStateObserver* observer);

  // Stops the given `observer` from observing all PageNodes. Does nothing if
  // this observer was never added.
  //
  // To observe a specific PageNode, use
  // PageLiveStateDecorator::Data::AddObserver and
  // PageLiveStateDecorator::Data::RemoveObserver.
  static void RemoveAllPageObserver(PageLiveStateObserver* observer);

  // Returns whether `observer` is observing all PageNodes.
  static bool HasAllPageObserver(PageLiveStateObserver* observer);

  // Must be called when the capability types used by `contents` change.
  static void OnCapabilityTypesChanged(
      content::WebContents* contents,
      content::WebContentsCapabilityType capability_type,
      bool used);

  // Functions that should be called by a MediaStreamCaptureIndicator::Observer.
  static void OnIsCapturingVideoChanged(content::WebContents* contents,
                                        bool is_capturing_video);
  static void OnIsCapturingAudioChanged(content::WebContents* contents,
                                        bool is_capturing_audio);
  static void OnIsBeingMirroredChanged(content::WebContents* contents,
                                       bool is_being_mirrored);
  static void OnIsCapturingWindowChanged(content::WebContents* contents,
                                         bool is_capturing_window);
  static void OnIsCapturingDisplayChanged(content::WebContents* contents,
                                          bool is_capturing_display);

  static void SetIsDiscarded(content::WebContents* contents, bool is_discarded);

  // Set the auto discardable property. This defaults to true, and can be set
  // to false by the chrome.tabs.autoDiscardable extension API to prevent a
  // tab from being discarded during an intervention. The tab can still be
  // discarded manually by extensions. Technically this is a property of the
  // tab, not the WebContents, so if discarding changes the WebContents for the
  // tab the value is copied to the new WebContents.
  static void SetIsAutoDiscardable(content::WebContents* contents,
                                   bool is_auto_discardable);

  static void SetIsActiveTab(content::WebContents* contents,
                             bool is_active_tab);

  static void SetIsPinnedTab(content::WebContents* contents,
                             bool is_pinned_tab);

  static void SetIsDevToolsOpen(content::WebContents* contents,
                                bool is_dev_tools_open);

  // Convenience functions to look up the given properties from the
  // PageLiveStateDecorator::Data for the given `contents`.
  static bool IsConnectedToUSBDevice(content::WebContents* contents);
  static bool IsConnectedToBluetoothDevice(content::WebContents* contents);
  static bool IsConnectedToHidDevice(content::WebContents* contents);
  static bool IsConnectedToSerialPort(content::WebContents* contents);
  static bool IsCapturingVideo(content::WebContents* contents);
  static bool IsCapturingAudio(content::WebContents* contents);
  static bool IsBeingMirrored(content::WebContents* contents);
  static bool IsCapturingWindow(content::WebContents* contents);
  static bool IsCapturingDisplay(content::WebContents* contents);
  static bool IsDiscarded(content::WebContents* contents);
  static bool IsAutoDiscardable(content::WebContents* contents);
  static bool IsActiveTab(content::WebContents* contents);
  static bool IsPinnedTab(content::WebContents* contents);
  static bool IsDevToolsOpen(content::WebContents* contents);
  static bool UpdatedTitleOrFaviconInBackground(content::WebContents* contents);

 private:
  friend class PageLiveStateDecoratorTest;

  // GraphOwned implementation:
  void OnPassedToGraph(Graph* graph) override;
  void OnTakenFromGraph(Graph* graph) override;

  // NodeDataDescriber implementation:
  base::Value::Dict DescribePageNodeData(const PageNode* node) const override;

  // PageNodeObserver implementation:
  void OnPageNodeAdded(const PageNode* page_node) override;
  void OnBeforePageNodeRemoved(const PageNode* page_node) override;
  void OnTitleUpdated(const PageNode* page_node) override;
  void OnFaviconUpdated(const PageNode* page_node) override;
  void OnAboutToBeDiscarded(const PageNode* page_node,
                            const PageNode* new_page_node) override;

  base::ObserverList<PageLiveStateObserver> all_page_observers_;

  base::WeakPtrFactory<PageLiveStateDecorator> weak_factory_{this};
};

class PageLiveStateDecorator::Data {
 public:
  Data();
  virtual ~Data();
  Data(const Data& other) = delete;
  Data& operator=(const Data&) = delete;

  void AddObserver(PageLiveStateObserver* observer);
  void RemoveObserver(PageLiveStateObserver* observer);

  virtual bool IsConnectedToUSBDevice() const = 0;
  virtual bool IsConnectedToBluetoothDevice() const = 0;
  virtual bool IsConnectedToHidDevice() const = 0;
  virtual bool IsConnectedToSerialPort() const = 0;
  virtual bool IsCapturingVideo() const = 0;
  virtual bool IsCapturingAudio() const = 0;
  virtual bool IsBeingMirrored() const = 0;
  virtual bool IsCapturingWindow() const = 0;
  virtual bool IsCapturingDisplay() const = 0;
  virtual bool IsDiscarded() const = 0;
  virtual bool IsAutoDiscardable() const = 0;
  virtual bool IsActiveTab() const = 0;
  virtual bool IsPinnedTab() const = 0;
  virtual bool IsDevToolsOpen() const = 0;
  virtual bool UpdatedTitleOrFaviconInBackground() const = 0;

  static const Data* FromPageNode(const PageNode* page_node);
  static Data* GetOrCreateForPageNode(const PageNode* page_node);

  virtual void SetIsConnectedToUSBDeviceForTesting(bool value) = 0;
  virtual void SetIsConnectedToBluetoothDeviceForTesting(bool value) = 0;
  virtual void SetIsConnectedToHidDeviceForTesting(bool value) = 0;
  virtual void SetIsConnectedToSerialPortForTesting(bool value) = 0;
  virtual void SetIsCapturingVideoForTesting(bool value) = 0;
  virtual void SetIsCapturingAudioForTesting(bool value) = 0;
  virtual void SetIsBeingMirroredForTesting(bool value) = 0;
  virtual void SetIsCapturingWindowForTesting(bool value) = 0;
  virtual void SetIsCapturingDisplayForTesting(bool value) = 0;
  virtual void SetIsDiscardedForTesting(bool value) = 0;
  virtual void SetIsAutoDiscardableForTesting(bool value) = 0;
  virtual void SetIsActiveTabForTesting(bool value) = 0;
  virtual void SetIsPinnedTabForTesting(bool value) = 0;
  virtual void SetIsDevToolsOpenForTesting(bool value) = 0;
  virtual void SetUpdatedTitleOrFaviconInBackgroundForTesting(bool value) = 0;

 protected:
  base::ObserverList<PageLiveStateObserver> observers_
      GUARDED_BY_CONTEXT(sequence_checker_);

  SEQUENCE_CHECKER(sequence_checker_);
};

class PageLiveStateObserver : public base::CheckedObserver {
 public:
  PageLiveStateObserver();
  ~PageLiveStateObserver() override;
  PageLiveStateObserver(const PageLiveStateObserver& other) = delete;
  PageLiveStateObserver& operator=(const PageLiveStateObserver&) = delete;

  virtual void OnIsConnectedToUSBDeviceChanged(const PageNode* page_node) {}
  virtual void OnIsConnectedToBluetoothDeviceChanged(
      const PageNode* page_node) {}
  virtual void OnIsConnectedToHidDeviceChanged(const PageNode* page_node) {}
  virtual void OnIsConnectedToSerialPortChanged(const PageNode* page_node) {}
  virtual void OnIsCapturingVideoChanged(const PageNode* page_node) {}
  virtual void OnIsCapturingAudioChanged(const PageNode* page_node) {}
  virtual void OnIsBeingMirroredChanged(const PageNode* page_node) {}
  virtual void OnIsCapturingWindowChanged(const PageNode* page_node) {}
  virtual void OnIsCapturingDisplayChanged(const PageNode* page_node) {}
  virtual void OnIsAutoDiscardableChanged(const PageNode* page_node) {}
  virtual void OnIsActiveTabChanged(const PageNode* page_node) {}
  virtual void OnIsPinnedTabChanged(const PageNode* page_node) {}
  virtual void OnIsDevToolsOpenChanged(const PageNode* page_node) {}
  virtual void OnUpdatedTitleOrFaviconInBackgroundChanged(
      const PageNode* page_node) {}
};

}  // namespace performance_manager

#endif  // COMPONENTS_PERFORMANCE_MANAGER_PUBLIC_DECORATORS_PAGE_LIVE_STATE_DECORATOR_H_
