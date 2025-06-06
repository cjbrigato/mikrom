// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(async function(/** @type {import('test_runner').TestRunner} */ testRunner) {
  const {dp, session} = await testRunner.startBlank(
      'Test that an attributionsrc request that is eligible for sources and triggers triggers an issue when it tries to register a source and trigger together.');

  await dp.Audits.enable();

  session.evaluateAsync(`
    fetch('/inspector-protocol/attribution-reporting/resources/register-source-and-trigger.php',
        {keepalive: true,
         attributionReporting: {
          eventSourceEligible: true,
          triggerEligible: true,
        }})
  `);

  const issue = await dp.Audits.onceIssueAdded();

  testRunner.log(issue.params.issue, 'Issue reported: ', ['request']);
  testRunner.completeTest();
})
