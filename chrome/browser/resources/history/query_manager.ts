// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {HistoryEmbeddingsUserActions, QUERY_RESULT_MINIMUM_AGE} from 'chrome://resources/cr_components/history/constants.js';
import type {QueryResult, QueryState} from 'chrome://resources/cr_components/history/history.mojom-webui.js';
import {EventTracker} from 'chrome://resources/js/event_tracker.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {BrowserServiceImpl} from './browser_service.js';
import {RESULTS_PER_PAGE} from './constants.js';

// Converts a JS Date object to a human readable string in the format of
// YYYY-MM-DD for the query.
export function convertDateToQueryValue(date: Date) {
  const fullYear = date.getFullYear();
  const month = date.getMonth() + 1; /* Month is 0-indexed. */
  const day = date.getDate();

  function twoDigits(value: number): string {
    return value >= 10 ? `${value}` : `0${value}`;
  }

  return `${fullYear}-${twoDigits(month)}-${twoDigits(day)}`;
}

declare global {
  interface HTMLElementTagNameMap {
    'history-query-manager': HistoryQueryManagerElement;
  }
}

export class HistoryQueryManagerElement extends CrLitElement {
  static get is() {
    return 'history-query-manager';
  }

  static get template() {
    return null;
  }

  static override get properties() {
    return {
      queryState: {
        type: Object,
        notify: true,
      },

      queryResult: {
        type: Object,
      },
    };
  }

  accessor queryState: QueryState;
  accessor queryResult: QueryResult;
  private eventTracker_: EventTracker = new EventTracker();
  /**
   * When this is non-null, that means there's a QueryResult that's pending
   * metrics logging since this debouncer timestamp. The debouncing is needed
   * because queries are issued as the user types, and we want to skip logging
   * these trivial queries the user typed through.
   */
  private resultPendingMetricsTimestamp_: number|null = null;

  constructor() {
    super();

    this.queryState = {
      // Whether the most recent query was incremental.
      incremental: false,
      // A query is initiated by page load.
      querying: true,
      searchTerm: '',
      after: '',
    };
  }

  override connectedCallback() {
    super.connectedCallback();
    this.eventTracker_.add(
        document, 'change-query', this.onChangeQuery_.bind(this));
    this.eventTracker_.add(
        document, 'query-history', this.onQueryHistory_.bind(this));
    this.eventTracker_.add(document, 'visibilitychange', () => {
      if (document.visibilityState === 'hidden') {
        this.flushDebouncedQueryResultMetric_();
      }
    });
  }

  override disconnectedCallback() {
    super.disconnectedCallback();
    this.flushDebouncedQueryResultMetric_();
    this.eventTracker_.removeAll();
  }

  initialize() {
    this.queryHistory_(false /* incremental */);
  }

  private queryHistory_(incremental: boolean) {
    this.queryState = {
      ...this.queryState,
      querying: true,
      incremental: incremental,
    };

    let afterTimestamp;
    if (loadTimeData.getBoolean('enableHistoryEmbeddings') &&
        this.queryState.after) {
      const afterDate = new Date(this.queryState.after);
      afterDate.setHours(0, 0, 0, 0);
      afterTimestamp = afterDate.getTime();
    }

    const browserService = BrowserServiceImpl.getInstance();
    const promise = incremental ?
        browserService.handler.queryHistoryContinuation() :
        browserService.handler.queryHistory(
            this.queryState.searchTerm, RESULTS_PER_PAGE,
            afterTimestamp ? afterTimestamp : null);
    // Ignore rejected (cancelled) queries.
    promise.then((result) => this.onQueryResult_(result.results), () => {});
  }

  private onChangeQuery_(e: CustomEvent<{search: string, after: string}>) {
    const changes = e.detail;
    let needsUpdate = false;

    if (changes.search !== null &&
        changes.search !== this.queryState.searchTerm) {
      this.queryState = {...this.queryState, searchTerm: changes.search};
      this.searchTermChanged_();
      needsUpdate = true;
    }

    if (loadTimeData.getBoolean('enableHistoryEmbeddings') &&
        changes.after !== null && changes.after !== this.queryState.after &&
        (Boolean(changes.after) || Boolean(this.queryState.after))) {
      this.queryState = {...this.queryState, after: changes.after};
      needsUpdate = true;
    }

    if (needsUpdate) {
      this.queryHistory_(false);
    }
  }

  private onQueryHistory_(e: CustomEvent<boolean>): boolean {
    this.queryHistory_(e.detail);
    return false;
  }

  /**
   * @param results List of results with information about the query.
   */
  private onQueryResult_(results: QueryResult) {
    this.queryState = {...this.queryState, querying: false};
    this.queryResult = {
      ...this.queryResult,
      info: results.info,
      value: results.value,
    };
    this.fire('query-finished', {result: this.queryResult});
  }

  private searchTermChanged_() {
    this.flushDebouncedQueryResultMetric_();

    // TODO(tsergeant): Ignore incremental searches in this metric.
    if (this.queryState.searchTerm) {
      BrowserServiceImpl.getInstance().recordAction('Search');
      this.resultPendingMetricsTimestamp_ = performance.now();
    }
  }

  /**
   * Flushes any pending query result metric waiting to be logged.
   */
  private flushDebouncedQueryResultMetric_() {
    if (this.resultPendingMetricsTimestamp_ &&
        (performance.now() - this.resultPendingMetricsTimestamp_) >=
            QUERY_RESULT_MINIMUM_AGE) {
      BrowserServiceImpl.getInstance().recordHistogram(
          'History.Embeddings.UserActions',
          HistoryEmbeddingsUserActions.NON_EMPTY_QUERY_HISTORY_SEARCH,
          HistoryEmbeddingsUserActions.END);
    }

    // Clear this regardless if it was recorded or not, because we don't want
    // to "try again" to record the same query.
    this.resultPendingMetricsTimestamp_ = null;
  }
}

customElements.define(
    HistoryQueryManagerElement.is, HistoryQueryManagerElement);
