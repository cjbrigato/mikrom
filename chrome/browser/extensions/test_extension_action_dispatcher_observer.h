// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_TEST_EXTENSION_ACTION_DISPATCHER_OBSERVER_H_
#define CHROME_BROWSER_EXTENSIONS_TEST_EXTENSION_ACTION_DISPATCHER_OBSERVER_H_

#include <set>

#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "chrome/browser/extensions/extension_action_dispatcher.h"
#include "extensions/buildflags/buildflags.h"
#include "extensions/common/extension_id.h"

static_assert(BUILDFLAG(ENABLE_EXTENSIONS_CORE));

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace extensions {

// A helper class to observe ExtensionActionDispatcher changes.
class TestExtensionActionDispatcherObserver
    : public ExtensionActionDispatcher::Observer {
 public:
  TestExtensionActionDispatcherObserver(content::BrowserContext* context,
                                        const ExtensionId& extension_id);
  TestExtensionActionDispatcherObserver(
      content::BrowserContext* context,
      const ExtensionId& extension_id,
      const std::set<raw_ptr<content::WebContents, SetExperimental>>&
          contents_to_observe);

  TestExtensionActionDispatcherObserver(
      const TestExtensionActionDispatcherObserver&) = delete;
  TestExtensionActionDispatcherObserver& operator=(
      const TestExtensionActionDispatcherObserver&) = delete;

  ~TestExtensionActionDispatcherObserver() override;

  // Waits until the extension action is updated and the update is seen for all
  // web contents in `contents_to_observe_` if `contents_to_observe_` is not
  // empty.
  void Wait();

  // Returns the web contents for which the extension action was updated. Must
  // be called after calling Wait().
  const content::WebContents* last_web_contents() const {
    return last_web_contents_;
  }

 private:
  // ExtensionActionDispatcher::Observer override:
  void OnExtensionActionUpdated(
      ExtensionAction* extension_action,
      content::WebContents* web_contents,
      content::BrowserContext* browser_context) override;

  raw_ptr<content::WebContents> last_web_contents_ = nullptr;
  ExtensionId extension_id_;
  base::RunLoop run_loop_;
  base::ScopedObservation<ExtensionActionDispatcher,
                          ExtensionActionDispatcher::Observer>
      scoped_observation_{this};

  // An optional set of web contents to observe for extension action updates.
  std::set<raw_ptr<content::WebContents, SetExperimental>> contents_to_observe_;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_TEST_EXTENSION_ACTION_DISPATCHER_OBSERVER_H_
