// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(async function(/** @type {import('test_runner').TestRunner} */ testRunner) {
  const {dp, session} = await testRunner.startURL(
      'https://devtools.test:8443/inspector-protocol/attribution-reporting/resources/permissions-policy-no-conversion-measurement.php',
      'Test that registering a trigger using a subresource request triggers an issue when the attribution-reporting Permissions Policy is disabled.');

  await dp.Audits.enable();

  session.evaluate(`
    document.body.innerHTML = '<img src="/inspector-protocol/attribution-reporting/resources/register-trigger.php">'
  `);

  const issue = await dp.Audits.onceIssueAdded();

  testRunner.log(issue.params.issue, 'Issue reported: ', ['request']);
  testRunner.completeTest();
})
