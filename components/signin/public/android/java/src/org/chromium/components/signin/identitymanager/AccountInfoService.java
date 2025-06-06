// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.signin.identitymanager;

import org.chromium.base.Promise;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.components.signin.base.AccountInfo;

/** This interface handles the {@link AccountInfo} fetch on Java side. */
@NullMarked
public interface AccountInfoService {
    /** Observes the changes of {@link AccountInfo}. */
    interface Observer {
        /** Notifies when an {@link AccountInfo} is updated. */
        void onAccountInfoUpdated(@Nullable AccountInfo accountInfo);
    }

    /** Gets the {@link AccountInfo} of the given account email. */
    // TODO(crbug.com/341948846): Replace all calls to this method by calls to
    // IdentityManager.findExtendedAccountInfoByEmailAddress().
    @Deprecated(since = "Use IdentityManager.findExtendedAccountInfoByEmailAddress() instead")
    Promise<@Nullable AccountInfo> getAccountInfoByEmail(String email);

    /** Adds an observer which will be invoked when an {@link AccountInfo} is updated. */
    void addObserver(Observer observer);

    /** Removes an observer which is invoked when an {@link AccountInfo} is updated. */
    void removeObserver(Observer observer);

    /** Releases the resources used by {@link AccountInfoService}. */
    void destroy();
}
