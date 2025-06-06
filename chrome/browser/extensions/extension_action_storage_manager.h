// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_ACTION_STORAGE_MANAGER_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_ACTION_STORAGE_MANAGER_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/extensions/extension_action_dispatcher.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"
#include "extensions/common/extension_id.h"

namespace content {
class BrowserContext;
}

namespace extensions {
class StateStore;

// This class manages reading and writing browser action values from storage.
class ExtensionActionStorageManager
    : public ExtensionActionDispatcher::Observer,
      public ExtensionRegistryObserver {
 public:
  explicit ExtensionActionStorageManager(content::BrowserContext* context);

  ExtensionActionStorageManager(const ExtensionActionStorageManager&) = delete;
  ExtensionActionStorageManager& operator=(
      const ExtensionActionStorageManager&) = delete;

  ~ExtensionActionStorageManager() override;

 private:
  // ExtensionActionDispatcher::Observer:
  void OnExtensionActionUpdated(
      ExtensionAction* extension_action,
      content::WebContents* web_contents,
      content::BrowserContext* browser_context) override;
  void OnShuttingDown() override;

  // ExtensionRegistryObserver:
  void OnExtensionLoaded(content::BrowserContext* browser_context,
                         const Extension* extension) override;

  // Reads/Writes the ExtensionAction's default values to/from storage.
  void WriteToStorage(ExtensionAction* extension_action);
  void ReadFromStorage(const ExtensionId& extension_id,
                       std::optional<base::Value> value);

  // Returns the Extensions StateStore for the `browser_context_`.
  // May return NULL.
  StateStore* GetStateStore();

  raw_ptr<content::BrowserContext> browser_context_;

  base::ScopedObservation<ExtensionActionDispatcher,
                          ExtensionActionDispatcher::Observer>
      extension_action_dispatcher_observation_{this};

  base::ScopedObservation<ExtensionRegistry, ExtensionRegistryObserver>
      extension_registry_observation_{this};

  base::WeakPtrFactory<ExtensionActionStorageManager> weak_factory_{this};
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_ACTION_STORAGE_MANAGER_H_
