// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_BTM_BTM_BROWSER_SIGNIN_DETECTOR_H_
#define CHROME_BROWSER_BTM_BTM_BROWSER_SIGNIN_DETECTOR_H_

#include "base/memory/raw_ptr.h"
#include "base/types/pass_key.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/signin/public/identity_manager/identity_manager.h"

struct AccountInfo;
class BtmBrowserSigninDetectorFactory;

namespace content {
class BrowserContext;
class BtmService;
}  // namespace content

// BtmBrowserSigninDetector is a service because it depends on both BtmService
// and IdentityManager. We need to be sure it gets shutdown before them.
//
// If, for example, we made BtmService subclass SupportsUserData and attached
// BtmBrowserSigninDetector to it as Data, we wouldn't be able to express the
// dependency of BtmService on IdentityManager.
class BtmBrowserSigninDetector : public KeyedService,
                                 signin::IdentityManager::Observer {
 public:
  BtmBrowserSigninDetector(base::PassKey<BtmBrowserSigninDetectorFactory>,
                           content::BtmService* dips_service,
                           signin::IdentityManager* identity_manager);
  BtmBrowserSigninDetector(const BtmBrowserSigninDetector&) = delete;
  BtmBrowserSigninDetector& operator=(const BtmBrowserSigninDetector&) = delete;
  ~BtmBrowserSigninDetector() override;

  static BtmBrowserSigninDetector* Get(
      content::BrowserContext* browser_context);

 private:
  // Begin signin::IdentityManager::Observer overrides:
  void OnExtendedAccountInfoUpdated(const AccountInfo& info) override;
  // End signin::IdentityManager::Observer overrides.

  // Begin KeyedService overrides:
  void Shutdown() override;
  // End KeyedService overrides.

  // Processes account |info| and records user activation(s) in the DIPS
  // Database if the account |info| is relevant.
  void RecordUserActivationsIfRelevant(const AccountInfo& info);

  raw_ptr<content::BtmService> dips_service_;
  raw_ptr<signin::IdentityManager> identity_manager_;
  base::ScopedObservation<signin::IdentityManager,
                          signin::IdentityManager::Observer>
      scoped_observation_{this};
};

#endif  // CHROME_BROWSER_BTM_BTM_BROWSER_SIGNIN_DETECTOR_H_
