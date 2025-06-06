// META: timeout=long
// META: script=/common/get-host-info.sub.js
// META: script=../aggregation-service/support/aggregation-service.js
// META: script=resources/utils.js
// META: script=/shared-storage/resources/util.js

'use strict';

const reportPoller = new ReportPoller(
    '/.well-known/private-aggregation/report-shared-storage',
    '/.well-known/private-aggregation/debug/report-shared-storage',
    /*fullTimeoutMs=*/ 2000,
);

private_aggregation_promise_test(async () => {
  await addModuleOnce('resources/shared-storage-module.js');

  const data = {enableDebugMode: true, contributions: [{bucket: 1n, value: 2}]};
  await sharedStorage.run('contribute-to-histogram', {data, keepAlive: true});

  const {reports: [report], debug_reports: [debug_report]} =
      await reportPoller.pollReportsAndAssert(
          /*expectedNumReports=*/ 1, /*expectedNumDebugReports=*/ 1);

  verifyReport(
      report, /*api=*/ 'shared-storage',
      /*is_debug_enabled=*/ true,
      /*debug_key=*/ undefined,
      /*expected_payload=*/
      buildExpectedPayload(
          ONE_CONTRIBUTION_EXAMPLE, NUM_CONTRIBUTIONS_SHARED_STORAGE));

  verifyReportsIdenticalExceptPayload(report, debug_report);
}, 'run() that calls Private Aggregation with one contribution');

private_aggregation_promise_test(async () => {
  await addModuleOnce('resources/shared-storage-module.js');

  const data = {
    enableDebugMode: true,
    contributions: [{bucket: 1n, value: 2}, {bucket: 3n, value: 4}]
  };

  await sharedStorage.run('contribute-to-histogram', {data, keepAlive: true});

  const {reports: [report], debug_reports: [debug_report]} =
      await reportPoller.pollReportsAndAssert(
          /*expectedNumReports=*/ 1, /*expectedNumDebugReports=*/ 1);

  verifyReport(
      report, /*api=*/ 'shared-storage', /*is_debug_enabled=*/ true,
      /*debug_key=*/ undefined,
      /*expected_payload=*/
      buildExpectedPayload(
          MULTIPLE_CONTRIBUTIONS_EXAMPLE, NUM_CONTRIBUTIONS_SHARED_STORAGE));

  verifyReportsIdenticalExceptPayload(report, debug_report);
}, 'run() that calls Private Aggregation with multiple contributions');

private_aggregation_promise_test(async () => {
  await addModuleOnce('resources/shared-storage-module.js');

  const data = {
    enableDebugMode: true,
    enableDebugModeArgs: {debugKey: 1234n},
    contributions: [{bucket: 1n, value: 2}]
  };

  await sharedStorage.run('contribute-to-histogram', {data, keepAlive: true});

  const {reports: [report], debug_reports: [debug_report]} =
      await reportPoller.pollReportsAndAssert(
          /*expectedNumReports=*/ 1, /*expectedNumDebugReports=*/ 1);

  verifyReport(
      report, /*api=*/ 'shared-storage', /*is_debug_enabled=*/ true,
      /*debug_key=*/ '1234',
      /*expected_payload=*/
      buildExpectedPayload(
          ONE_CONTRIBUTION_EXAMPLE, NUM_CONTRIBUTIONS_SHARED_STORAGE));

  verifyReportsIdenticalExceptPayload(report, debug_report);
}, 'run() that calls Private Aggregation with debug key');


private_aggregation_promise_test(async () => {
  await addModuleOnce('resources/shared-storage-module.js');

  const data = {
    enableDebugMode: true,
    enableDebugModeArgs: {debugKey: 1234n},
    contributions: [{bucket: 1n, value: 2}, {bucket: 3n, value: 4}]
  };

  await sharedStorage.run('contribute-to-histogram', {data, keepAlive: true});

  const {reports: [report], debug_reports: [debug_report]} =
      await reportPoller.pollReportsAndAssert(
          /*expectedNumReports=*/ 1, /*expectedNumDebugReports=*/ 1);

  verifyReport(
      report, /*api=*/ 'shared-storage', /*is_debug_enabled=*/ true,
      /*debug_key=*/ '1234',
      /*expected_payload=*/
      buildExpectedPayload(
          MULTIPLE_CONTRIBUTIONS_EXAMPLE, NUM_CONTRIBUTIONS_SHARED_STORAGE));

  verifyReportsIdenticalExceptPayload(report, debug_report);
}, 'run() that calls Private Aggregation with multiple contributions and a debug key');

private_aggregation_promise_test(async () => {
  await addModuleOnce('resources/shared-storage-module.js');

  const data = {
    enableDebugMode: false,
    contributions: [{bucket: 1n, value: 2}]
  };

  await sharedStorage.run('contribute-to-histogram', {data, keepAlive: true});

  const {reports: [report]} = await reportPoller.pollReportsAndAssert(
      /*expectedNumReports=*/ 1, /*expectedNumDebugReports=*/ 0);

  // Note that the payload cannot be tested as debug mode is disabled.
  verifyReport(
      report, /*api=*/ 'shared-storage', /*is_debug_enabled=*/ false,
      /*debug_key=*/ undefined,
      /*expected_payload=*/ undefined);
}, 'run() that calls Private Aggregation without debug mode');

private_aggregation_promise_test(async () => {
  await addModuleOnce('resources/shared-storage-module.js');

  const data = {
    enableDebugModeAfterOp: true,
    enableDebugModeArgs: {debugKey: 1234n},
    contributions: [{bucket: 1n, value: 2}]
  };

  await sharedStorage.run('contribute-to-histogram', {data, keepAlive: true});

  const {reports: [report], debug_reports: [debug_report]} =
      await reportPoller.pollReportsAndAssert(
          /*expectedNumReports=*/ 1, /*expectedNumDebugReports=*/ 1);

  verifyReport(
      report, /*api=*/ 'shared-storage', /*is_debug_enabled=*/ true,
      /*debug_key=*/ '1234',
      /*expected_payload=*/
      buildExpectedPayload(
          ONE_CONTRIBUTION_EXAMPLE, NUM_CONTRIBUTIONS_SHARED_STORAGE));

  verifyReportsIdenticalExceptPayload(report, debug_report);
}, 'run() that calls enableDebugMode() after contributeToHistogram()');


private_aggregation_promise_test(async () => {
  await addModuleOnce('resources/shared-storage-module.js');

  const data_1 = {enableDebugMode: true, contributions: []};
  const data_2 = {contributions: [{bucket: 1n, value: 2}]};

  await sharedStorage.run(
      'contribute-to-histogram', {data: data_1, keepAlive: true});
  await sharedStorage.run(
      'contribute-to-histogram', {data: data_2, keepAlive: true});

  const {reports: [report]} = await reportPoller.pollReportsAndAssert(
      /*expectedNumReports=*/ 1, /*expectedNumDebugReports=*/ 0);

  verifyReport(
      report, /*api=*/ 'shared-storage', /*is_debug_enabled=*/ false,
      /*debug_key=*/ undefined,
      /*expected_payload=*/ undefined);
}, 'calling enableDebugMode() in a different operation doesn\'t apply');


private_aggregation_promise_test(async () => {
  await addModuleOnce('resources/shared-storage-module.js');

  const data_1 = {
    enableDebugMode: true,
    contributions: [{bucket: 1n, value: 2}]
  };
  const data_2 = {
    enableDebugMode: true,
    contributions: [{bucket: 1n, value: 2}, {bucket: 3n, value: 4}]
  };

  await sharedStorage.run(
      'contribute-to-histogram', {data: data_1, keepAlive: true});
  await sharedStorage.run(
      'contribute-to-histogram', {data: data_2, keepAlive: true});

  // We don't verify the reports as they could arrive in a different order.
  await reportPoller.pollReportsAndAssert(
      /*expectedNumReports=*/ 2, /*expectedNumDebugReports=*/ 2);
}, 'reports from multiple operations aren\'t batched');

private_aggregation_promise_test(async () => {
  await addModuleOnce('resources/shared-storage-module.js');

  const data = {
    enableDebugMode: true,
    contributions:
        [{bucket: 1n, value: 2}, {bucket: 3n, value: 3}, {bucket: 3n, value: 1}]
  };
  await sharedStorage.run('contribute-to-histogram', {data, keepAlive: true});

  const {reports: [report], debug_reports: [debug_report]} =
      await reportPoller.pollReportsAndAssert(
          /*expectedNumReports=*/ 1, /*expectedNumDebugReports=*/ 1);

  verifyReport(
      report, /*api=*/ 'shared-storage', /*is_debug_enabled=*/ true,
      /*debug_key=*/ undefined,
      /*expected_payload=*/
      buildExpectedPayload(
          MULTIPLE_CONTRIBUTIONS_EXAMPLE, NUM_CONTRIBUTIONS_SHARED_STORAGE));

  verifyReportsIdenticalExceptPayload(report, debug_report);
}, 'run() that calls Private Aggregation with mergeable contributions');

private_aggregation_promise_test(async () => {
  await addModuleOnce('resources/shared-storage-module.js');

  const data = {
    enableDebugMode: true,
    contributions:
        [{bucket: 1n, value: 2}, {bucket: 3n, value: 4}, {bucket: 4n, value: 0}]
  };
  await sharedStorage.run('contribute-to-histogram', {data, keepAlive: true});

  const {reports: [report], debug_reports: [debug_report]} =
      await reportPoller.pollReportsAndAssert(
          /*expectedNumReports=*/ 1, /*expectedNumDebugReports=*/ 1);

  verifyReport(
      report, /*api=*/ 'shared-storage', /*is_debug_enabled=*/ true,
      /*debug_key=*/ undefined,
      /*expected_payload=*/
      buildExpectedPayload(
          MULTIPLE_CONTRIBUTIONS_EXAMPLE, NUM_CONTRIBUTIONS_SHARED_STORAGE));

  verifyReportsIdenticalExceptPayload(report, debug_report);
}, 'run() that calls Private Aggregation with zero-value contributions');

private_aggregation_promise_test(async () => {
  await addModuleOnce('resources/shared-storage-module.js');

  let contributions = [];
  for (let i = 1; i <= NUM_CONTRIBUTIONS_SHARED_STORAGE + 1;
       i++) {  // Too many contributions
    contributions.push({bucket: BigInt(i), value: 1});
  }

  const data = {enableDebugMode: true, contributions};
  await sharedStorage.run('contribute-to-histogram', {data, keepAlive: true});

  const {reports: [report], debug_reports: [debug_report]} =
      await reportPoller.pollReportsAndAssert(
          /*expectedNumReports=*/ 1, /*expectedNumDebugReports=*/ 1);

  verifyReport(
      report, /*api=*/ 'shared-storage', /*is_debug_enabled=*/ true,
      /*debug_key=*/ undefined,
      /*expected_payload=*/
      buildPayloadWithSequentialContributions(
          NUM_CONTRIBUTIONS_SHARED_STORAGE));

  verifyReportsIdenticalExceptPayload(report, debug_report);
}, 'run() that calls Private Aggregation with too many contributions');

private_aggregation_promise_test(async () => {
  await addModuleOnce('resources/shared-storage-module.js');

  let contributions = [];
  for (let i = 1n; i <= 21n; i++) {  // Too many contributions ignoring merging
    contributions.push({bucket: 1n, value: 1});
  }

  const data = {enableDebugMode: true, contributions};
  await sharedStorage.run('contribute-to-histogram', {data, keepAlive: true});

  const {reports: [report], debug_reports: [debug_report]} =
      await reportPoller.pollReportsAndAssert(
          /*expectedNumReports=*/ 1, /*expectedNumDebugReports=*/ 1);

  verifyReport(
      report, /*api=*/ 'shared-storage', /*is_debug_enabled=*/ true,
      /*debug_key=*/ undefined,
      /*expected_payload=*/
      buildExpectedPayload(
          ONE_CONTRIBUTION_HIGHER_VALUE_EXAMPLE,
          NUM_CONTRIBUTIONS_SHARED_STORAGE));

  verifyReportsIdenticalExceptPayload(report, debug_report);
}, 'run() that calls Private Aggregation with many mergeable contributions');

private_aggregation_promise_test(async () => {
  await addModuleOnce('resources/shared-storage-module.js');

  let contributions = []
  // Sums to value 1 if overflow is allowed.
  contributions.push({bucket: 1n, value: 2147483647});
  contributions.push({bucket: 1n, value: 1073741824});
  contributions.push({bucket: 1n, value: 1073741824});
  contributions.push({bucket: 1n, value: 2});

  const data = {enableDebugMode: true, contributions};
  await sharedStorage.run('contribute-to-histogram', {data, keepAlive: true});

  // The final contribution should succeed, but the first three should fail.
  const {reports: [report], debug_reports: [debug_report]} =
      await reportPoller.pollReportsAndAssert(
          /*expectedNumReports=*/ 1, /*expectedNumDebugReports=*/ 1);

  verifyReport(
      report, /*api=*/ 'shared-storage', /*is_debug_enabled=*/ true,
      /*debug_key=*/ undefined,
      /*expected_payload=*/
      buildExpectedPayload(
          ONE_CONTRIBUTION_EXAMPLE, NUM_CONTRIBUTIONS_SHARED_STORAGE));
}, 'run() that calls Private Aggregation with values that sum to more than the max long');
