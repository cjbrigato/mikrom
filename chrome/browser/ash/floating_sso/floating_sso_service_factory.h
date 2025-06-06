// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_FLOATING_SSO_FLOATING_SSO_SERVICE_FACTORY_H_
#define CHROME_BROWSER_ASH_FLOATING_SSO_FLOATING_SSO_SERVICE_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace ash::floating_sso {
class FloatingSsoService;

class FloatingSsoServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static FloatingSsoService* GetForProfile(Profile* profile);
  static FloatingSsoServiceFactory* GetInstance();

  FloatingSsoServiceFactory(const FloatingSsoServiceFactory&) = delete;
  FloatingSsoServiceFactory& operator=(const FloatingSsoServiceFactory&) =
      delete;

  // Cookie sync functionality should be enabled only for primary profiles, but
  // in some tests we bypass this restriction when it's not easy to mimic a
  // logged in ChromeOS user.
  void AllowNonPrimaryProfileForTests();

 private:
  friend base::NoDestructor<FloatingSsoServiceFactory>;

  FloatingSsoServiceFactory();
  ~FloatingSsoServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;

  bool allow_non_primary_profile_for_tests_ = false;
};

}  // namespace ash::floating_sso

#endif  // CHROME_BROWSER_ASH_FLOATING_SSO_FLOATING_SSO_SERVICE_FACTORY_H_
