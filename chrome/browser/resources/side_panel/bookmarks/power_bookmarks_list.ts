// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '/strings.m.js';
import './commerce/shopping_list.js';
import './icons.html.js';
import './power_bookmarks_context_menu.js';
import './power_bookmarks_labels.js';
import './power_bookmark_row.js';
import './power_bookmarks_context_menu.js';
import './power_bookmarks_edit_dialog.js';
import '//bookmarks-side-panel.top-chrome/shared/sp_empty_state.js';
import '//bookmarks-side-panel.top-chrome/shared/sp_footer.js';
import '//bookmarks-side-panel.top-chrome/shared/sp_heading.js';
import '//bookmarks-side-panel.top-chrome/shared/sp_icons.html.js';
import '//bookmarks-side-panel.top-chrome/shared/sp_list_item_badge.js';
import '//bookmarks-side-panel.top-chrome/shared/sp_shared_style.css.js';
import '//resources/cr_elements/cr_action_menu/cr_action_menu.js';
import '//resources/cr_elements/cr_button/cr_button.js';
import '//resources/cr_elements/cr_dialog/cr_dialog.js';
import '//resources/cr_elements/cr_icon_button/cr_icon_button.js';
import '//resources/cr_elements/cr_lazy_render/cr_lazy_render.js';
import '//resources/cr_elements/cr_toast/cr_toast.js';
import '//resources/cr_elements/cr_toolbar/cr_toolbar_search_field.js';
import '//resources/cr_elements/cr_toolbar/cr_toolbar_selection_overlay.js';
import '//resources/cr_elements/icons.html.js';
import '//resources/polymer/v3_0/iron-list/iron-list.js';

import type {SpEmptyStateElement} from '//bookmarks-side-panel.top-chrome/shared/sp_empty_state.js';
import {ColorChangeUpdater} from '//resources/cr_components/color_change_listener/colors_css_updater.js';
import type {PriceTrackingBrowserProxy} from '//resources/cr_components/commerce/price_tracking_browser_proxy.js';
import {PriceTrackingBrowserProxyImpl} from '//resources/cr_components/commerce/price_tracking_browser_proxy.js';
import type {BookmarkProductInfo} from '//resources/cr_components/commerce/shared.mojom-webui.js';
import {getInstance as getAnnouncerInstance} from '//resources/cr_elements/cr_a11y_announcer/cr_a11y_announcer.js';
import type {CrActionMenuElement} from '//resources/cr_elements/cr_action_menu/cr_action_menu.js';
import type {CrDialogElement} from '//resources/cr_elements/cr_dialog/cr_dialog.js';
import type {CrLazyRenderElement} from '//resources/cr_elements/cr_lazy_render/cr_lazy_render.js';
import type {CrToastElement} from '//resources/cr_elements/cr_toast/cr_toast.js';
import type {CrToolbarSearchFieldElement} from '//resources/cr_elements/cr_toolbar/cr_toolbar_search_field.js';
import {FocusOutlineManager} from '//resources/js/focus_outline_manager.js';
import {loadTimeData} from '//resources/js/load_time_data.js';
import {PluralStringProxyImpl} from '//resources/js/plural_string_proxy.js';
import {listenOnce} from '//resources/js/util.js';
import type {IronListElement} from '//resources/polymer/v3_0/iron-list/iron-list.js';
import type {DomRepeatEvent} from '//resources/polymer/v3_0/polymer/polymer_bundled.min.js';
import {afterNextRender, Debouncer, PolymerElement, timeOut} from '//resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {ActionSource, SortOrder, ViewType} from './bookmarks.mojom-webui.js';
import type {BookmarksTreeNode} from './bookmarks.mojom-webui.js';
import type {BookmarksApiProxy} from './bookmarks_api_proxy.js';
import {BookmarksApiProxyImpl} from './bookmarks_api_proxy.js';
import {KeyArrowNavigationService} from './keyboard_arrow_navigation_service.js';
import {BOOKMARK_ROW_LOAD_EVENT} from './power_bookmark_row.js';
import type {PowerBookmarksContextMenuElement} from './power_bookmarks_context_menu.js';
import {PowerBookmarksDragManager} from './power_bookmarks_drag_manager.js';
import type {PowerBookmarksEditDialogElement} from './power_bookmarks_edit_dialog.js';
import {TEMP_FOLDER_ID_PREFIX} from './power_bookmarks_edit_dialog.js';
import type {PowerBookmarksLabelsElement} from './power_bookmarks_labels.js';
import {getTemplate} from './power_bookmarks_list.html.js';
import type {Label} from './power_bookmarks_service.js';
import {editingDisabledByPolicy, PowerBookmarksService} from './power_bookmarks_service.js';
import type {PowerBookmarksDelegate} from './power_bookmarks_service.js';
import {getFolderLabel} from './power_bookmarks_utils.js';

const ADD_FOLDER_ACTION_UMA = 'Bookmarks.FolderAddedFromSidePanel';
const ADD_URL_ACTION_UMA = 'Bookmarks.AddedFromSidePanel';

function getBookmarkName(bookmark: BookmarksTreeNode): string {
  return bookmark.title || bookmark.url || '';
}

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused. This must be kept in sync with
// BookmarksSidePanelSearchCTREvent in tools/metrics/histograms/enums.xml.
export enum SearchAction {
  SHOWN = 0,
  SEARCHED = 1,

  // Must be last.
  COUNT = 2,
}

export interface SortOption {
  sortOrder: SortOrder;
  label: string;
  lowerLabel: string;
}

export interface PowerBookmarksListElement {
  $: {
    bookmarks: HTMLElement,
    contextMenu: PowerBookmarksContextMenuElement,
    deletionToast: CrLazyRenderElement<CrToastElement>,
    powerBookmarksContainer: HTMLElement,
    searchField: CrToolbarSearchFieldElement,
    sortMenu: CrActionMenuElement,
    editDialog: PowerBookmarksEditDialogElement,
    disabledFeatureDialog: CrDialogElement,
    topLevelEmptyState: SpEmptyStateElement,
    folderEmptyState: SpEmptyStateElement,
    heading: HTMLElement,
    footer: HTMLElement,
    labels: PowerBookmarksLabelsElement,
  };
}

interface SectionVisibility {
  search?: boolean;
  labels?: boolean;
  heading?: boolean;
  filterHeadings?: boolean;
  folderEmptyState?: boolean;
  newFolderButton?: boolean;
  bookmarksList?: boolean;
  topLevelEmptyState?: boolean;
  footer?: boolean;
}

export class PowerBookmarksListElement extends PolymerElement implements
    PowerBookmarksDelegate {
  static get is() {
    return 'power-bookmarks-list';
  }

  static get template() {
    return getTemplate();
  }

  static get properties() {
    return {
      displayLists_: {
        type: Array,
        value: () => [],
      },

      compact_: {
        type: Boolean,
        value: () => loadTimeData.getInteger('viewType') === 0,
        observer: 'updateListScrollOffset_',
      },

      contextMenuBookmark_: Object,

      activeFolderPath_: {
        type: Array,
        value: () => [],
      },

      currentUrl_: String,

      imageUrls_: {
        type: Object,
        value: () => {
          return {};
        },
      },

      labels_: {
        type: Array,
        value: () => [],
      },

      activeSortIndex_: {
        type: Number,
        value: () => loadTimeData.getInteger('sortOrder'),
      },

      sortTypes_: {
        type: Array,
        value: () =>
            [{
              sortOrder: SortOrder.kNewest,
              label: loadTimeData.getString('sortNewest'),
              lowerLabel: loadTimeData.getString('sortNewestLower'),
            },
             {
               sortOrder: SortOrder.kOldest,
               label: loadTimeData.getString('sortOldest'),
               lowerLabel: loadTimeData.getString('sortOldestLower'),
             },
             {
               sortOrder: SortOrder.kLastOpened,
               label: loadTimeData.getString('sortLastOpened'),
               lowerLabel: loadTimeData.getString('sortLastOpenedLower'),
             },
             {
               sortOrder: SortOrder.kAlphabetical,
               label: loadTimeData.getString('sortAlphabetically'),
               lowerLabel: loadTimeData.getString('sortAlphabetically'),
             },
             {
               sortOrder: SortOrder.kReverseAlphabetical,
               label: loadTimeData.getString('sortReverseAlphabetically'),
               lowerLabel: loadTimeData.getString('sortReverseAlphabetically'),
             }],
      },

      editing_: {
        type: Boolean,
        value: false,
      },

      selectedBookmarks_: {
        type: Object,
        value: () => {
          return {};
        },
      },

      guestMode_: {
        type: Boolean,
        value: loadTimeData.getBoolean('guestMode'),
        reflectToAttribute: true,
      },

      renamingId_: {
        type: String,
        value: '',
      },

      bookmarksTreeViewEnabled_: {
        type: Boolean,
        value: () => loadTimeData.getBoolean('bookmarksTreeViewEnabled'),
      },

      deletionDescription_: {
        type: String,
        value: '',
      },

      /* If container containing shown bookmarks has scrollbars. */
      hasScrollbars_: {
        type: Boolean,
        value: false,
        reflectToAttribute: true,
      },

      hasLoadedData_: {
        type: Boolean,
        value: false,
      },

      searchQuery_: String,
      shoppingCollectionFolderId_: String,

      trackedProductInfos_: {
        type: Object,
        value: () => {
          return {};
        },
      },

      updatedElementIds_: {
        type: Array,
        value: () => [],
      },

      hasSomeActiveFilter_: {
        type: Boolean,
        value: false,
        computed: 'computeHasSomeActiveFilter_(searchQuery_, labels_.*)',
      },

      hasShownBookmarks_: {
        type: Boolean,
        value: false,
        computed: 'computeHasShownBookmarks_(displayLists_.*)',
      },

      canDrag_: {
        type: Boolean,
        value: true,
        computed:
            'computeCanDrag_(editing_, renamingId_, hasSomeActiveFilter_)',
        observer: 'onCanDragChange_',
      },

      sectionVisibility_: {
        type: Object,
        computed: 'computeSectionVisibility_(hasLoadedData_,' +
            'activeFolderPath_.length, hasShownBookmarks_,' +
            'labels_.length, hasSomeActiveFilter_)',
      },
    };
  }

  static get observers() {
    return [
      'updateDisplayLists_(activeFolderPath_.splices, labels_.*, ' +
          'activeSortIndex_, searchQuery_)',
    ];
  }

  private bookmarksApi_: BookmarksApiProxy =
      BookmarksApiProxyImpl.getInstance();
  private priceTrackingProxy_: PriceTrackingBrowserProxy =
      PriceTrackingBrowserProxyImpl.getInstance();
  private shoppingListenerIds_: number[] = [];
  declare private displayLists_: BookmarksTreeNode[][];
  declare private trackedProductInfos_: {[key: string]: BookmarkProductInfo};
  private availableProductInfos_ = new Map<string, BookmarkProductInfo>();
  private bookmarksService_: PowerBookmarksService;
  private keyArrowNavigationService_: KeyArrowNavigationService;
  private bookmarksDragManager_: PowerBookmarksDragManager;
  private focusOutlineManager_: FocusOutlineManager;
  declare private compact_: boolean;
  declare private activeFolderPath_: BookmarksTreeNode[];
  declare private labels_: Label[];
  declare private imageUrls_: {[key: string]: string};
  declare private activeSortIndex_: number;
  declare private sortTypes_: SortOption[];
  declare private searchQuery_: string|undefined;
  declare private currentUrl_: string|undefined;
  declare private editing_: boolean;
  declare private selectedBookmarks_: {[key: string]: boolean};
  declare private guestMode_: boolean;
  declare private renamingId_: string;
  declare private deletionDescription_: string;
  private shownBookmarksResizeObserver_?: ResizeObserver;
  declare private hasScrollbars_: boolean;
  declare private contextMenuBookmark_: BookmarksTreeNode|undefined;
  declare private hasLoadedData_: boolean;
  declare private canDrag_: boolean;
  declare private hasSomeActiveFilter_: boolean;
  declare private hasShownBookmarks_: boolean;
  declare private sectionVisibility_: SectionVisibility;
  declare private shoppingCollectionFolderId_: string;
  private recordCountMetricsOnNextUpdate_: boolean = false;
  declare private updatedElementIds_: string[];
  declare private bookmarksTreeViewEnabled_: boolean;
  private isBookmarksInTransportModeEnabled: boolean =
      loadTimeData.getBoolean('isBookmarksInTransportModeEnabled');
  private rebuildNavigationElementsDebouncer_: Debouncer|null = null;

  constructor() {
    super();
    ColorChangeUpdater.forDocument().start();

    const bookmarksService = new PowerBookmarksService(this);
    PowerBookmarksService.setInstance(bookmarksService);
    this.bookmarksService_ = PowerBookmarksService.getInstance();

    const keyArrowNavigationService =
        new KeyArrowNavigationService(this, 'power-bookmark-row:not([hidden])');
    KeyArrowNavigationService.setInstance(keyArrowNavigationService);
    this.keyArrowNavigationService_ = KeyArrowNavigationService.getInstance();

    this.bookmarksDragManager_ = new PowerBookmarksDragManager(this);
  }

  override connectedCallback() {
    super.connectedCallback();
    this.setAttribute('role', 'application');
    listenOnce(this.$.powerBookmarksContainer, 'dom-change', () => {
      setTimeout(() => this.bookmarksApi_.showUi(), 0);
    });
    this.focusOutlineManager_ = FocusOutlineManager.forDocument(document);
    this.bookmarksService_.startListening();
    this.priceTrackingProxy_.getAllPriceTrackedBookmarkProductInfo().then(
        res => {
          res.productInfos.forEach(
              product => this.set(
                  `trackedProductInfos_.${product.bookmarkId.toString()}`,
                  product));
        });
    this.priceTrackingProxy_.getAllShoppingBookmarkProductInfo().then(res => {
      res.productInfos.forEach(
          product => this.setAvailableProductInfo_(product));
    });
    this.updateShoppingCollectionFolderId_();
    const callbackRouter = this.priceTrackingProxy_.getCallbackRouter();
    this.shoppingListenerIds_.push(
        callbackRouter.priceTrackedForBookmark.addListener(
            (product: BookmarkProductInfo) =>
                this.onBookmarkPriceTracked_(product)),
        callbackRouter.priceUntrackedForBookmark.addListener(
            (product: BookmarkProductInfo) =>
                this.onBookmarkPriceUntracked_(product)),
    );

    this.shownBookmarksResizeObserver_ =
        new ResizeObserver(this.onShownBookmarksResize_.bind(this));
    this.shownBookmarksResizeObserver_.observe(this.$.bookmarks);

    this.updateListScrollOffset_();

    this.bookmarksDragManager_.startObserving();
    this.recordMetricsOnConnected_();
    this.keyArrowNavigationService_.startListening();

    this.addEventListener(BOOKMARK_ROW_LOAD_EVENT, () => {
      this.rebuildNavigationElementsDebouncer_ = Debouncer.debounce(
          this.rebuildNavigationElementsDebouncer_, timeOut.after(1), () => {
            this.keyArrowNavigationService_.rebuildNavigationElements();
          });
    });
  }

  override disconnectedCallback() {
    this.bookmarksService_.stopListening();
    this.shoppingListenerIds_.forEach(
        id => this.priceTrackingProxy_.getCallbackRouter().removeListener(id));

    this.shownBookmarksResizeObserver_!.disconnect();
    this.shownBookmarksResizeObserver_ = undefined;

    this.bookmarksDragManager_.stopObserving();
    this.keyArrowNavigationService_.stopListening();
  }

  setCurrentUrl(url: string) {
    this.currentUrl_ = url;
  }

  getCurrentUrlForTesting(): string|undefined {
    return this.currentUrl_;
  }

  setImageUrl(bookmark: BookmarksTreeNode, url: string) {
    this.set(`imageUrls_.${bookmark.id.toString()}`, url);
    this.imageUrls_ = structuredClone(this.imageUrls_);
  }

  onBookmarksLoaded() {
    this.updateDisplayLists_();
    this.hasLoadedData_ = true;
  }

  onBookmarkChanged(id: string) {
    const bookmark = this.bookmarksService_.findBookmarkWithId(id)!;
     this.updatedElementIds_ = [bookmark.id];
    if (this.bookmarkShouldShow_(bookmark) ||
        this.bookmarkIsShowing_(bookmark)) {
      this.updateDisplayLists_();
    }
    this.notifyPathIfVisible_(id, 'title');
    this.notifyPathIfVisible_(id, 'url');
    this.updateShoppingData_();
  }

  onBookmarkAdded(bookmark: BookmarksTreeNode, parent: BookmarksTreeNode) {
    if (this.bookmarkShouldShow_(bookmark)) {
      this.updateShoppingCollectionFolderId_();

      const scrollTop = this.$.bookmarks.scrollTop;
      this.updateDisplayLists_();
      if (bookmark.url) {
        getAnnouncerInstance().announce(loadTimeData.getStringF(
            'bookmarkCreated', getBookmarkName(bookmark)));
      } else {
        getAnnouncerInstance().announce(loadTimeData.getStringF(
            'bookmarkFolderCreated', getBookmarkName(bookmark)));
      }
      for (let i = 0; i < this.displayLists_.length; i++) {
        const indexInList = this.displayLists_[i].indexOf(bookmark);
        if (indexInList > -1) {
          const listElement = this.getDisplayListElement_(i);
          if (listElement &&
              (indexInList < listElement.firstVisibleIndex ||
               indexInList > listElement.lastVisibleIndex)) {
            listElement.scrollToIndex(indexInList);
          } else {
            afterNextRender(this, () => {
              this.$.bookmarks.scrollTop = scrollTop;
            });
          }
          break;
        }
      }
    }
    this.updatedElementIds_ = [bookmark.id];
    this.updateShoppingData_();
    this.notifyPathIfVisible_(parent.id, 'children');
    this.keyArrowNavigationService_.rebuildNavigationElements();
  }

  onBookmarkMoved(
      bookmark: BookmarksTreeNode, oldParent: BookmarksTreeNode,
      newParent: BookmarksTreeNode) {
    const shouldShow = this.bookmarkShouldShow_(bookmark);
    const isShowing = this.bookmarkIsShowing_(bookmark);
    if (oldParent === newParent && shouldShow) {
      getAnnouncerInstance().announce(loadTimeData.getStringF(
          'bookmarkReordered', getBookmarkName(bookmark)));
    } else if (
        (shouldShow !== isShowing) ||
        (shouldShow && this.hasSomeActiveFilter_)) {
      const scrollTop = this.$.bookmarks.scrollTop;
      this.updateDisplayLists_();
      getAnnouncerInstance().announce(loadTimeData.getStringF(
          'bookmarkMoved', getBookmarkName(bookmark),
          getBookmarkName(newParent)));
      afterNextRender(this, () => {
        this.$.bookmarks.scrollTop = scrollTop;
      });
    }
    this.updatedElementIds_ = [newParent.id, oldParent.id];
    // If the new parent folder is visible, notify to ensure its displayed
    // child count is updated.
    this.notifyPathIfVisible_(newParent.id, 'children');
    // If compact and tree view is active, we must resize open folders
    if (this.bookmarksTreeViewEnabled_ && this.compact_) {
      this.notifyBookmarksListResize_();
    }
    this.keyArrowNavigationService_.rebuildNavigationElements();
  }

  onBookmarkRemoved(bookmark: BookmarksTreeNode) {
    const scrollTop = this.$.bookmarks.scrollTop;
    const isShown = this.bookmarkIsShowing_(bookmark);
    if (isShown) {
      this.removeNodeFromDisplayLists_(bookmark.id);
      getAnnouncerInstance().announce(loadTimeData.getStringF(
          'bookmarkDeleted', getBookmarkName(bookmark)));
      afterNextRender(this, () => {
        this.$.bookmarks.scrollTop = scrollTop;
      });
    }

    if (this.shoppingCollectionFolderId_ === bookmark.id) {
      this.shoppingCollectionFolderId_ = '';
    }
    this.updatedElementIds_ = [bookmark.parentId];
    this.set(`trackedProductInfos_.${bookmark.id}`, null);
    this.availableProductInfos_.delete(bookmark.id);

    // If the parent folder is visible, notify to ensure its displayed
    // child count is updated.
    this.notifyPathIfVisible_(bookmark.parentId, 'children');
    afterNextRender(this, () => {
      this.keyArrowNavigationService_.rebuildNavigationElements();
    });
  }

  getTrackedProductInfos(): {[key: string]: BookmarkProductInfo} {
    return this.trackedProductInfos_;
  }

  getAvailableProductInfos(): Map<string, BookmarkProductInfo> {
    return this.availableProductInfos_;
  }

  getProductImageUrl(bookmark: BookmarksTreeNode): string {
    const bookmarkProductInfo = this.availableProductInfos_.get(bookmark.id);
    if (bookmarkProductInfo) {
      return bookmarkProductInfo.info.imageUrl.url;
    } else {
      return '';
    }
  }

  /** PowerBookmarksDragDelegate */
  getFallbackBookmark(): BookmarksTreeNode {
    // Returning other bookmarks folder in tree view allow moving bookmarks to
    // the root folder
    if (this.bookmarksTreeViewEnabled_ && this.compact_) {
      return this.bookmarksService_.findBookmarkWithId(
          loadTimeData.getString('otherBookmarksId'))!;
    }

    return this.getParentFolder_();
  }

  /** PowerBookmarksDragDelegate */
  getFallbackDropTargetElement(): HTMLElement {
    return this;
  }

  /** PowerBookmarksDragDelegate */
  onFinishDrop(dropTarget: BookmarksTreeNode): void {
    this.focusBookmark_(dropTarget.id);

    // Show the focus state immediately after dropping a bookmark to indicate
    // where the bookmark was moved to, and remove the state immediately after
    // the next mouse event.
    this.focusOutlineManager_.visible = true;
    document.addEventListener('mousedown', () => {
      this.focusOutlineManager_.visible = false;
    }, {once: true});
    this.keyArrowNavigationService_.rebuildNavigationElements();
  }

  clickBookmarkRowForTests(bookmark: BookmarksTreeNode) {
    const event = new CustomEvent('row-clicked', {
      bubbles: true,
      composed: true,
      detail: {
        bookmark: bookmark,
        event: new MouseEvent('row-clicked'),
      },
    });
    this.onRowClicked_(event);
  }

  setRenamingIdForTests(id: string) {
    const event = new CustomEvent('rename', {
      bubbles: true,
      composed: true,
      detail: {
        id: id,
      },
    });
    this.setRenamingId_(event);
  }

  /**
   * Returns the KeyboardNavigationService instance for testing.
   */
  getKeyboardNavigationServiceforTesting() {
    return this.keyArrowNavigationService_;
  }

  private notifyPathIfVisible_(id: string, key: string) {
    for (let i = 0; i < this.displayLists_.length; i++) {
      const listIndex = this.displayLists_[i].findIndex(b => b.id === id);
      if (listIndex > -1) {
        this.notifyPath(`displayLists_.${i}.${listIndex}.${key}`);
        return;
      }
    }
  }

  private computeCanDrag_(): boolean {
    return !this.editing_ && !this.renamingId_ && !this.hasSomeActiveFilter_;
  }

  private focusBookmark_(id: string) {
    const bookmarkElement =
        this.shadowRoot!.querySelector<HTMLElement>(`#bookmark-${id}`);
    if (bookmarkElement) {
      bookmarkElement.focus();
    }
  }

  private onBookmarkPriceTracked_(product: BookmarkProductInfo) {
    this.set(`trackedProductInfos_.${product.bookmarkId.toString()}`, product);
  }

  private onBookmarkPriceUntracked_(product: BookmarkProductInfo) {
    this.set(`trackedProductInfos_.${product.bookmarkId.toString()}`, null);
  }

  private bookmarkIsShowing_(bookmark: BookmarksTreeNode): boolean {
    return this.displayLists_.some(
        list => list.some(item => item.id === bookmark.id));
  }

  private removeNodeFromDisplayLists_(nodeId: string) {
    for (let listIndex = 0; listIndex < this.displayLists_.length;
         listIndex++) {
      const itemIndex =
          this.displayLists_[listIndex].findIndex(b => b.id === nodeId);
      if (itemIndex > -1) {
        this.splice(`displayLists_.${listIndex}`, itemIndex, 1);
      }
    }
  }

  /**
   * Returns true if the given node is either the current active folder or a
   * root folder that isn't shown itself while the all bookmarks list is shown.
   */
  private visibleParent_(parent: BookmarksTreeNode): boolean {
    const activeFolder = this.getActiveFolder_();
    return (!activeFolder &&
            parent.parentId === loadTimeData.getString('rootBookmarkId') &&
            !this.bookmarkIsShowing_(parent)) ||
        parent === activeFolder;
  }

  private bookmarkShouldShow_(bookmark: BookmarksTreeNode): boolean {
    if (this.hasSomeActiveFilter_) {
      return this.bookmarksService_.bookmarkMatchesSearchQueryAndLabels(
          bookmark, this.labels_, this.searchQuery_);
    }
    return this.visibleParent_(
        this.bookmarksService_.findBookmarkWithId(bookmark.parentId)!);
  }

  private getActiveFolder_(): BookmarksTreeNode|undefined {
    if (this.activeFolderPath_.length) {
      return this.activeFolderPath_[this.activeFolderPath_.length - 1];
    }
    return undefined;
  }

  private getBackButtonLabel_(): string {
    const activeFolder = this.getActiveFolder_();
    const parentFolder = this.bookmarksService_.findBookmarkWithId(
        activeFolder ? activeFolder.parentId : undefined);
    return loadTimeData.getStringF(
        'backButtonLabel', getFolderLabel(parentFolder));
  }

  private getBookmarksListRole_(): string {
    return this.editing_ ? 'listbox' : 'list';
  }

  private getViewButtonIcon_() {
    return this.compact_ ? 'bookmarks:compact-view' : 'bookmarks:visual-view';
  }

  private getViewButtonTooltip_() {
    return this.compact_ ? loadTimeData.getString('compactView') :
                           loadTimeData.getString('visualView');
  }

  private updateShoppingCollectionFolderId_(): void {
    this.priceTrackingProxy_.getShoppingCollectionBookmarkFolderId().then(
        res => {
          this.shoppingCollectionFolderId_ = res.collectionId.toString();
        });
  }

  private getActiveFolderLabel_(): string {
    if (this.bookmarksTreeViewEnabled_ && this.compact_) {
      return loadTimeData.getString('allBookmarks');
    }
    return getFolderLabel(this.getActiveFolder_());
  }

  private getSortLabel_(): string {
    return this.sortTypes_[this.activeSortIndex_].label;
  }

  private updateShoppingData_() {
    this.availableProductInfos_.clear();
    this.priceTrackingProxy_.getAllShoppingBookmarkProductInfo().then(res => {
      res.productInfos.forEach(
          product => this.setAvailableProductInfo_(product));
    });
  }

  private setAvailableProductInfo_(productInfo: BookmarkProductInfo) {
    const bookmarkId = productInfo.bookmarkId.toString();
    this.availableProductInfos_.set(bookmarkId, productInfo);
    if (productInfo.info.imageUrl.url === '') {
      return;
    }
    const bookmark = this.bookmarksService_.findBookmarkWithId(bookmarkId)!;
    if (!bookmark) {
      return;
    }
    this.setImageUrl(bookmark, productInfo.info.imageUrl.url);
  }

  /**
   * Update the lists of bookmarks and folders displayed to the user.
   */
  private updateDisplayLists_() {
    const activeFolder = this.bookmarksTreeViewEnabled_ && this.compact_ ?
        undefined :
        this.getActiveFolder_();
    const primaryList = this.bookmarksService_.filterBookmarks(
        activeFolder, this.activeSortIndex_, this.searchQuery_, this.labels_);
    if (this.hasSomeActiveFilter_ && !!activeFolder) {
      const secondaryList = this.bookmarksService_.filterBookmarks(
          undefined, this.activeSortIndex_, this.searchQuery_, this.labels_,
          activeFolder);
      this.displayLists_ = [primaryList, secondaryList];
    } else {
      this.displayLists_ = [primaryList];
    }
    this.displayLists_.forEach(
        list => this.bookmarksService_.refreshDataForBookmarks(list));
    this.updateListScrollOffset_();

    if (this.recordCountMetricsOnNextUpdate_) {
      this.recordBookmarkCountMetrics_();
      this.recordCountMetricsOnNextUpdate_ = false;
    }

    // After the lists are updated and all children updates are complete,
    // notify iron-list to resize.
    afterNextRender(this, () => {
      const children =
          [...this.shadowRoot!.querySelectorAll('power-bookmark-row')];
      if (children.length > 0) {
        Promise.all(children.map(el => el.updateComplete))
            .then(() => this.notifyBookmarksListResize_());
      }
    });
  }

  private updateListScrollOffset_() {
    // Set scrollOffset so the iron-list scrolling accounts for the space the
    // other scrolling UI elements take.
    afterNextRender(this, () => {
      const primaryList = this.getDisplayListElement_(0);
      const secondaryList = this.getDisplayListElement_(1);
      const bookmarksOffsetTop = this.$.bookmarks.offsetTop;
      if (primaryList) {
        primaryList.scrollOffset = primaryList.offsetTop - bookmarksOffsetTop;
      }
      if (secondaryList) {
        secondaryList.scrollOffset =
            secondaryList.offsetTop - bookmarksOffsetTop;
      }
    });
  }

  private onCanDragChange_() {
    if (this.canDrag_) {
      this.bookmarksDragManager_.startObserving();
    } else {
      this.bookmarksDragManager_.stopObserving();
    }
  }

  private recordMetricsOnConnected_() {
    chrome.metricsPrivate.recordEnumerationValue(
        'PowerBookmarks.SidePanel.SortTypeShown',
        this.sortTypes_[this.activeSortIndex_].sortOrder, SortOrder.kCount);
    chrome.metricsPrivate.recordEnumerationValue(
        'PowerBookmarks.SidePanel.ViewTypeShown',
        this.compact_ ? ViewType.kCompact : ViewType.kExpanded,
        ViewType.kCount);
    chrome.metricsPrivate.recordEnumerationValue(
        'PowerBookmarks.SidePanel.Search.CTR', SearchAction.SHOWN,
        SearchAction.COUNT);
    this.recordCountMetricsOnNextUpdate_ = true;
  }

  private recordBookmarkCountMetrics_() {
    const count =
        this.displayLists_.reduce((prev, curr) => prev + curr.length, 0);
    const metricName = `PowerBookmarks.SidePanel${
        this.hasSomeActiveFilter_ ? '.SearchOrFilter' : ''}.BookmarksShown`;
    chrome.metricsPrivate.recordMediumCount(metricName, count);
  }

  private canAddCurrentUrl_(): boolean {
    return this.bookmarksService_.canAddUrl(
        this.currentUrl_, this.getActiveFolder_());
  }

  private getSortMenuItemLabel_(sortType: SortOption): string {
    return loadTimeData.getStringF('sortByType', sortType.label);
  }

  private getSortMenuItemLowerLabel_(sortType: SortOption): string {
    return loadTimeData.getStringF('sortByType', sortType.lowerLabel);
  }

  private sortMenuItemIsSelected_(sortType: SortOption): boolean {
    return this.sortTypes_[this.activeSortIndex_].sortOrder ===
        sortType.sortOrder;
  }

  private onRowToggled_(event: CustomEvent<{
    bookmark: BookmarksTreeNode,
    expanded: boolean,
    event: MouseEvent,
  }>) {
    const bookmark = event.detail.bookmark;
    if (event.detail.expanded) {
      this.activeFolderPath_ = this.bookmarksService_.findPathToId(bookmark.id);
    } else if (bookmark === this.getActiveFolder_()) {
      this.pop('activeFolderPath_');
    }
    this.notifyBookmarksListResize_();
  }
  /**
   * Invoked when the user clicks a power bookmarks row. This will either
   * display children in the case of a folder row, or open the URL in the case
   * of a bookmark row.
   */
  private onRowClicked_(
      event: CustomEvent<{bookmark: BookmarksTreeNode, event: MouseEvent}>) {
    event.preventDefault();
    event.stopPropagation();
    if (!this.editing_) {
      if (event.detail.bookmark.children) {
        this.recordCountMetricsOnNextUpdate_ = true;
        this.push('activeFolderPath_', event.detail.bookmark);
        // Cancel search when changing active folder.
        this.$.searchField.setValue('');
        afterNextRender(this, () => {
          for (let i = 0; i < this.displayLists_.length; i++) {
            if (this.displayLists_[i].length > 0) {
              this.getDisplayListElement_(i)!.focusItem(0);
              break;
            }
          }
        });
      } else {
        this.bookmarksApi_.openBookmark(
            event.detail.bookmark.id, this.activeFolderPath_.length, {
              middleButton: event.detail.event.button === 1,
              altKey: event.detail.event.altKey,
              ctrlKey: event.detail.event.ctrlKey,
              metaKey: event.detail.event.metaKey,
              shiftKey: event.detail.event.shiftKey,
            },
            ActionSource.kBookmark);
      }
    }
    // Workaround for this issue, causing unexpected list scrolling when
    // refocusing the list after changing tabs:
    // https://github.com/PolymerElements/iron-list/issues/270
    if (event.target) {
      (event.target as HTMLElement).blur();
    }
  }

  private onRowSelectedChange_(
      event: CustomEvent<{bookmark: BookmarksTreeNode, checked: boolean}>) {
    event.preventDefault();
    event.stopPropagation();
    const isSelected =
        Object.entries(this.selectedBookmarks_)
            .find(([key, _val]) => key === event.detail.bookmark.id)
            ?.[1] ??
        false;
    if (event.detail.checked && !isSelected) {
      this.set(
          `selectedBookmarks_.${event.detail.bookmark.id.toString()}`, true);
    } else if (!event.detail.checked && isSelected) {
      this.set(
          `selectedBookmarks_.${event.detail.bookmark.id.toString()}`, false);
    }
  }

  private async onBookmarksEdited_(event: CustomEvent<{
    bookmarks: BookmarksTreeNode[],
    name: string|undefined,
    url: string|undefined,
    folderId: string,
    newFolders: BookmarksTreeNode[],
  }>) {
    event.preventDefault();
    event.stopPropagation();
    let parentId = event.detail.folderId;
    for (const folder of event.detail.newFolders) {
      chrome.metricsPrivate.recordUserAction(ADD_FOLDER_ACTION_UMA);
      const result: {newFolderId: string} =
          await this.bookmarksApi_.createFolder(folder.parentId, folder.title);
      folder.children!.forEach(child => child.parentId = result.newFolderId);
      if (folder.id === parentId) {
        parentId = result.newFolderId;
      }
      // Removing folders added in edit menu while editing a bookmark as they
      // are made with TEMP_FOLDER_ID_PREFIX bookmark-id and are again created
      // with correct id with createFolder method above
      const parentFolder =
          this.bookmarksService_.findBookmarkWithId(folder.parentId)!;
      parentFolder.children = parentFolder.children!.filter(
          child => !child.id.startsWith(TEMP_FOLDER_ID_PREFIX));
    }
    this.bookmarksApi_.editBookmarks(
        event.detail.bookmarks.map(bookmark => bookmark.id), event.detail.name,
        event.detail.url, parentId);
    this.selectedBookmarks_ = {};
    this.editing_ = false;
  }

  private setRenamingId_(event: CustomEvent<{id: string}>) {
    this.renamingId_ = event.detail.id;
  }

  private onRename_(
      event: CustomEvent<{bookmark: BookmarksTreeNode, value: string|null}>) {
    const newName = event.detail.value;
    if (newName != null) {
      this.bookmarksApi_.renameBookmark(event.detail.bookmark.id, newName);
    }
    this.renamingId_ = '';
  }

  private getDisplayListElement_(index: number): IronListElement|null {
    return this.shadowRoot!.querySelector<IronListElement>(
        `#shownBookmarksIronList${index}`);
  }

  private notifyBookmarksListResize_() {
    for (let i = 0; i < this.displayLists_.length; i++) {
      if (this.displayLists_[i].length > 0) {
        this.getDisplayListElement_(i)!.notifyResize();
      }
    }
  }

  private getFilterHeading_(index: number) {
    if (index === 0) {
      return loadTimeData.getStringF(
          'primaryFilterHeading', this.getActiveFolderLabel_());
    }
    return loadTimeData.getString('secondaryFilterHeading');
  }

  private getSelectedDescription_() {
    return loadTimeData.getStringF(
        'selectedBookmarkCount', this.getSelectedBookmarksLength_());
  }

  private getSelectedBookmarksList_(): BookmarksTreeNode[] {
    const selectedEntries = Object.entries(this.selectedBookmarks_)
                                .filter(([_id, selected]) => selected);
    const selectedIds = selectedEntries.map(([id, _selected]) => id);
    return selectedIds.map(
        (id) => this.bookmarksService_.findBookmarkWithId(id)!);
  }

  private getSelectedBookmarksLength_(): number {
    return Object.values(this.selectedBookmarks_)
        .filter((selected) => selected)
        .length;
  }

  /**
   * Toggles the given label between active and inactive.
   */
  private onLabelsChanged_() {
    this.labels_ = [...this.$.labels.labels];
  }

  /**
   * Moves the displayed folders up one level when the back button is clicked.
   */
  private onBackClicked_() {
    this.recordCountMetricsOnNextUpdate_ = true;
    this.pop('activeFolderPath_');
  }

  private shouldHideBackButton_(): boolean {
    if (this.compact_ && this.bookmarksTreeViewEnabled_) {
      return true;
    }
    return !this.activeFolderPath_.length;
  }

  private onSearchChanged_(e: CustomEvent<string>) {
    this.recordCountMetricsOnNextUpdate_ = true;
    this.searchQuery_ = e.detail.toLocaleLowerCase();
  }

  private onSearchBlurred_() {
    chrome.metricsPrivate.recordEnumerationValue(
        'PowerBookmarks.SidePanel.Search.CTR', SearchAction.SEARCHED,
        SearchAction.COUNT);
  }

  private onContextMenuShown_(bookmark: BookmarksTreeNode) {
    this.contextMenuBookmark_ = bookmark;
  }

  private onShowContextMenuClicked_(
      event: CustomEvent<{bookmark: BookmarksTreeNode, event: MouseEvent}>) {
    event.preventDefault();
    event.stopPropagation();
    if (!event.detail.bookmark) {
      return;
    }
    const priceTracked =
        !!this.bookmarksService_.getPriceTrackedInfo(event.detail.bookmark);
    const priceTrackingEligible =
        !!this.bookmarksService_.getAvailableProductInfo(event.detail.bookmark);
    const bookmark = event.detail.bookmark;
    const target = event.detail.event.target as HTMLElement;
    if (event.detail.event.button === 0) {
      this.bookmarksApi_.isActiveTabInSplit().then((isSplit: boolean) => {
        this.$.contextMenu.showAt(
            target, [bookmark], priceTracked, priceTrackingEligible, isSplit,
            this.onContextMenuShown_.bind(this, bookmark));
      });
    } else {
      this.bookmarksApi_.isActiveTabInSplit().then((isSplit: boolean) => {
        this.$.contextMenu.showAtPosition(
            event.detail.event, [bookmark], priceTracked, priceTrackingEligible,
            isSplit, this.onContextMenuShown_.bind(this, bookmark));
      });
    }
  }

  private getParentFolder_(): BookmarksTreeNode {
    return this.getActiveFolder_() ||
        this.bookmarksService_.findBookmarkWithId(
            loadTimeData.getString('otherBookmarksId'))!;
  }

  private onShowSortMenuClicked_(event: MouseEvent) {
    event.preventDefault();
    event.stopPropagation();
    this.$.sortMenu.showAt(event.target as HTMLElement);
  }

  private onAddNewFolderClicked_(event: MouseEvent) {
    event.preventDefault();
    event.stopPropagation();
    const newParent = this.getParentFolder_();
    if (editingDisabledByPolicy([newParent])) {
      this.showDisabledFeatureDialog_();
      return;
    }
    chrome.metricsPrivate.recordUserAction(ADD_FOLDER_ACTION_UMA);
    this.bookmarksApi_
        .createFolder(newParent.id, loadTimeData.getString('newFolderTitle'))
        .then((result: {newFolderId: string}) => {
          this.renamingId_ = result.newFolderId;
        });
  }

  private onBulkEditClicked_(event: MouseEvent) {
    event.preventDefault();
    event.stopPropagation();
    this.editing_ = !this.editing_;
    if (!this.editing_) {
      this.selectedBookmarks_ = {};
    }
  }

  private onDeleteClicked_(event: MouseEvent) {
    event.preventDefault();
    event.stopPropagation();
    const selectedBookmarksList = this.getSelectedBookmarksList_();
    if (editingDisabledByPolicy(selectedBookmarksList)) {
      this.showDisabledFeatureDialog_();
      return;
    }
    this.bookmarksApi_
        .deleteBookmarks(selectedBookmarksList.map((bookmark) => bookmark.id))
        .then(() => {
          this.showDeletionToastWithCount_(selectedBookmarksList.length);
          this.selectedBookmarks_ = {};
          this.editing_ = false;
        });
  }

  private onContextMenuEditClicked_(
      event: CustomEvent<{bookmarks: BookmarksTreeNode[]}>) {
    event.preventDefault();
    event.stopPropagation();
    if (editingDisabledByPolicy(event.detail.bookmarks)) {
      this.showDisabledFeatureDialog_();
      return;
    }
    this.showEditDialog_(
        event.detail.bookmarks, event.detail.bookmarks.length > 1);
  }

  private onContextMenuDeleteClicked_(
      event: CustomEvent<{bookmarks: BookmarksTreeNode[]}>) {
    event.preventDefault();
    event.stopPropagation();
    this.showDeletionToastWithCount_(event.detail.bookmarks.length);
    this.selectedBookmarks_ = {};
    this.editing_ = false;
  }

  private onContextMenuClosed_() {
    // This check is needed to avoid the case where the context menu is closed
    // via right-click a new row, and is already re-opened by the time this
    // executes.
    if (!this.$.contextMenu.isOpen()) {
      this.contextMenuBookmark_ = undefined;
    }
  }

  private showDeletionToastWithCount_(deletionCount: number) {
    PluralStringProxyImpl.getInstance()
        .getPluralString('bookmarkDeletionCount', deletionCount)
        .then(pluralString => {
          this.deletionDescription_ = pluralString;
          this.$.deletionToast.get().show();
        });
  }

  private showDisabledFeatureDialog_() {
    this.$.disabledFeatureDialog.showModal();
  }

  private closeDisabledFeatureDialog_() {
    this.$.disabledFeatureDialog.close();
  }

  private onUndoClicked_() {
    this.bookmarksApi_.undo();
    this.$.deletionToast.get().hide();
  }

  private onMoveClicked_(event: MouseEvent) {
    event.preventDefault();
    event.stopPropagation();
    const selectedBookmarksList = this.getSelectedBookmarksList_();
    if (editingDisabledByPolicy(selectedBookmarksList)) {
      this.showDisabledFeatureDialog_();
      return;
    }
    this.showEditDialog_(selectedBookmarksList, true);
  }

  private showEditDialog_(bookmarks: BookmarksTreeNode[], moveOnly: boolean) {
    if (!this.isBookmarksInTransportModeEnabled) {
      this.$.editDialog.showDialog(
          this.activeFolderPath_, this.bookmarksService_.getTopLevelBookmarks(),
          bookmarks, moveOnly);
      return;
    }

    if (moveOnly) {
      this.bookmarksApi_.contextMenuMove(
          bookmarks.map(bookmark => bookmark.id), ActionSource.kBookmark);
    } else {
      this.bookmarksApi_.contextMenuEdit(
          bookmarks.map(bookmark => bookmark.id), ActionSource.kBookmark);
    }
  }

  private onBulkEditMenuClicked_(event: MouseEvent) {
    event.preventDefault();
    event.stopPropagation();
    const target = event.target as HTMLElement;
    this.bookmarksApi_.isActiveTabInSplit().then((isSplit: boolean) => {
      this.$.contextMenu.showAt(
          target, this.getSelectedBookmarksList_(), false, false, isSplit);
    });
  }

  private onSortTypeClicked_(event: DomRepeatEvent<SortOption>) {
    event.preventDefault();
    event.stopPropagation();
    this.$.sortMenu.close();
    this.activeSortIndex_ = event.model.index;
    this.bookmarksApi_.setSortOrder(event.model.item.sortOrder);
    chrome.metricsPrivate.recordEnumerationValue(
        'PowerBookmarks.SidePanel.SortTypeShown', event.model.item.sortOrder,
        SortOrder.kCount);
  }

  private onViewToggleClicked_(event: MouseEvent) {
    event.preventDefault();
    event.stopPropagation();
    this.compact_ = !this.compact_;
    if(this.bookmarksTreeViewEnabled_ && this.compact_){
      // While changing visual view to tree view, displayList_ should get back
      // to allBookmarks list.
      this.updateDisplayLists_();
    }
    this.notifyBookmarksListResize_();
    const viewType = this.compact_ ? ViewType.kCompact : ViewType.kExpanded;
    this.bookmarksApi_.setViewType(viewType);
    chrome.metricsPrivate.recordEnumerationValue(
        'PowerBookmarks.SidePanel.ViewTypeShown', viewType, ViewType.kCount);
  }

  private onAddTabClicked_() {
    const newParent = this.getParentFolder_();
    if (editingDisabledByPolicy([newParent])) {
      this.showDisabledFeatureDialog_();
      return;
    }
    chrome.metricsPrivate.recordUserAction(ADD_URL_ACTION_UMA);
    this.bookmarksApi_.bookmarkCurrentTabInFolder(newParent.id);
  }

  private hideAddTabButton_() {
    return this.editing_ || this.guestMode_;
  }

  private disableBackButton_(): boolean {
    return !this.activeFolderPath_.length || this.editing_;
  }

  private getEmptyTitle_(): string {
    if (this.guestMode_) {
      return loadTimeData.getString('emptyTitleGuest');
    } else if (this.hasSomeActiveFilter_) {
      return loadTimeData.getString('emptyTitleSearch');
    } else {
      return loadTimeData.getString('emptyTitle');
    }
  }

  private getEmptyBody_(): string {
    if (this.guestMode_) {
      return loadTimeData.getString('emptyBodyGuest');
    } else if (this.hasSomeActiveFilter_) {
      return loadTimeData.getString('emptyBodySearch');
    } else {
      return loadTimeData.getString('emptyBody');
    }
  }

  private getEmptyImagePath_(): string {
    return this.hasSomeActiveFilter_ ? '' : './images/bookmarks_empty.svg';
  }

  private getEmptyImagePathDark_(): string {
    return this.hasSomeActiveFilter_ ? '' : './images/bookmarks_empty_dark.svg';
  }

  private computeHasSomeActiveFilter_(): boolean {
    return !!this.searchQuery_ || this.labels_.some(label => label.active);
  }

  private computeHasShownBookmarks_(): boolean {
    return this.displayLists_.some((list) => list.length > 0);
  }

  private computeSectionVisibility_(): SectionVisibility {
    if (this.guestMode_) {
      return {topLevelEmptyState: true};
    }

    if (!this.hasLoadedData_) {
      return {search: true, footer: true};
    }

    const hasActiveFolder = this.activeFolderPath_.length > 0;
    const hasShownBookmarks = this.hasShownBookmarks_;
    const hasSomeActiveFilter = this.hasSomeActiveFilter_;

    return {
      search: true,
      labels: this.labels_.length > 0,
      heading: !hasSomeActiveFilter && (hasActiveFolder || hasShownBookmarks),
      filterHeadings: hasSomeActiveFilter,
      folderEmptyState:
          !hasShownBookmarks && !hasSomeActiveFilter && hasActiveFolder,
      newFolderButton: !hasSomeActiveFilter,
      bookmarksList: hasShownBookmarks,
      topLevelEmptyState:
          !hasShownBookmarks && (hasSomeActiveFilter || !hasActiveFolder),
      footer: !hasSomeActiveFilter,
    };
  }

  private onShownBookmarksResize_() {
    // The iron-lists of `displayLists_` are in a dynamically sized card.
    // Any time the size changes, let iron-list know so that iron-list can
    // properly adjust to its possibly new height.
    this.notifyBookmarksListResize_();

    this.hasScrollbars_ =
        this.$.bookmarks.scrollHeight > this.$.bookmarks.offsetHeight;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'power-bookmarks-list': PowerBookmarksListElement;
  }
}

customElements.define(PowerBookmarksListElement.is, PowerBookmarksListElement);
