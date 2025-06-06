// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/external_files/model/external_file_remover_impl.h"

#import <utility>

#import "base/functional/bind.h"
#import "base/functional/callback_helpers.h"
#import "base/logging.h"
#import "base/strings/sys_string_conversions.h"
#import "base/task/sequenced_task_runner.h"
#import "base/task/single_thread_task_runner.h"
#import "base/task/thread_pool.h"
#import "base/threading/scoped_blocking_call.h"
#import "components/bookmarks/browser/bookmark_model.h"
#import "components/bookmarks/browser/url_and_title.h"
#import "components/sessions/core/tab_restore_service.h"
#import "ios/chrome/browser/bookmarks/model/bookmark_model_factory.h"
#import "ios/chrome/browser/sessions/model/ios_chrome_tab_restore_service_factory.h"
#import "ios/chrome/browser/shared/model/browser/browser.h"
#import "ios/chrome/browser/shared/model/browser/browser_list.h"
#import "ios/chrome/browser/shared/model/browser/browser_list_factory.h"
#import "ios/chrome/browser/shared/model/profile/profile_ios.h"
#import "ios/chrome/browser/shared/model/url/url_util.h"
#import "ios/chrome/browser/shared/model/web_state_list/web_state_list.h"
#import "ios/web/public/navigation/navigation_item.h"
#import "ios/web/public/navigation/navigation_manager.h"
#import "ios/web/public/thread/web_thread.h"
#import "ios/web/public/web_state.h"

namespace {

// The path relative to the <Application_Home>/Documents/ directory where the
// files received from other applications are saved.
NSString* const kInboxPath = @"Inbox";

// Conversion factor to turn number of days to number of seconds.
const CFTimeInterval kSecondsPerDay = 60 * 60 * 24;

// Empty callback. The closure owned by `closure_runner` will be invoked as
// part of the destructor of base::ScopedClosureRunner (which takes care of
// checking for null closure).
void RunCallback(base::ScopedClosureRunner closure_runner) {}

NSSet* ComputeReferencedExternalFiles(Browser* browser) {
  NSMutableSet* referenced_files = [NSMutableSet set];
  if (!browser) {
    return referenced_files;
  }
  WebStateList* web_state_list = browser->GetWebStateList();
  // Check the currently open tabs for external files.
  for (int index = 0; index < web_state_list->count(); ++index) {
    web::WebState* web_state = web_state_list->GetWebStateAt(index);
    const GURL& last_committed_url = web_state->GetLastCommittedURL();
    if (UrlIsExternalFileReference(last_committed_url)) {
      [referenced_files addObject:base::SysUTF8ToNSString(
                                      last_committed_url.ExtractFileName())];
    }

    // An "unrealized" WebState has no pending load. Checking for realization
    // before accessing the NavigationManager prevents accidental realization
    // of the WebState.
    if (web_state->IsRealized()) {
      web::NavigationItem* pending_item =
          web_state->GetNavigationManager()->GetPendingItem();
      if (pending_item && UrlIsExternalFileReference(pending_item->GetURL())) {
        [referenced_files
            addObject:base::SysUTF8ToNSString(
                          pending_item->GetURL().ExtractFileName())];
      }
    }
  }
  // Do the same for the recently closed tabs.
  sessions::TabRestoreService* restore_service =
      IOSChromeTabRestoreServiceFactory::GetForProfile(browser->GetProfile());
  DCHECK(restore_service);
  for (const auto& entry : restore_service->entries()) {
    sessions::tab_restore::Tab* tab =
        static_cast<sessions::tab_restore::Tab*>(entry.get());
    int navigation_index = tab->current_navigation_index;
    sessions::SerializedNavigationEntry navigation =
        tab->navigations[navigation_index];
    GURL url = navigation.virtual_url();
    if (UrlIsExternalFileReference(url)) {
      NSString* file_name = base::SysUTF8ToNSString(url.ExtractFileName());
      [referenced_files addObject:file_name];
    }
  }
  return referenced_files;
}

// Returns the path in the application sandbox of an external file from the
// URL received for that file.
NSString* GetDefaultInboxDirectoryPath() {
  NSArray* paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,
                                                       NSUserDomainMask, YES);
  if ([paths count] < 1) {
    return nil;
  }

  NSString* documents_directory_path = [paths objectAtIndex:0];
  return [documents_directory_path stringByAppendingPathComponent:kInboxPath];
}

// Removes all the files in the Inbox directory that are not in
// `files_to_keep` and that are older than `age_in_days` days.
// `files_to_keep` may be nil if all files should be removed.
void RemoveFilesWithOptions(NSSet* files_to_keep,
                            NSInteger age_in_days,
                            NSString* inbox_directory) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::WILL_BLOCK);
  NSFileManager* file_manager = [NSFileManager defaultManager];
  NSArray* external_files =
      [file_manager contentsOfDirectoryAtPath:inbox_directory error:nil];
  for (NSString* filename in external_files) {
    NSString* file_path =
        [inbox_directory stringByAppendingPathComponent:filename];
    if ([files_to_keep containsObject:filename]) {
      continue;
    }
    // Checks the age of the file and do not remove files that are too recent.
    // Under normal circumstances, e.g. when file purge is not initiated by
    // user action, leave recently downloaded files around to avoid users
    // using history back or recent tabs to reach an external file that was
    // pre-maturely purged.
    NSError* error = nil;
    NSDictionary* attributesDictionary =
        [file_manager attributesOfItemAtPath:file_path error:&error];
    if (error) {
      DLOG(ERROR) << "Failed to retrieve attributes for " << file_path << ": "
                  << base::SysNSStringToUTF8([error description]);
      continue;
    }
    NSDate* date = [attributesDictionary objectForKey:NSFileCreationDate];
    if (-[date timeIntervalSinceNow] <= (age_in_days * kSecondsPerDay)) {
      continue;
    }
    // Removes the file.
    [file_manager removeItemAtPath:file_path error:&error];
    if (error) {
      DLOG(ERROR) << "Failed to remove file " << file_path << ": "
                  << base::SysNSStringToUTF8([error description]);
      continue;
    }
  }
}

}  // namespace

ExternalFileRemoverImpl::ExternalFileRemoverImpl(
    ProfileIOS* profile,
    sessions::TabRestoreService* tab_restore_service,
    NSString* inbox_directory_path)
    : tab_restore_service_(tab_restore_service),
      profile_(profile),
      inbox_directory_path_(inbox_directory_path
                                ?: GetDefaultInboxDirectoryPath()),
      weak_ptr_factory_(this) {
  DCHECK(tab_restore_service_);
  tab_restore_service_->AddObserver(this);
}

ExternalFileRemoverImpl::~ExternalFileRemoverImpl() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void ExternalFileRemoverImpl::RemoveAfterDelay(base::TimeDelta delay,
                                               base::OnceClosure callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::ScopedClosureRunner closure_runner =
      base::ScopedClosureRunner(std::move(callback));
  bool remove_all_files = delay == base::Seconds(0);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&ExternalFileRemoverImpl::RemoveFiles,
                     weak_ptr_factory_.GetWeakPtr(), remove_all_files,
                     std::move(closure_runner)),
      delay);
}

void ExternalFileRemoverImpl::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (tab_restore_service_) {
    tab_restore_service_->RemoveObserver(this);
    tab_restore_service_ = nullptr;
  }
}

void ExternalFileRemoverImpl::TabRestoreServiceChanged(
    sessions::TabRestoreService* service) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (service->IsLoaded()) {
    return;
  }

  tab_restore_service_->RemoveObserver(this);
  tab_restore_service_ = nullptr;
}

void ExternalFileRemoverImpl::TabRestoreServiceDestroyed(
    sessions::TabRestoreService* service) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  NOTREACHED() << "Should never happen as unregistration happen in Shutdown";
}

void ExternalFileRemoverImpl::RemoveFiles(
    bool all_files,
    base::ScopedClosureRunner closure_runner) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  NSSet* referenced_files = all_files ? GetReferencedExternalFiles() : nil;

  const NSInteger kMinimumAgeInDays = 30;
  NSInteger age_in_days = all_files ? 0 : kMinimumAgeInDays;

  base::ThreadPool::PostTaskAndReply(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&RemoveFilesWithOptions, referenced_files, age_in_days,
                     inbox_directory_path_),
      base::BindOnce(&RunCallback, std::move(closure_runner)));
}

NSSet* ExternalFileRemoverImpl::GetReferencedExternalFiles() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Add files from all Browsers.
  NSMutableSet* referenced_external_files = [NSMutableSet set];
  BrowserList* browser_list = BrowserListFactory::GetForProfile(profile_);
  const BrowserList::BrowserType browser_types =
      profile_->IsOffTheRecord()
          ? BrowserList::BrowserType::kIncognito
          : BrowserList::BrowserType::kRegularAndInactive;
  std::set<Browser*> browsers = browser_list->BrowsersOfType(browser_types);
  for (Browser* browser : browsers) {
    NSSet* files = ComputeReferencedExternalFiles(browser);
    if (files) {
      [referenced_external_files unionSet:files];
    }
  }

  bookmarks::BookmarkModel* bookmark_model =
      ios::BookmarkModelFactory::GetForProfile(profile_);
  // Check if the bookmark model is loaded.
  if (!bookmark_model || !bookmark_model->loaded()) {
    return referenced_external_files;
  }

  // Add files from Bookmarks.
  for (const auto& bookmark : bookmark_model->GetUniqueUrls()) {
    GURL bookmark_url = bookmark.url;
    if (UrlIsExternalFileReference(bookmark_url)) {
      [referenced_external_files
          addObject:base::SysUTF8ToNSString(bookmark_url.ExtractFileName())];
    }
  }
  return referenced_external_files;
}
