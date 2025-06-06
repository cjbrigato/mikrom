// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tab_group_sync;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.anyInt;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import static org.chromium.components.tab_group_sync.SyncedGroupTestHelper.SYNC_GROUP_ID1;
import static org.chromium.components.tab_group_sync.SyncedGroupTestHelper.SYNC_GROUP_ID2;

import android.util.Pair;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.Spy;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.annotation.Config;

import org.chromium.base.Token;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabGroupModelFilter;
import org.chromium.chrome.test.util.browser.tabmodel.MockTabModel;
import org.chromium.components.tab_group_sync.ClosingSource;
import org.chromium.components.tab_group_sync.LocalTabGroupId;
import org.chromium.components.tab_group_sync.SavedTabGroup;
import org.chromium.components.tab_group_sync.SavedTabGroupTab;
import org.chromium.components.tab_group_sync.TabGroupSyncService;
import org.chromium.url.GURL;

import java.util.List;

/** Unit tests for the {@link TabGroupSyncUtils}. */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class TabGroupSyncUtilsUnitTest {
    private static final int TAB_ID_1 = 1;
    private static final int TAB_ID_2 = 2;
    private static final int TAB_ID_3 = 2;
    private static final int ROOT_ID_1 = 1;
    private static final Token TOKEN_1 = new Token(2, 3);
    private static final Token TOKEN_2 = new Token(5, 8);
    private static final LocalTabGroupId LOCAL_TAB_GROUP_ID_1 = new LocalTabGroupId(TOKEN_1);
    private static final LocalTabGroupId LOCAL_TAB_GROUP_ID_2 = new LocalTabGroupId(TOKEN_2);

    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Spy private TabGroupSyncService mTabGroupSyncService;
    @Mock private Profile mProfile;
    @Mock private TabGroupModelFilter mTabGroupModelFilter;
    private MockTabModel mTabModel;
    private Tab mTab1;
    private Tab mTab2;

    @Before
    public void setUp() {
        mTabModel = spy(new MockTabModel(mProfile, null));
        when(mTabGroupModelFilter.getTabModel()).thenReturn(mTabModel);
        when(mTabModel.isIncognito()).thenReturn(false);

        mTab1 = mTabModel.addTab(TAB_ID_1);
        mTab2 = mTabModel.addTab(TAB_ID_2);
        createTabGroup(List.of(mTab1, mTab2), ROOT_ID_1, TOKEN_1);
    }

    @Test
    public void testUnmapAllTabGroupIdsNotInCurrentFilter() {
        SavedTabGroup group1 = new SavedTabGroup();
        group1.syncId = SYNC_GROUP_ID1;
        SavedTabGroupTab savedTabGroup1Tab1 = new SavedTabGroupTab();
        SavedTabGroupTab savedTabGroup1Tab2 = new SavedTabGroupTab();
        savedTabGroup1Tab1.localId = TAB_ID_1;
        savedTabGroup1Tab2.localId = TAB_ID_2;
        group1.savedTabs = List.of(savedTabGroup1Tab1, savedTabGroup1Tab2);
        group1.localId = LOCAL_TAB_GROUP_ID_1;

        SavedTabGroup group2 = new SavedTabGroup();
        group2.syncId = SYNC_GROUP_ID2;
        SavedTabGroupTab savedTabGroup2Tab1 = new SavedTabGroupTab();
        savedTabGroup2Tab1.localId = TAB_ID_3;
        group2.savedTabs = List.of(savedTabGroup2Tab1);
        group2.localId = LOCAL_TAB_GROUP_ID_2;

        when(mTabGroupSyncService.getAllGroupIds())
                .thenReturn(new String[] {SYNC_GROUP_ID1, SYNC_GROUP_ID2});
        when(mTabGroupSyncService.getGroup(SYNC_GROUP_ID1)).thenReturn(group1);
        when(mTabGroupSyncService.getGroup(SYNC_GROUP_ID2)).thenReturn(group2);
        when(mTabGroupModelFilter.getRootIdFromTabGroupId(TOKEN_2)).thenReturn(Tab.INVALID_TAB_ID);

        TabGroupSyncUtils.unmapLocalIdsNotInTabGroupModelFilter(
                mTabGroupSyncService, mTabGroupModelFilter);

        verify(mTabGroupSyncService, never())
                .removeLocalTabGroupMapping(
                        eq(LOCAL_TAB_GROUP_ID_1),
                        eq(ClosingSource.CLEANED_UP_ON_LAST_INSTANCE_CLOSURE));
        verify(mTabGroupSyncService)
                .removeLocalTabGroupMapping(
                        eq(LOCAL_TAB_GROUP_ID_2),
                        eq(ClosingSource.CLEANED_UP_ON_LAST_INSTANCE_CLOSURE));
    }

    @Test
    public void testUnmapAllTabGroupIdsNotInCurrentFilter_NullLocalId() {
        SavedTabGroup group1 = new SavedTabGroup();
        group1.syncId = SYNC_GROUP_ID1;
        SavedTabGroupTab savedTabGroup1Tab1 = new SavedTabGroupTab();
        SavedTabGroupTab savedTabGroup1Tab2 = new SavedTabGroupTab();
        group1.savedTabs = List.of(savedTabGroup1Tab1, savedTabGroup1Tab2);
        group1.localId = null;

        when(mTabGroupSyncService.getAllGroupIds()).thenReturn(new String[] {SYNC_GROUP_ID1});
        when(mTabGroupSyncService.getGroup(SYNC_GROUP_ID1)).thenReturn(group1);

        TabGroupSyncUtils.unmapLocalIdsNotInTabGroupModelFilter(
                mTabGroupSyncService, mTabGroupModelFilter);

        // Shouldn't crash and never called.
        verify(mTabGroupModelFilter, never()).getRootIdFromTabGroupId(any());
        verify(mTabGroupSyncService, never())
                .removeLocalTabGroupMapping(eq(LOCAL_TAB_GROUP_ID_1), anyInt());
    }

    @Test
    public void testGetFilteredUrl_NewTab() {
        // All types of NTP URLs.
        expectFilteredUrlAndTitle(
                "chrome://newtab", "New tab", "chrome-native://newtab", "New Tab");
        expectFilteredUrlAndTitle(
                "chrome://newtab", "New tab", "chrome-native://newtab/", "New tab");
        expectFilteredUrlAndTitle("chrome://newtab", "New tab", "chrome://newtab", "New tab");
        expectFilteredUrlAndTitle("chrome://newtab", "New tab", "chrome://newtab/", "New tab");
        expectFilteredUrlAndTitle("chrome://newtab", "New tab", "chrome://new-tab-page", "New Tab");
    }

    @Test
    public void testGetFilteredUrl_HttpHttpsChromeFile() {
        // HTTP / HTTPS URLs.
        expectFilteredUrlAndTitle("https://google.com", "Google", "https://google.com", "Google");
        expectFilteredUrlAndTitle("http://google.com", "Google", "http://google.com", "Google");

        // These URLs are not syncable.
        expectFilteredUrlAndTitle("chrome://newtab", "Unsavable tab", "ftp://foo.com", "Foo");
        expectFilteredUrlAndTitle(
                "chrome://newtab", "Unsavable tab", "chrome://flags", "Experiments");
        expectFilteredUrlAndTitle(
                "chrome://newtab", "Unsavable tab", "chrome-untrusted://foo", "Foo");
        expectFilteredUrlAndTitle("chrome://newtab", "Unsavable tab", "www.foo.com", "Foo");
        expectFilteredUrlAndTitle("chrome://newtab", "Unsavable tab", "file://sdcard/foo", "Foo");
    }

    private void expectFilteredUrlAndTitle(
            String filteredUrl, String filteredTitle, String inputUrl, String inputTitle) {
        Assert.assertEquals(
                "Failed expectation for " + inputUrl,
                new Pair<>(new GURL(filteredUrl), filteredTitle),
                TabGroupSyncUtils.getFilteredUrlAndTitle(new GURL(inputUrl), inputTitle));
    }

    private void createTabGroup(List<Tab> tabs, int rootId, Token tabGroupId) {
        for (Tab tab : tabs) {
            tab.setRootId(rootId);
            tab.setTabGroupId(tabGroupId);
        }
        when(mTabGroupModelFilter.getTabsInGroup(eq(tabGroupId))).thenReturn(tabs);
        when(mTabGroupModelFilter.getRootIdFromTabGroupId(eq(tabGroupId))).thenReturn(rootId);
        when(mTabGroupModelFilter.getTabGroupIdFromRootId(eq(rootId))).thenReturn(tabGroupId);
    }
}
