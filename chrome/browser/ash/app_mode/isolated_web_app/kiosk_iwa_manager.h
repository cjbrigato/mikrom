// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_APP_MODE_ISOLATED_WEB_APP_KIOSK_IWA_MANAGER_H_
#define CHROME_BROWSER_ASH_APP_MODE_ISOLATED_WEB_APP_KIOSK_IWA_MANAGER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ash/app_mode/isolated_web_app/kiosk_iwa_data.h"
#include "chrome/browser/ash/app_mode/kiosk_app_manager_base.h"
#include "chrome/browser/ash/app_mode/kiosk_app_types.h"
#include "chrome/browser/ash/policy/core/device_local_account.h"
#include "chrome/browser/chromeos/app_mode/kiosk_web_app_update_observer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/web_applications/web_app_install_info.h"
#include "components/account_id/account_id.h"

class PrefService;
class PrefRegistrySimple;

namespace ash {

class KioskIwaManager : public KioskAppManagerBase {
 public:
  static const char kIwaKioskDictionaryName[];

  // Registers kiosk app entries in local state.
  static void RegisterPrefs(PrefRegistrySimple* registry);

  // Returns the manager instance or will crash if it not yet initiazlied.
  static KioskIwaManager* Get();
  explicit KioskIwaManager(PrefService& local_state);
  KioskIwaManager(const KioskIwaManager&) = delete;
  KioskIwaManager& operator=(const KioskIwaManager&) = delete;
  ~KioskIwaManager() override;

  // KioskAppManagerBase overrides:
  KioskAppManagerBase::AppList GetApps() const override;

  // Returns app data associated with `account_id`.
  const KioskIwaData* GetApp(const AccountId& account_id) const;

  // Updates app by title and icon_bitmaps.
  void UpdateApp(const AccountId& account_id,
                 const std::string& title,
                 const GURL& start_url,
                 const web_app::IconBitmaps& icon_bitmaps);

  // Returns a valid account id if an IWA kiosk is configured for auto launch.
  // Returns a nullopt otherwise.
  const std::optional<AccountId>& GetAutoLaunchAccountId() const;

  // Notify this manager that a Kiosk session started with the given `app_id`.
  void OnKioskSessionStarted(const KioskAppId& app_id);

  // Starts observing web app updates from App Service in a Kiosk session.
  void StartObservingAppUpdate(Profile* profile, const AccountId& account_id);

  // Adds fake apps in tests.
  void AddAppForTesting(const policy::DeviceLocalAccount& account);

 private:
  void UpdateAppsFromPolicy() override;

  void Reset();
  void MaybeSetAutoLaunchInfo(const std::string& policy_account_id,
                              const AccountId& kiosk_app_account_id);
  void CancelCryptohomeRemovalsForCurrentApps();

  using KioskIwaDataMap = std::map<std::string, std::unique_ptr<KioskIwaData>>;
  // Clears object's internal state (apps list and autolaunch info) and returns
  // a map of previous app data.
  KioskIwaDataMap GetAppsAndReset();

  // Removes accounts associated with `previous_apps`.
  void RemoveApps(const KioskIwaDataMap& previous_apps) const;

  // Creates app data from `account` and adds it to the list of current apps.
  // Removes the matching app data from `previous_apps` that will be later
  // removed.
  void ProcessDeviceLocalAccount(const policy::DeviceLocalAccount& account,
                                 KioskIwaDataMap& previous_apps);

  const raw_ref<PrefService> local_state_;

  // TODO(crbug.com/377878781): Make common helpers for all kiosk app managers.
  std::vector<std::unique_ptr<KioskIwaData>> isolated_web_apps_;
  std::optional<AccountId> auto_launch_id_;

  // Observes IWA updates. Persists through the whole IWA Kiosk session.
  std::unique_ptr<chromeos::KioskWebAppUpdateObserver> app_update_observer_;

  base::WeakPtrFactory<KioskIwaManager> weak_ptr_factory_{this};
};
}  // namespace ash

#endif  // CHROME_BROWSER_ASH_APP_MODE_ISOLATED_WEB_APP_KIOSK_IWA_MANAGER_H_
