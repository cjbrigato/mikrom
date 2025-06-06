(async function(/** @type {import('test_runner').TestRunner} */ testRunner) {
  const {tabTargetSession} = await testRunner.startBlankWithTabTarget(
      `Test that prerender navigations receives the status updates`);

  const childTargetManager =
      new TestRunner.ChildTargetManager(testRunner, tabTargetSession);
  await childTargetManager.startAutoAttach();
  const session = childTargetManager.findAttachedSessionPrimaryMainFrame();
  const dp = session.protocol;
  await dp.Preload.enable();

  session.navigate('resources/simple-prerender-with-target-hint.html');

  // Pending
  const resultPending = await dp.Preload.oncePrerenderStatusUpdated();
  testRunner.log(resultPending);

  // Running
  testRunner.log(await dp.Preload.oncePrerenderStatusUpdated());

  // Ready
  testRunner.log(await dp.Preload.oncePrerenderStatusUpdated());

  // Activate prerendered page.
  session.evaluate(`document.getElementById('link').click()`);

  // Success
  const resultSuccess = await dp.Preload.oncePrerenderStatusUpdated();
  testRunner.log(resultSuccess);

  if (resultPending.params.key.loaderId !== resultSuccess.params.key.loaderId) {
    testRunner.log('loaderId should remain consistent.');
  }

  testRunner.completeTest();
});
