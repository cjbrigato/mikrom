// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import type {ChromeEvent} from '/tools/typescript/definitions/chrome_event.js';

export interface OpenInNewTabParams {
  active?: boolean;
  split?: boolean;
}

export interface BookmarkManagerApiProxy {
  onDragEnter:
      ChromeEvent<(p1: chrome.bookmarkManagerPrivate.DragData) => void>;

  drop(parentId: string, index?: number): Promise<void>;
  startDrag(
      idList: string[], dragNodeIndex: number, isFromTouch: boolean, x: number,
      y: number): void;
  removeTrees(idList: string[]): Promise<void>;
  canPaste(parentId: string): Promise<boolean>;
  isActiveTabInSplit(): Promise<boolean>;
  openInNewWindow(idList: string[], incognito: boolean): void;
  openInNewTab(id: string, params?: OpenInNewTabParams): void;
  openInNewTabGroup(idList: string[]): void;
  cut(idList: string[]): Promise<void>;
  paste(parentId: string, selectedIdList?: string[]): Promise<void>;
  copy(idList: string[]): Promise<void>;
}

export class BookmarkManagerApiProxyImpl implements BookmarkManagerApiProxy {
  onDragEnter = chrome.bookmarkManagerPrivate.onDragEnter;

  drop(parentId: string, index?: number) {
    return chrome.bookmarkManagerPrivate.drop(parentId, index);
  }

  startDrag(
      idList: string[], dragNodeIndex: number, isFromTouch: boolean, x: number,
      y: number) {
    return chrome.bookmarkManagerPrivate.startDrag(
        idList, dragNodeIndex, isFromTouch, x, y);
  }

  removeTrees(idList: string[]) {
    return chrome.bookmarkManagerPrivate.removeTrees(idList);
  }

  canPaste(parentId: string) {
    return chrome.bookmarkManagerPrivate.canPaste(parentId);
  }

  isActiveTabInSplit() {
    return chrome.bookmarkManagerPrivate.isActiveTabInSplit();
  }

  openInNewWindow(idList: string[], incognito: boolean) {
    chrome.bookmarkManagerPrivate.openInNewWindow(idList, incognito);
  }

  openInNewTab(id: string, params: OpenInNewTabParams) {
    chrome.bookmarkManagerPrivate.openInNewTab(
        id, {active: params.active, split: params.split});
  }

  openInNewTabGroup(idList: string[]) {
    chrome.bookmarkManagerPrivate.openInNewTabGroup(idList);
  }

  cut(idList: string[]) {
    return chrome.bookmarkManagerPrivate.cut(idList);
  }

  paste(parentId: string, selectedIdList?: string[]) {
    return chrome.bookmarkManagerPrivate.paste(parentId, selectedIdList);
  }

  copy(idList: string[]) {
    return chrome.bookmarkManagerPrivate.copy(idList);
  }


  static getInstance(): BookmarkManagerApiProxy {
    return instance || (instance = new BookmarkManagerApiProxyImpl());
  }

  static setInstance(obj: BookmarkManagerApiProxy) {
    instance = obj;
  }
}

let instance: BookmarkManagerApiProxy|null = null;
