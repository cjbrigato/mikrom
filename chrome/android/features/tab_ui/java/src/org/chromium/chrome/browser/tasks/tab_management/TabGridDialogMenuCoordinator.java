// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import android.app.Activity;
import android.content.Context;
import android.view.View;

import androidx.annotation.VisibleForTesting;

import org.chromium.base.Token;
import org.chromium.base.supplier.Supplier;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.tab_ui.R;
import org.chromium.components.browser_ui.widget.BrowserUiListMenuUtils;
import org.chromium.components.collaboration.CollaborationService;
import org.chromium.components.data_sharing.member_role.MemberRole;
import org.chromium.components.tab_group_sync.TabGroupSyncService;
import org.chromium.ui.listmenu.ListMenuItemProperties;
import org.chromium.ui.modelutil.MVCListAdapter.ModelList;

/**
 * A coordinator for the menu in TabGridDialog toolbar. It is responsible for creating a list of
 * menu items, setting up the menu and displaying the menu.
 */
@NullMarked
public class TabGridDialogMenuCoordinator extends TabGroupOverflowMenuCoordinator {
    private final Supplier<Token> mTabGroupIdSupplier;

    /**
     * @param onItemClicked A callback for listening to clicks.
     * @param tabModelSupplier The supplier of the tab model.
     * @param tabGroupIdSupplier The tab group ID supplier for the tab group being acted on.
     * @param tabGroupSyncService Used to checking if a group is shared or synced.
     * @param collaborationService Used for checking the user is the owner of a group.
     * @param context The {@link Context} that the coordinator resides in.
     */
    public TabGridDialogMenuCoordinator(
            OnItemClickedCallback<Token> onItemClicked,
            Supplier<TabModel> tabModelSupplier,
            Supplier<Token> tabGroupIdSupplier,
            @Nullable TabGroupSyncService tabGroupSyncService,
            CollaborationService collaborationService,
            Context context) {
        super(
                R.layout.tab_switcher_action_menu_layout,
                onItemClicked,
                tabModelSupplier,
                tabGroupSyncService,
                collaborationService,
                context);
        mTabGroupIdSupplier = tabGroupIdSupplier;
    }

    /**
     * Creates a {@link View.OnClickListener} that creates the menu and shows it when clicked.
     *
     * @return The on click listener.
     */
    public View.OnClickListener getOnClickListener() {
        return view ->
                createAndShowMenu(view, mTabGroupIdSupplier.get(), (Activity) view.getContext());
    }

    @VisibleForTesting
    @Override
    public void buildMenuActionItems(ModelList itemList, Token tabGroupId) {
        boolean isIncognito = mTabModelSupplier.get().isIncognitoBranded();
        @Nullable String collaborationId = getCollaborationIdOrNull(tabGroupId);
        boolean hasCollaborationData =
                TabShareUtils.isCollaborationIdValid(collaborationId)
                        && mCollaborationService.getServiceStatus().isAllowedToJoin();
        itemList.add(
                BrowserUiListMenuUtils.buildMenuListItemWithIncognitoBranding(
                        R.string.menu_select_tabs,
                        R.id.select_tabs,
                        R.drawable.ic_select_check_box_24dp,
                        R.color.default_icon_color_light_tint_list,
                        R.style.TextAppearance_TextLarge_Primary_Baseline_Light,
                        isIncognito,
                        /* enabled= */ true));
        itemList.add(
                BrowserUiListMenuUtils.buildMenuListItemWithIncognitoBranding(
                        R.string.tab_grid_dialog_toolbar_edit_group_name,
                        R.id.edit_group_name,
                        R.drawable.material_ic_edit_24dp,
                        R.color.default_icon_color_light_tint_list,
                        R.style.TextAppearance_TextLarge_Primary_Baseline_Light,
                        isIncognito,
                        /* enabled= */ true));
        itemList.add(
                BrowserUiListMenuUtils.buildMenuListItemWithIncognitoBranding(
                        R.string.tab_grid_dialog_toolbar_edit_group_color,
                        R.id.edit_group_color,
                        R.drawable.ic_colorize_24dp,
                        R.color.default_icon_color_light_tint_list,
                        R.style.TextAppearance_TextLarge_Primary_Baseline_Light,
                        isIncognito,
                        /* enabled= */ true));
        itemList.add(
                BrowserUiListMenuUtils.buildMenuListItemWithIncognitoBranding(
                        R.string.tab_grid_dialog_toolbar_close_group,
                        R.id.close_tab_group,
                        R.drawable.ic_tab_close_24dp,
                        R.color.default_icon_color_light_tint_list,
                        R.style.TextAppearance_TextLarge_Primary_Baseline_Light,
                        isIncognito,
                        /* enabled= */ true));
        if (mTabGroupSyncService != null && !isIncognito && !hasCollaborationData) {
            itemList.add(
                    BrowserUiListMenuUtils.buildMenuListItemWithIncognitoBranding(
                            R.string.tab_grid_dialog_toolbar_delete_group,
                            R.id.delete_tab_group,
                            R.drawable.material_ic_delete_24dp,
                            R.color.default_icon_color_light_tint_list,
                            R.style.TextAppearance_TextLarge_Primary_Baseline_Light,
                            isIncognito,
                            /* enabled= */ true));
        }
    }

    @VisibleForTesting
    @Override
    public void buildCollaborationMenuItems(ModelList itemList, @MemberRole int memberRole) {
        if (memberRole != MemberRole.UNKNOWN) {
            // Insert these items above the close group menu item.
            int insertionIndex = getMenuItemIndex(itemList, R.id.close_tab_group);
            itemList.add(
                    insertionIndex++,
                    BrowserUiListMenuUtils.buildMenuListItemWithIncognitoBranding(
                            R.string.tab_grid_dialog_toolbar_manage_sharing,
                            R.id.manage_sharing,
                            R.drawable.ic_group_24dp,
                            R.color.default_icon_color_light_tint_list,
                            R.style.TextAppearance_TextLarge_Primary_Baseline_Light,
                            /* isIncognito= */ false,
                            /* enabled= */ true));
            itemList.add(
                    insertionIndex++,
                    BrowserUiListMenuUtils.buildMenuListItemWithIncognitoBranding(
                            R.string.tab_grid_dialog_toolbar_recent_activity,
                            R.id.recent_activity,
                            R.drawable.ic_update_24dp,
                            R.color.default_icon_color_light_tint_list,
                            R.style.TextAppearance_TextLarge_Primary_Baseline_Light,
                            /* isIncognito= */ false,
                            /* enabled= */ true));
        }

        if (memberRole == MemberRole.OWNER) {
            itemList.add(
                    BrowserUiListMenuUtils.buildMenuListItemWithIncognitoBranding(
                            R.string.tab_grid_dialog_toolbar_delete_group,
                            R.id.delete_shared_group,
                            R.drawable.material_ic_delete_24dp,
                            R.color.default_icon_color_light_tint_list,
                            R.style.TextAppearance_TextLarge_Primary_Baseline_Light,
                            /* isIncognito= */ false,
                            /* enabled= */ true));
        } else if (memberRole == MemberRole.MEMBER) {
            itemList.add(
                    BrowserUiListMenuUtils.buildMenuListItemWithIncognitoBranding(
                            R.string.tab_grid_dialog_toolbar_leave_group,
                            R.id.leave_group,
                            R.drawable.material_ic_delete_24dp,
                            R.color.default_icon_color_light_tint_list,
                            R.style.TextAppearance_TextLarge_Primary_Baseline_Light,
                            /* isIncognito= */ false,
                            /* enabled= */ true));
        }
    }

    @Override
    protected int getMenuWidth(int anchorViewWidthPx) {
        return getDimensionPixelSize(R.dimen.menu_width);
    }

    private int getMenuItemIndex(ModelList itemList, int menuItemId) {
        for (int i = 0; i < itemList.size(); i++) {
            if (itemList.get(i).model.get(ListMenuItemProperties.MENU_ITEM_ID) == menuItemId) {
                return i;
            }
        }
        return itemList.size();
    }
}
