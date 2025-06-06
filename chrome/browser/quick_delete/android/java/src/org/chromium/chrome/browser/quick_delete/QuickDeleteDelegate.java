// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.quick_delete;

import static org.chromium.build.NullUtil.assumeNonNull;

import org.chromium.build.annotations.NullMarked;
import org.chromium.chrome.browser.browsing_data.TimePeriod;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.signin.services.IdentityServicesProvider;
import org.chromium.chrome.browser.sync.SyncServiceFactory;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.components.signin.identitymanager.ConsentLevel;
import org.chromium.components.signin.identitymanager.IdentityManager;
import org.chromium.components.sync.DataType;
import org.chromium.components.sync.SyncService;

import java.util.List;

/**
 * An interface for providing and handling quick-delete operations, such as the browsing data
 * deletion.
 */
@NullMarked
abstract class QuickDeleteDelegate {
    /** A data-structure to hold the strings for the Browsing history row in the dialog. */
    static class DomainVisitsData {
        final String mLastVisitedDomain;
        final int mDomainsCount;

        /**
         * @param lastVisitedDomain The last visited domain shown inside the browsing history row of
         *                          the dialog.
         * @param domainsCount      The number of synced unique domains shown inside the browsing
         *  history
         *                          row of the dialog.
         */
        DomainVisitsData(String lastVisitedDomain, int domainsCount) {
            mLastVisitedDomain = lastVisitedDomain;
            mDomainsCount = domainsCount;
        }
    }

    /**
     * @param {@link Profile} from which to query the signed-in status.
     *
     * @return A boolean indicating whether the user is signed in or not.
     */
    static boolean isSignedIn(Profile profile) {
        IdentityManager manager = IdentityServicesProvider.get().getIdentityManager(profile);
        return assumeNonNull(manager).hasPrimaryAccount(ConsentLevel.SIGNIN);
    }

    /**
     * @param {@link Profile} from which to query the syncing history status.
     * @return A boolean indicating whether the user is syncing history and history deletions are
     *     propagated.
     */
    static boolean isSyncingHistory(Profile profile) {
        SyncService syncService = SyncServiceFactory.getForProfile(profile);
        return syncService != null
                && syncService.getActiveDataTypes().contains(DataType.HISTORY_DELETE_DIRECTIVES);
    }

    /**
     * Performs the data deletion for the quick delete feature.
     *
     * @param onDeleteFinished A {@link Runnable} to be called once the browsing data has been
     *     cleared.
     * @param timePeriod The {@link TimePeriod} of the browsing data to delete.
     */
    abstract void performQuickDelete(Runnable onDeleteFinished, @TimePeriod int timePeriod);

    /**
     * Show the Quick Delete animation on the tab list.
     *
     * @param onAnimationEnd Runnable that is invoked when the animation is completed.
     * @param tabs The tabs to fade with the animation. These tabs will get closed after the
     *     animation is complete.
     */
    abstract void showQuickDeleteAnimation(Runnable onAnimationEnd, List<Tab> tabs);

    /**
     * @return True if the user has more than one restorable window.
     */
    abstract boolean isInMultiWindowMode();

    /**
     * Attempt to trigger the HaTS survey 5 seconds after the next page load on any {@link
     * TabModelSelector}.
     */
    abstract void triggerHatsSurvey();
}
