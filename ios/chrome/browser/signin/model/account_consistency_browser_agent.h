// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_SIGNIN_MODEL_ACCOUNT_CONSISTENCY_BROWSER_AGENT_H_
#define IOS_CHROME_BROWSER_SIGNIN_MODEL_ACCOUNT_CONSISTENCY_BROWSER_AGENT_H_

#import "base/memory/raw_ptr.h"
#import "components/signin/ios/browser/manage_accounts_delegate.h"
#import "ios/chrome/browser/shared/model/browser/browser_observer.h"
#import "ios/chrome/browser/shared/model/browser/browser_user_data.h"
#import "ios/chrome/browser/web_state_list/model/web_state_dependency_installation_observer.h"

@protocol ApplicationCommands;
@protocol SettingsCommands;
class Browser;
@class SceneState;
@class SigninCoordinator;
typedef NS_ENUM(NSUInteger, SigninCoordinatorResult);
@protocol SystemIdentity;
@class ManageAccountsDelegateBridge;
@class UIViewController;

// A browser agent that tracks the addition and removal of webstates, registers
// them with the AccountConsistencyService, and handles events triggered from
// them.
class AccountConsistencyBrowserAgent
    : public BrowserUserData<AccountConsistencyBrowserAgent>,
      public DependencyInstaller,
      public ManageAccountsDelegate,
      public BrowserObserver {
 public:
  // Not copyable or moveable
  AccountConsistencyBrowserAgent(const AccountConsistencyBrowserAgent&) =
      delete;
  AccountConsistencyBrowserAgent& operator=(
      const AccountConsistencyBrowserAgent&) = delete;
  ~AccountConsistencyBrowserAgent() override;

  // DependencyInstaller
  void InstallDependency(web::WebState* web_state) override;
  void UninstallDependency(web::WebState* web_state) override;

  // ManageAccountsDelegate
  void OnRestoreGaiaCookies() override;
  void OnManageAccounts(const GURL& url) override;
  void OnAddAccount(const GURL& url) override;
  void OnShowConsistencyPromo(const GURL& url,
                              web::WebState* webState) override;
  void OnGoIncognito(const GURL& url) override;

 private:
  friend class BrowserUserData<AccountConsistencyBrowserAgent>;

  void StopSigninCoordinator(SigninCoordinatorResult result,
                             id<SystemIdentity> identity);

  // `base_view_controller` is the view controller which UI will be presented
  // from.
  AccountConsistencyBrowserAgent(Browser* browser,
                                 UIViewController* base_view_controller);

  // BrowserObserver
  void BrowserDestroyed(Browser* browser) override;

  // Returns whether it makes sense to show the browser's account menu instead
  // of starting an "add account" flow or showing the "manage accounts" screen.
  bool ShouldShowAccountMenu() const;

  // Opens the account menu, offering to switch to a different account (even one
  // that's in a different profile).
  void ShowAccountMenu(const GURL& url);

  UIViewController* base_view_controller_;
  id<ApplicationCommands> application_handler_;
  id<SettingsCommands> settings_handler_;
  SigninCoordinator* add_account_coordinator_;

  // Bridge object to act as the delegate.
  ManageAccountsDelegateBridge* bridge_;
  // Observer to handle webstate registration and deregistration events.
  std::unique_ptr<WebStateDependencyInstallationObserver>
      installation_observer_;
};

#endif  // IOS_CHROME_BROWSER_SIGNIN_MODEL_ACCOUNT_CONSISTENCY_BROWSER_AGENT_H_
