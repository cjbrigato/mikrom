// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/settings/ui_bundled/password/password_details/add_password_mediator.h"

#import <algorithm>

#import "base/check.h"
#import "base/containers/contains.h"
#import "base/containers/flat_set.h"
#import "base/functional/bind.h"
#import "base/memory/raw_ptr.h"
#import "base/strings/sys_string_conversions.h"
#import "base/task/cancelable_task_tracker.h"
#import "base/task/sequenced_task_runner.h"
#import "base/task/thread_pool.h"
#import "components/password_manager/core/browser/features/password_manager_features_util.h"
#import "components/password_manager/core/browser/form_parsing/form_data_parser.h"
#import "components/password_manager/core/browser/generation/password_generator.h"
#import "components/password_manager/core/browser/password_form.h"
#import "components/password_manager/core/browser/password_manager_util.h"
#import "components/password_manager/core/browser/password_requirements_service.h"
#import "components/password_manager/core/browser/password_sync_util.h"
#import "components/password_manager/core/browser/ui/credential_ui_entry.h"
#import "components/signin/public/identity_manager/account_info.h"
#import "ios/chrome/browser/passwords/model/password_check_observer_bridge.h"
#import "ios/chrome/browser/settings/ui_bundled/password/password_details/add_password_details_consumer.h"
#import "ios/chrome/browser/settings/ui_bundled/password/password_details/add_password_mediator_delegate.h"
#import "ios/chrome/browser/settings/ui_bundled/password/password_details/add_password_metrics.h"
#import "ios/chrome/browser/settings/ui_bundled/password/password_details/add_password_view_controller_delegate.h"
#import "ios/chrome/browser/settings/ui_bundled/password/password_manager_ui_features.h"
#import "net/base/apple/url_conversions.h"

using base::SysNSStringToUTF16;
using base::SysNSStringToUTF8;
using base::SysUTF8ToNSString;

namespace {
// Checks for existing credentials with the same url and username.
bool CheckForDuplicates(
    GURL url,
    NSString* username,
    std::vector<password_manager::CredentialUIEntry> credentials) {
  std::string signon_realm = password_manager::GetSignonRealm(
      password_manager_util::StripAuthAndParams(url));
  std::u16string username_value = SysNSStringToUTF16(username);
  auto have_equal_username_and_realm =
      [&signon_realm,
       &username_value](const password_manager::CredentialUIEntry& credential) {
        return signon_realm == credential.GetFirstSignonRealm() &&
               username_value == credential.username;
      };
  if (std::ranges::any_of(credentials, have_equal_username_and_realm)) {
    return true;
  }
  return false;
}
}  // namespace

@interface AddPasswordMediator () <AddPasswordViewControllerDelegate> {
  // Password Check manager.
  raw_ptr<IOSChromePasswordCheckManager> _manager;
  // Pref service.
  raw_ptr<PrefService> _prefService;
  // Sync service.
  raw_ptr<syncer::SyncService> _syncService;
  // Used to create and run validation tasks.
  std::unique_ptr<base::CancelableTaskTracker> _validationTaskTracker;
  raw_ptr<password_manager::PasswordRequirementsService>
      _passwordRequirementsService;
}

// Delegate for this mediator.
@property(nonatomic, weak) id<AddPasswordMediatorDelegate> delegate;

// Task runner on which validation operations happen.
@property(nonatomic, assign) scoped_refptr<base::SequencedTaskRunner>
    sequencedTaskRunner;

// Stores the url entered in the website field.
@property(nonatomic, assign) GURL URL;

@property(nonatomic, copy) NSString* suggestedPassword;

@end

@implementation AddPasswordMediator

- (instancetype)initWithDelegate:(id<AddPasswordMediatorDelegate>)delegate
            passwordCheckManager:(IOSChromePasswordCheckManager*)manager
                     prefService:(PrefService*)prefService
                     syncService:(syncer::SyncService*)syncService
     passwordRequirementsService:
         (password_manager::PasswordRequirementsService*)
             passwordRequirementsService {
  DCHECK(delegate);
  DCHECK(manager);
  DCHECK(prefService);
  DCHECK(syncService);
  self = [super init];
  if (self) {
    _delegate = delegate;
    _manager = manager;
    _prefService = prefService;
    _syncService = syncService;
    _passwordRequirementsService = passwordRequirementsService;
    _sequencedTaskRunner = base::ThreadPool::CreateSequencedTaskRunner(
        {base::MayBlock(), base::TaskPriority::USER_BLOCKING});
    _validationTaskTracker = std::make_unique<base::CancelableTaskTracker>();
  }
  return self;
}

- (void)setConsumer:(id<AddPasswordDetailsConsumer>)consumer {
  if (_consumer == consumer) {
    return;
  }
  _consumer = consumer;
  std::optional<std::string> account =
      password_manager::sync_util::GetAccountForSaving(_prefService,
                                                       _syncService);
  if (account) {
    CHECK(!account->empty());
    [_consumer setAccountSavingPasswords:SysUTF8ToNSString(*account)];
  } else {
    [_consumer setAccountSavingPasswords:nil];
  }
}

- (void)dealloc {
  _validationTaskTracker->TryCancelAll();
  _validationTaskTracker.reset();
}

#pragma mark - AddPasswordViewControllerDelegate

- (void)addPasswordViewController:(AddPasswordViewController*)viewController
            didAddPasswordDetails:(NSString*)username
                         password:(NSString*)password
                             note:(NSString*)note {
  if (_validationTaskTracker->HasTrackedTasks()) {
    // If the task tracker has pending tasks and the "Save" button is pressed,
    // don't do anything.
    return;
  }

  DCHECK([self isURLValid]);

  password_manager::CredentialUIEntry credential;
  std::string signonRealm = password_manager::GetSignonRealm(self.URL);
  credential.username = SysNSStringToUTF16(username);
  credential.password = SysNSStringToUTF16(password);

  if (password_manager::features::
          IsSuggestStrongPasswordInAddPasswordEnabled()) {
    base::UmaHistogramBoolean(
        kPasswordManagerPasswordSettingsiOSSavedPasswordIsGenerated,
        [password isEqualToString:_suggestedPassword]);
  }

  credential.note = SysNSStringToUTF16(note);
  credential.stored_in = {
      password_manager::features_util::IsAccountStorageEnabled(_prefService,
                                                               _syncService)
          ? password_manager::PasswordForm::Store::kAccountStore
          : password_manager::PasswordForm::Store::kProfileStore};

  password_manager::CredentialFacet facet;
  facet.url = self.URL;
  facet.signon_realm = signonRealm;
  credential.facets.push_back(std::move(facet));

  _manager->GetSavedPasswordsPresenter()->AddCredential(credential);
  [self.delegate setUpdatedPassword:credential];
  [self.delegate dismissAddPasswordTableViewController];
}

- (void)checkForDuplicates:(NSString*)username {
  _validationTaskTracker->TryCancelAll();
  if (![self isURLValid]) {
    return;
  }

  __weak __typeof(self) weakSelf = self;
  _validationTaskTracker->PostTaskAndReplyWithResult(
      _sequencedTaskRunner.get(), FROM_HERE,
      base::BindOnce(
          &CheckForDuplicates, self.URL, username,
          _manager->GetSavedPasswordsPresenter()->GetSavedCredentials()),
      base::BindOnce(^(bool duplicateFound) {
        [weakSelf.consumer onDuplicateCheckCompletion:duplicateFound];
      }));
}

- (void)showExistingCredential:(NSString*)username {
  if (![self isURLValid]) {
    return;
  }

  std::string signon_realm = password_manager::GetSignonRealm(
      password_manager_util::StripAuthAndParams(self.URL));
  std::u16string username_value = SysNSStringToUTF16(username);
  for (const auto& credential :
       _manager->GetSavedPasswordsPresenter()->GetSavedCredentials()) {
    if (credential.GetFirstSignonRealm() == signon_realm &&
        credential.username == username_value) {
      [self.delegate showPasswordDetailsControllerWithCredential:credential];
      return;
    }
  }
  NOTREACHED();
}

- (void)didCancelAddPasswordDetails {
  [self.delegate dismissAddPasswordTableViewController];
}

- (void)setWebsiteURL:(NSString*)website {
  self.URL = password_manager_util::ConstructGURLWithScheme(
      SysNSStringToUTF8(website));
}

- (BOOL)isURLValid {
  return self.URL.is_valid() && self.URL.SchemeIsHTTPOrHTTPS();
}

- (BOOL)isTLDMissing {
  std::string hostname = self.URL.host();
  return !base::Contains(hostname, '.');
}

- (BOOL)shouldShowSuggestPasswordItem {
  // Only show the field `suggestPasswordItem` to user who are signed in and
  // syncing password to their Google Account.
  return password_manager::features_util::IsAccountStorageEnabled(_prefService,
                                                                  _syncService);
}

// Requests a generated password and calls the completion block with the result.
- (void)requestGeneratedPasswordWithCompletion:
    (void (^)(NSString* password))completion {
  __weak __typeof(self) weakSelf = self;
  password_manager::FetchPasswordRequirementsSpecCallback callback =
      base::BindOnce(^(autofill::PasswordRequirementsSpec spec) {
        NSString* generatedPassword =
            base::SysUTF16ToNSString(autofill::GeneratePassword(spec));
        weakSelf.suggestedPassword = generatedPassword;
        completion(generatedPassword);
      });
  _passwordRequirementsService->FetchPasswordRequirementsSpec(
      self.URL, std::move(callback));
}

@end
