// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_PENDING_EXTENSION_MANAGER_FACTORY_H_
#define EXTENSIONS_BROWSER_PENDING_EXTENSION_MANAGER_FACTORY_H_

#include "base/memory/singleton.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

namespace extensions {

class PendingExtensionManager;

// Factory for PendingExtensionManager objects. PendingExtensionManager objects
// are shared between an incognito browser context and its original browser
// context.
class PendingExtensionManagerFactory
    : public BrowserContextKeyedServiceFactory {
 public:
  PendingExtensionManagerFactory(const PendingExtensionManagerFactory&) =
      delete;
  PendingExtensionManagerFactory& operator=(
      const PendingExtensionManagerFactory&) = delete;

  static PendingExtensionManager* GetForBrowserContext(
      content::BrowserContext* context);

  static PendingExtensionManagerFactory* GetInstance();

 private:
  friend struct base::DefaultSingletonTraits<PendingExtensionManagerFactory>;

  PendingExtensionManagerFactory();
  ~PendingExtensionManagerFactory() override;

  // BrowserContextKeyedServiceFactory implementation:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_PENDING_EXTENSION_MANAGER_FACTORY_H_
